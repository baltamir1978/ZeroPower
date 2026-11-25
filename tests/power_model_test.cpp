/*
 * SPDX-License-Identifier: MIT
 */

#include "power_logic.h"
#include "power_settings.h"
#include "sudo_password.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

void test_catalog() {
    // Cada ajuste tiene clave única, opciones y un valor por defecto que es una de ellas.
    for (const auto& info : power::all_settings()) {
        CHECK(!info.options.empty());
        CHECK(power::option_index(info, info.default_value) >= 0);
        CHECK(power::find_setting(info.key) == &info);
    }
    CHECK(power::find_setting("nope") == nullptr);
    CHECK(power::settings_in(power::Group::Hardware).size() == 10);
    CHECK(power::settings_in(power::Group::Saving).size() == 6);
    CHECK(power::settings_in(power::Group::Services).size() == 7);

    const auto* eth = power::find_setting("ethernet");
    CHECK(eth && power::step_option(*eth, "on", 1) == "auto");
    CHECK(eth && power::step_option(*eth, "off", 1) == "on");  // vuelta
    CHECK(eth && power::step_option(*eth, "on", -1) == "off");
    CHECK(eth && power::option_label(*eth, "auto") == "Auto-suspend");
    const auto* gpu = power::find_setting("gpu_mem");
    // Un valor puesto a mano que no es opción: se salta a la primera.
    CHECK(gpu && power::step_option(*gpu, "48", 1) == "16");
    CHECK(gpu && power::option_label(*gpu, "48") == "48");
    CHECK(gpu && gpu->needs_reboot);
}

void test_key_values_and_daemon_config() {
    const auto kv = power::parse_key_values(
        "# comentario\n ethernet = auto \nwifi=on\n\nbasura\n=sin clave\ngpu_mem=16\r\n");
    CHECK(kv.size() == 3);
    CHECK(kv.at("ethernet") == "auto");
    CHECK(kv.at("gpu_mem") == "16");

    auto config = power::daemon_config_from({});
    CHECK(config.idle_audio && config.idle_launcher && !config.quiet_launcher && config.low_battery_percent == 0);
    CHECK(config.idle_delay_s == 5 && config.desktop_unplug_min == 0);
    CHECK(config.screen_timeout_s == 300);
    CHECK(power::daemon_config_from({{"screen_timeout", "60"}}).screen_timeout_s == 60);
    CHECK(power::daemon_config_from({{"screen_timeout", "0"}}).screen_timeout_s == 0);
    CHECK(power::daemon_config_from({{"screen_timeout", "7"}}).screen_timeout_s == 300); // no es opción
    config = power::daemon_config_from(
        {{"idle_saver", "off"}, {"quiet_launcher", "on"}, {"low_battery", "8"}});
    CHECK(!config.idle_audio && !config.idle_launcher && config.quiet_launcher && config.low_battery_percent == 8);
    config = power::daemon_config_from({{"idle_launcher", "off"}, {"idle_delay", "30"}, {"desktop_unplug", "5"}});
    CHECK(config.idle_audio && !config.idle_launcher && config.idle_delay_s == 30 && config.desktop_unplug_min == 5);
    // El ajuste de antes (idle_saver) vale, y los nuevos mandan si están.
    config = power::daemon_config_from({{"idle_saver", "off"}, {"idle_audio", "on"}, {"idle_delay", "7"}});
    CHECK(config.idle_audio && !config.idle_launcher && config.idle_delay_s == 5);
    // Valores no válidos: se quedan los de por defecto.
    config = power::daemon_config_from({{"idle_saver", "maybe"}, {"low_battery", "50"}});
    CHECK(config.idle_audio && config.idle_launcher && config.low_battery_percent == 0);
    // Ida y vuelta por el texto.
    power::DaemonConfig c;
    c.idle_audio = false;
    c.idle_delay_s = 10;
    c.desktop_unplug_min = 15;
    c.quiet_launcher = true;
    c.low_battery_percent = 10;
    c.screen_timeout_s = 120;
    const auto back = power::daemon_config_from(power::parse_key_values(power::daemon_config_text(c)));
    CHECK(!back.idle_audio && back.idle_launcher && back.quiet_launcher && back.low_battery_percent == 10);
    CHECK(back.idle_delay_s == 10 && back.desktop_unplug_min == 15);
    CHECK(back.screen_timeout_s == 120);
}

void test_battery_math() {
    power::CurrentAverage avg(3);
    CHECK(avg.empty() && avg.value() == 0.0);
    avg.add(300);
    avg.add(400);
    avg.add(500);
    avg.add(600); // la primera sale de la ventana
    CHECK(std::fabs(avg.value() - 500.0) < 1e-9);
    CHECK(std::fabs(power::hours_left(1750, 350) - 5.0) < 1e-9);
    CHECK(power::hours_left(1750, -200) < 0); // cargando
    CHECK(power::format_hours(4.5833) == "4 h 35 min");
    CHECK(power::format_hours(0.5) == "30 min");
    CHECK(power::format_hours(-1) == "—");
}

void test_low_battery_guard() {
    power::LowBatteryGuard guard(3);
    CHECK(!guard.update(true, 7, 8));
    CHECK(!guard.update(true, 7, 8));
    CHECK(guard.update(true, 7, 8)); // tercera seguida
    guard = power::LowBatteryGuard(3);
    CHECK(!guard.update(true, 7, 8));
    CHECK(!guard.update(false, 7, 8)); // enchufado: se reinicia la cuenta
    CHECK(guard.strikes() == 0);
    CHECK(!guard.update(true, 9, 8));  // por encima del umbral
    CHECK(!guard.update(true, 1, 0));  // desactivado
}

void test_idle_tracker() {
    using A = power::IdleTracker::Action;
    power::IdleTracker idle(5.0);
    CHECK(idle.update(0, 50, false, true) == A::None);
    CHECK(idle.update(1, 0, false, true) == A::None); // se apaga la pantalla
    CHECK(idle.update(5, 0, false, true) == A::None); // 4 s
    CHECK(idle.update(6, 0, false, true) == A::Sleep);
    CHECK(idle.sleeping());
    CHECK(idle.update(7, 0, false, true) == A::None); // ya dormido
    CHECK(idle.update(8, 0, true, true) == A::Wake);  // tecla
    CHECK(!idle.sleeping());
    CHECK(idle.update(9, 0, false, true) == A::None); // vuelve a contar desde 0
    CHECK(idle.update(14, 0, false, true) == A::Sleep);
    CHECK(idle.update(15, 80, false, true) == A::Wake); // pantalla encendida
    // Apagar la opción estando dormido despierta.
    CHECK(idle.update(20, 0, false, true) == A::None);
    CHECK(idle.update(26, 0, false, true) == A::Sleep);
    CHECK(idle.update(27, 0, false, false) == A::Wake);
    CHECK(idle.update(40, 0, false, false) == A::None);
}

void test_screen_timeout() {
    using A = power::ScreenTimeout::Action;
    // El caso del 22-nov-2026: el lanzador despierta y deja la pantalla encendida.
    power::ScreenTimeout t;
    CHECK(t.update(0, 100, false, 60) == A::None);
    CHECK(t.update(59, 100, false, 60) == A::None);
    CHECK(t.update(60, 100, false, 60) == A::TurnOff);
    CHECK(t.forced_off() && t.saved_brightness() == 100);
    CHECK(t.update(61, 0, false, 60) == A::None); // sigue apagada
    CHECK(t.update(500, 0, false, 60) == A::None);
    CHECK(t.update(501, 0, true, 60) == A::TurnOn); // tecla: vuelve el brillo
    CHECK(!t.forced_off());
    CHECK(t.update(502, 100, false, 60) == A::None);
    CHECK(t.update(530, 100, true, 60) == A::None); // tecla: vuelve a contar
    CHECK(t.update(589, 100, false, 60) == A::None);
    CHECK(t.update(590, 100, false, 60) == A::TurnOff);

    // Si otro la enciende mientras está apagada por nosotros, deja de ser nuestra.
    CHECK(t.update(600, 70, false, 60) == A::None);
    CHECK(!t.forced_off());
    CHECK(t.update(601, 70, true, 60) == A::None); // tecla normal: nada que devolver
    CHECK(t.update(661, 70, false, 60) == A::TurnOff);
    CHECK(t.saved_brightness() == 70);

    // Desactivar la opción con la pantalla apagada por nosotros la enciende.
    CHECK(t.update(662, 0, false, 0) == A::TurnOn);
    CHECK(t.update(5000, 70, false, 0) == A::None); // desactivado: nunca apaga

    // Pantalla apagada por el lanzador: no se toca; al encenderse se cuenta desde ahí.
    power::ScreenTimeout u;
    CHECK(u.update(0, 0, false, 60) == A::None);
    CHECK(u.update(1000, 0, false, 60) == A::None);
    CHECK(u.update(1001, 50, false, 60) == A::None); // una app la enciende sola
    CHECK(u.update(1060, 50, false, 60) == A::None);
    CHECK(u.update(1061, 50, false, 60) == A::TurnOff);

    // Sin pantalla (brillo -1): nada.
    power::ScreenTimeout v;
    CHECK(v.update(0, -1, false, 60) == A::None);
    CHECK(v.update(1000, -1, false, 60) == A::None);
}

void test_unplug_timer() {
    power::UnplugTimer u;
    CHECK(!u.update(0, true, 1));
    CHECK(!u.update(10, false, 1));  // empieza a contar
    CHECK(!u.update(69, false, 1));
    CHECK(u.update(70, false, 1));   // un minuto desconectado
    CHECK(!u.update(200, false, 1)); // una vez por desconexión
    CHECK(!u.update(210, true, 1));  // se vuelve a conectar
    CHECK(!u.update(220, false, 1));
    CHECK(!u.update(250, true, 1));  // cable flojo: vuelve antes de tiempo
    CHECK(!u.update(260, false, 1));
    CHECK(!u.update(300, false, 1));
    CHECK(u.update(320, false, 1));
    power::UnplugTimer never;
    CHECK(!never.update(0, false, 0) && !never.update(100000, false, 0));
}

void test_sudo_password() {
    using power::SudoOutcome;
    CHECK(power::classify_sudo(0, "ethernet=off\n") == SudoOutcome::Ok);
    CHECK(power::classify_sudo(1, "sudo: a password is required\n") == SudoOutcome::NeedsPassword);
    CHECK(power::classify_sudo(1, "Sorry, try again.\nsudo: no password was provided\n"
                                  "sudo: 1 incorrect password attempt\n") == SudoOutcome::WrongPassword);
    CHECK(power::classify_sudo(1, "sudo: no password was provided\n") == SudoOutcome::NeedsPassword);
    CHECK(power::classify_sudo(1, "pi is not in the sudoers file.\n") == SudoOutcome::NotAllowed);
    CHECK(power::classify_sudo(1, "Sorry, user pi is not allowed to execute '/usr/lib/zeropower/zeropower-helper "
                                  "set ethernet off' as root on cpz.\n") == SudoOutcome::NotAllowed);
    CHECK(power::classify_sudo(2, "invalid value\n") == SudoOutcome::Failed);

    power::SecretInput in;
    CHECK(in.empty() && in.masked().empty());
    CHECK(in.add('p') && in.add('W') && in.add('!') && in.add(' '));
    CHECK(!in.add('\n') && !in.add(0x7F) && !in.add(0xE9)); // control y no ASCII: fuera
    CHECK(in.size() == 4);
    CHECK(in.masked() == "\u2022\u2022\u2022\u2022");
    CHECK(in.masked(2) == "\u2026\u2022\u2022");
    CHECK(in.backspace() && in.size() == 3);
    std::string line = in.take_line();
    CHECK(line == "pW!\n");
    CHECK(in.empty() && !in.backspace());
    power::wipe(line);
    CHECK(line.empty());

    for (std::size_t i = 0; i < power::SecretInput::kMaxLength; ++i) {
        CHECK(in.add('a'));
    }
    CHECK(!in.add('a'));
    in.clear();
    CHECK(in.empty());
}

} // namespace

void test_usb_switch() {
    using power::UsbSwitch;
    // Lo que se ve en el Cardputer Zero: root hub (1d6b), hub Genesys (05e3), Ethernet (0fe6).
    CHECK(power::usb_switch_state(true, {"1d6b", "05e3", "0fe6"}) == UsbSwitch::Host);
    CHECK(power::usb_switch_state(true, {"1d6b"}) == UsbSwitch::Slave);
    CHECK(power::usb_switch_state(true, {}) == UsbSwitch::Slave);
    CHECK(power::usb_switch_state(false, {"05e3"}) == UsbSwitch::Unknown);
}

int main() {
    test_catalog();
    test_key_values_and_daemon_config();
    test_battery_math();
    test_low_battery_guard();
    test_idle_tracker();
    test_screen_timeout();
    test_unplug_timer();
    test_sudo_password();
    test_usb_switch();
    if (failures == 0) {
        std::printf("power_model_test: todo bien\n");
    }
    return failures == 0 ? 0 : 1;
}
