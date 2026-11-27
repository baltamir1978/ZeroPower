/*
 * SPDX-License-Identifier: MIT
 */

// zeropowerd: lo que ZeroPower hace mientras usas el aparato. Es un servicio del sistema que
// corre como el usuario del aparato (User=1000, el del lanzador), no como root:
//
// - Pantalla apagada (brillo 0) durante `idle_delay` s (5 por defecto): pausa el lanzador
//   (SIGSTOP) y aparca su audio en un sumidero nulo para que el amplificador y la tarjeta de
//   sonido duerman (cada cosa con su ajuste: `idle_launcher`, `idle_audio`). Una tecla o
//   la pantalla encendida lo despiertan. Si hay una app abierta desde el lanzador, no se
//   pausa nada (la app seguiría sin pantalla, pero quizá sonando o grabando).
// - «Quiet launcher»: el audio del lanzador aparcado siempre (sin sonido de teclas).
// - Batería baja: tras tres lecturas seguidas en el umbral, pone un aviso en la pantalla
//   (pausando el lanzador y la app abierta para que no lo tapen) y, a los 10 s, apaga
//   ordenadamente. Si en esos 10 s se enchufa el cargador, no apaga y lo deja todo como
//   estaba. Al final del apagado el controlador del panel deja la pantalla en negro.
// - Tiempo de pantalla: si la pantalla sigue encendida tras `screen_timeout` sin teclas (el
//   lanzador no la ha apagado, o hay una app abierta), la apaga él; una tecla la enciende.
// - Con Desk Mode: cierra su escritorio HDMI (una unidad de usuario) cuando el HDMI lleva
//   `desktop_unplug` minutos desconectado.
//
// Las ideas (y las medidas de cuánto ahorra cada una) son de cz-battery-saver
// (github.com/InstantComet/cz-battery-saver, MIT); el código es propio.
//
// Gasto propio: duerme en poll() sobre el teclado con 1 s de tope; lee el brillo cada
// segundo, la batería cada 20 s y sus opciones cada 3 s.

#include "battery.h"
#include "framebuffer.h"
#include "power_logic.h"
#include "power_settings.h"
#include "process.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void say(const char* fmt, const std::string& arg = "") {
    std::fprintf(stderr, "zeropowerd: ");
    std::fprintf(stderr, fmt, arg.c_str());
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

std::string config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string root = (xdg && *xdg) ? xdg : std::string(home ? home : "/tmp") + "/.config";
    return root + "/zeropower/power.conf";
}

// --- Lanzador ---

pid_t find_launcher() {
    DIR* proc = ::opendir("/proc");
    if (!proc) {
        return -1;
    }
    pid_t found = -1;
    while (dirent* e = ::readdir(proc)) {
        const pid_t pid = static_cast<pid_t>(std::atoi(e->d_name));
        if (pid <= 0) {
            continue;
        }
        const std::string cmd = platform::read_text_file("/proc/" + std::to_string(pid) + "/cmdline");
        if (cmd.find("M5CardputerZero-APPLaunch") != std::string::npos && cmd.rfind("/", 0) == 0) {
            found = pid;
            break;
        }
    }
    ::closedir(proc);
    return found;
}

// ¿Tiene el lanzador una app abierta? Devuelve el nombre del primer hijo vivo, o "".
// Los zombis no cuentan: el lanzador lanza `nmcli` y compañía de vez en cuando, y si se le
// pausa justo después, el hijo terminado se queda sin recoger y parecería una app abierta.
std::string open_app(pid_t parent) {
    const std::string children =
        platform::read_text_file("/proc/" + std::to_string(parent) + "/task/" + std::to_string(parent) + "/children");
    std::size_t pos = 0;
    while ((pos = children.find_first_of("0123456789", pos)) != std::string::npos) {
        const std::size_t end = children.find_first_not_of("0123456789", pos);
        const std::string child = children.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end;
        // /proc/<pid>/stat: "pid (comm) S ...": el estado va tras el último ')'.
        const std::string stat = platform::read_text_file("/proc/" + child + "/stat");
        const auto open = stat.find('(');
        const auto close = stat.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close + 2 >= stat.size()) {
            continue; // ya no existe
        }
        const char state = stat[close + 2];
        if (state == 'Z' || state == 'X') {
            continue;
        }
        return stat.substr(open + 1, close - open - 1);
    }
    return "";
}

// Hijos vivos del lanzador (las apps abiertas), para pausarlos con él mientras se ve el aviso
// de batería baja: también pintan en el framebuffer.
std::vector<pid_t> live_children(pid_t parent) {
    std::vector<pid_t> pids;
    const std::string children =
        platform::read_text_file("/proc/" + std::to_string(parent) + "/task/" + std::to_string(parent) + "/children");
    std::size_t pos = 0;
    while ((pos = children.find_first_of("0123456789", pos)) != std::string::npos) {
        const std::size_t end = children.find_first_not_of("0123456789", pos);
        pids.push_back(static_cast<pid_t>(std::atoi(children.substr(pos, end - pos).c_str())));
        pos = end;
    }
    return pids;
}

// --- Aviso de batería baja ---

constexpr const char* kLowBatteryScreen = "/usr/share/zeropower/screens/low-battery.rgb565";
constexpr int kWarningSeconds = 10;
constexpr int kWarningBrightness = 40; // de 100: que se lea sin gastar de más

// --- Audio (pactl de PipeWire) ---

const char* kNullSink = "zeropower_null";

// Índices de los flujos de audio del proceso `pid`.
std::vector<std::string> sink_inputs_of(pid_t pid) {
    const auto r = platform::run_process({"pactl", "list", "sink-inputs"});
    std::vector<std::string> out;
    std::string current;
    const std::string needle = "application.process.id = \"" + std::to_string(pid) + "\"";
    std::size_t pos = 0;
    while (pos < r.output.size()) {
        const std::size_t end = r.output.find('\n', pos);
        const std::string line = r.output.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? r.output.size() : end + 1;
        if (line.rfind("Sink Input #", 0) == 0) {
            current = line.substr(12);
        } else if (!current.empty() && line.find(needle) != std::string::npos) {
            out.push_back(current);
        }
    }
    return out;
}

class AudioParker {
public:
    // Mueve los flujos del lanzador al sumidero nulo (se puede llamar a menudo: el lanzador
    // a veces abre uno nuevo).
    void park(pid_t launcher) {
        if (launcher <= 0) {
            return;
        }
        if (module_.empty()) {
            const auto r = platform::run_process(
                {"pactl", "load-module", "module-null-sink", std::string("sink_name=") + kNullSink,
                 "sink_properties=device.description=ZeroPower"});
            if (r.exit_code != 0) {
                return;
            }
            module_ = r.output.substr(0, r.output.find_first_not_of("0123456789"));
            say("audio: launcher parked");
        }
        for (const auto& input : sink_inputs_of(launcher)) {
            platform::run_process({"pactl", "move-sink-input", input, kNullSink});
        }
    }

    void restore(pid_t launcher) {
        if (module_.empty()) {
            return;
        }
        auto sink = platform::run_process({"pactl", "get-default-sink"}).output;
        while (!sink.empty() && (sink.back() == '\n' || sink.back() == ' ')) {
            sink.pop_back();
        }
        if (launcher > 0 && !sink.empty() && sink != kNullSink) {
            for (const auto& input : sink_inputs_of(launcher)) {
                platform::run_process({"pactl", "move-sink-input", input, sink});
            }
        }
        // Al quitar el módulo, lo que quedara en él vuelve solo al sumidero por defecto.
        platform::run_process({"pactl", "unload-module", module_});
        module_.clear();
        say("audio: launcher restored");
    }

    bool parked() const {
        return !module_.empty();
    }

private:
    std::string module_;
};

// --- Teclado ---

int open_keyboard() {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/dev/input", ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("event", 0) != 0) {
            continue;
        }
        const int fd = ::open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        unsigned long keys[(KEY_MAX / (8 * sizeof(unsigned long))) + 1] = {};
        if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0) {
            const auto bit = [&](int code) {
                return (keys[code / (8 * sizeof(unsigned long))] >> (code % (8 * sizeof(unsigned long)))) & 1UL;
            };
            // Un teclado de verdad (letras y Enter), no los botones de encendido o el IR.
            if (bit(KEY_A) && bit(KEY_ENTER) && bit(KEY_ESC)) {
                return fd;
            }
        }
        ::close(fd);
    }
    return -1;
}

// Lee lo pendiente; true si había alguna pulsación.
bool drain_keys(int fd) {
    bool pressed = false;
    input_event ev{};
    while (::read(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
        if (ev.type == EV_KEY && ev.value == 1) {
            pressed = true;
        }
    }
    return pressed;
}

// Pulsaciones del teclado contadas por el kernel (interrupciones del TCA8418), o -1 si no hay.
// Si otra app se queda el teclado en exclusiva (EVIOCGRAB: cardputer-remote lo hace), las
// teclas ya no llegan por evdev, pero la interrupción sí: así la pantalla sigue despertando.
long long keyboard_irqs() {
    return power::irq_count(platform::read_text_file("/proc/interrupts"), "tca8418");
}

// Algún conector HDMI con pantalla (card*-HDMI-A-*/status = connected).
bool hdmi_connected() {
    DIR* dir = ::opendir("/sys/class/drm");
    if (!dir) {
        return false;
    }
    bool connected = false;
    while (auto* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("card", 0) == 0 && name.find("-HDMI-A-") != std::string::npos) {
            std::string status = platform::read_text_file("/sys/class/drm/" + name + "/status");
            if (status.rfind("connected", 0) == 0) {
                connected = true;
                break;
            }
        }
    }
    ::closedir(dir);
    return connected;
}

bool desk_mode_installed() {
    std::error_code ec;
    return fs::exists(power::kDeskModeDir, ec);
}

} // namespace

int main() {
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);
    std::signal(SIGHUP, on_signal);

    // ZEROPOWERD_BATTERY_DIR: una carpeta con `status` y `capacity` de mentira, para probar el
    // aviso de batería baja sin vaciarla.
    const char* fake_battery = std::getenv("ZEROPOWERD_BATTERY_DIR");
    const std::string battery_dir = fake_battery && *fake_battery ? fake_battery : platform::find_battery_dir();
    const std::string backlight_dir = platform::find_backlight_dir();
    const std::string conf = config_path();
    say("started, options in %s", conf);

    power::DaemonConfig config;
    long long config_mtime = -1;
    power::IdleTracker idle(5.0);
    power::UnplugTimer unplug;
    power::LowBatteryGuard low(3);
    power::ScreenTimeout screen;
    std::string app_name; // la app abierta desde el lanzador, para el registro
    bool powering_off = false;
    AudioParker audio;
    pid_t launcher = -1;
    bool launcher_stopped = false;
    int kbd = -1;
    long long last_irqs = keyboard_irqs();
    bool grab_noted = false;
    double next_config = 0, next_battery = 0, next_scan = 0, next_audio = 0;

    auto resume_launcher = [&]() {
        if (launcher_stopped && launcher > 0) {
            ::kill(launcher, SIGCONT);
            say("launcher resumed");
        }
        launcher_stopped = false;
    };

    while (!g_stop) {
        const double t = now_s();

        if (t >= next_config) {
            next_config = t + 3;
            struct stat st{};
            const long long mtime = ::stat(conf.c_str(), &st) == 0 ? static_cast<long long>(st.st_mtime) : 0;
            if (mtime != config_mtime) {
                config_mtime = mtime;
                config = power::daemon_config_from(power::parse_key_values(platform::read_text_file(conf)));
                idle.set_delay(config.idle_delay_s);
            }
        }

        if (t >= next_scan) {
            next_scan = t + 5;
            if (launcher <= 0 || ::kill(launcher, 0) != 0) {
                launcher = find_launcher();
                launcher_stopped = false;
            }
            if (kbd < 0) {
                kbd = open_keyboard();
            }
        }

        // Teclado: se duerme aquí (1 s como mucho; con la pantalla apagada, 0,3 s, porque si
        // otra app se ha quedado el teclado sólo se ve la tecla al despertar y mirar las
        // interrupciones: que no tarde en encenderse).
        bool key = false;
        bool evdev_events = false; // si llegó algo por el teclado (también soltar una tecla)
        const int wait_ms = last_irqs >= 0 && platform::read_brightness(backlight_dir) == 0 ? 300 : 1000;
        if (kbd >= 0) {
            pollfd pfd{kbd, POLLIN, 0};
            const int ready = ::poll(&pfd, 1, wait_ms);
            if (ready > 0) {
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    ::close(kbd);
                    kbd = -1;
                } else {
                    evdev_events = true;
                    key = drain_keys(kbd);
                }
            }
        } else {
            ::usleep(wait_ms * 1000);
        }
        // La misma tecla por las interrupciones (vale aunque otra app tenga el teclado).
        const long long irqs = keyboard_irqs();
        if (irqs >= 0 && last_irqs >= 0 && irqs != last_irqs && !evdev_events) {
            key = true;
            if (!grab_noted) {
                say("key seen only as an interrupt: another app has probably grabbed the keyboard");
                grab_noted = true;
            }
        }
        last_irqs = irqs;

        // Tiempo de pantalla: antes que el reposo, para que el lanzador se pause en cuanto
        // la apagamos y que, al despertar, el brillo ya esté de vuelta.
        int brightness = platform::read_brightness(backlight_dir);
        switch (screen.update(now_s(), brightness, key, config.screen_timeout_s)) {
            case power::ScreenTimeout::Action::TurnOff:
                if (platform::write_brightness(backlight_dir, 0)) {
                    say("screen on with no keys for %s s: turning it off",
                        std::to_string(config.screen_timeout_s));
                    brightness = 0;
                } else {
                    say("screen timeout: cannot write the brightness");
                }
                break;
            case power::ScreenTimeout::Action::TurnOn:
                platform::write_brightness(backlight_dir, screen.saved_brightness());
                brightness = screen.saved_brightness();
                say("key: screen back on");
                break;
            case power::ScreenTimeout::Action::None:
                break;
        }

        // Pantalla apagada.
        const std::string app = launcher > 0 ? open_app(launcher) : "";
        if (app != app_name) {
            say(app.empty() ? "app closed%s" : "app open: %s", app);
            app_name = app;
        }
        const bool app_open = !app.empty();
        const bool idle_enabled = (config.idle_audio || config.idle_launcher) && launcher > 0 && !app_open &&
                                  brightness >= 0;
        switch (idle.update(now_s(), brightness, key, idle_enabled)) {
            case power::IdleTracker::Action::Sleep:
                if (config.idle_audio) {
                    audio.park(launcher);
                }
                if (config.idle_launcher && ::kill(launcher, SIGSTOP) == 0) {
                    launcher_stopped = true;
                }
                say("screen off: %s", config.idle_audio && launcher_stopped ? "sound card asleep, launcher paused"
                                      : launcher_stopped                     ? "launcher paused"
                                                                             : "sound card asleep");
                break;
            case power::IdleTracker::Action::Wake:
                // Por qué: si un día se queda despierto sin motivo, que quede en el registro.
                say(key ? "wake: key%s" : (brightness > 0 ? "wake: screen turned on%s" : "wake: saver disabled or app opened%s"));
                resume_launcher();
                if (!config.quiet_launcher) {
                    audio.restore(launcher);
                }
                break;
            case power::IdleTracker::Action::None:
                break;
        }

        // Lanzador silencioso: se repasa de vez en cuando por si abre un flujo nuevo.
        if (t >= next_audio) {
            next_audio = t + 5;
            if (config.quiet_launcher && !launcher_stopped) {
                audio.park(launcher);
            } else if (!config.quiet_launcher && !idle.sleeping() && audio.parked()) {
                audio.restore(launcher);
            }
        }

        // Batería baja (y, de paso, el HDMI de Desk Mode).
        if (t >= next_battery) {
            next_battery = t + 20;
            if (unplug.update(t, hdmi_connected(), config.desktop_unplug_min) && desk_mode_installed()) {
                const auto active = platform::run_process({"systemctl", "--user", "is-active", power::kDeskModeUnit});
                if (active.output.rfind("active", 0) == 0) {
                    say("HDMI unplugged for %s min: closing the Desk Mode desktop",
                        std::to_string(config.desktop_unplug_min));
                    platform::run_process({"systemctl", "--user", "stop", power::kDeskModeUnit});
                }
            }
            const auto b = platform::read_battery(battery_dir);
            if (b.present && low.update(b.discharging(), b.capacity, config.low_battery_percent)) {
                say("battery at %s %%: warning on screen, powering off in 10 s", std::to_string(b.capacity));
                // Quieto todo lo que pinta en la pantalla (lo que ya estaba en pausa se queda
                // como estaba), y el aviso encima con la pantalla encendida.
                std::vector<pid_t> frozen;
                if (launcher > 0) {
                    for (pid_t child : live_children(launcher)) {
                        if (::kill(child, SIGSTOP) == 0) {
                            frozen.push_back(child);
                        }
                    }
                    if (!launcher_stopped && ::kill(launcher, SIGSTOP) == 0) {
                        frozen.push_back(launcher);
                    }
                }
                const int before = platform::read_brightness(backlight_dir);
                if (before == 0) {
                    platform::write_brightness(backlight_dir, kWarningBrightness);
                }
                platform::FramebufferImage warning;
                if (!warning.show(kLowBatteryScreen)) {
                    say("cannot draw the warning on the screen");
                }
                bool charger = false;
                for (int i = 0; i < kWarningSeconds && !g_stop && !charger; ++i) {
                    ::usleep(1000 * 1000);
                    const auto now = platform::read_battery(battery_dir);
                    charger = now.present && !now.discharging();
                }
                if (charger || g_stop) {
                    say(charger ? "charger connected: power-off cancelled%s" : "stopped: power-off cancelled%s");
                    warning.restore();
                    if (before == 0) {
                        platform::write_brightness(backlight_dir, 0);
                    }
                    for (pid_t pid : frozen) {
                        ::kill(pid, SIGCONT);
                    }
                    low = power::LowBatteryGuard(3);
                } else {
                    powering_off = true;
                    // Desde un servicio (fuera de la sesión) polkit puede no dejarlo: entonces
                    // con el sudo sin contraseña del usuario del aparato. El lanzador sigue en
                    // pausa: al apagar, systemd lo despierta para terminarlo.
                    if (platform::run_process({"systemctl", "poweroff", "--check-inhibitors=no"}).exit_code != 0) {
                        platform::run_process({"sudo", "-n", "systemctl", "poweroff", "--check-inhibitors=no"});
                    }
                }
            }
        }
    }

    // Al parar el servicio (o desinstalar) no se deja nada a medias. Al apagar por batería
    // no se despierta al lanzador, para que no pinte encima del aviso mientras se apaga.
    if (screen.forced_off() && !powering_off) {
        platform::write_brightness(backlight_dir, screen.saved_brightness());
    }
    if (!powering_off) { // al apagar, que no repinte encima del aviso
        resume_launcher();
    }
    audio.restore(launcher);
    if (kbd >= 0) {
        ::close(kbd);
    }
    say("stopped");
    return 0;
}
