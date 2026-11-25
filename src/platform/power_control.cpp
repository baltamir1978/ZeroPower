/*
 * SPDX-License-Identifier: MIT
 */

#include "power_control.h"

#include "power_settings.h"
#include "process.h"
#include "sudo_password.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace platform {
namespace {

namespace fs = std::filesystem;

constexpr const char* kHelper = "/usr/lib/zeropower/zeropower-helper";

std::string daemon_config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string root = (xdg && *xdg) ? xdg : std::string(home ? home : "/tmp") + "/.config";
    return root + "/zeropower/power.conf";
}

std::string last_line(const std::string& text) {
    std::string s = text;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    const auto nl = s.rfind('\n');
    return nl == std::string::npos ? s : s.substr(nl + 1);
}

} // namespace

PowerControl::PowerControl() {
    std::error_code ec;
    simulated_ = !fs::exists(kHelper, ec);
    if (simulated_) {
        for (const auto& info : power::all_settings()) {
            if (info.applier == power::Applier::System) {
                fake_system_[info.key] = info.default_value;
            }
        }
        fake_system_["gpu_mem_active"] = "64";
        const char* fake = std::getenv("ZEROPOWER_SIMULATE_SUDO");
        if (fake && *fake) {
            fake_sudo_password_ = std::string(fake) + "\n";
            fake_sudo_ok_ = false;
        }
    }
}

std::map<std::string, std::string> PowerControl::read_daemon_values() const {
    // Lo que no esté en el fichero vale lo de por defecto de zeropowerd.
    const auto config = power::daemon_config_from(power::parse_key_values(read_text_file(daemon_config_path())));
    std::error_code ec;
    return {
        {"idle_audio", config.idle_audio ? "on" : "off"},
        {"idle_launcher", config.idle_launcher ? "on" : "off"},
        {"idle_delay", std::to_string(config.idle_delay_s)},
        {"quiet_launcher", config.quiet_launcher ? "on" : "off"},
        {"low_battery", std::to_string(config.low_battery_percent)},
        {"screen_timeout", std::to_string(config.screen_timeout_s)},
        // Sin Desk Mode no hay escritorio que cerrar: la fila sale como «not installed».
        {"desktop_unplug", fs::exists(power::kDeskModeDir, ec) ? std::to_string(config.desktop_unplug_min) : "absent"},
    };
}

std::map<std::string, std::string> PowerControl::read_all() {
    std::map<std::string, std::string> values;
    if (simulated_) {
        values = fake_system_;
    } else {
        values = power::parse_key_values(run_process({kHelper, "status"}).output);
    }
    for (const auto& [k, v] : read_daemon_values()) {
        values[k] = v;
    }
    return values;
}

PowerControl::Result PowerControl::apply(const std::string& key, const std::string& value,
                                         std::string password_line) {
    struct Wiper {
        std::string& s;
        ~Wiper() {
            power::wipe(s);
        }
    } wiper{password_line};

    Result result;
    const power::SettingInfo* info = power::find_setting(key);
    if (!info || power::option_index(*info, value) < 0) {
        result.message = "Invalid value";
        return result;
    }

    if (info->applier == power::Applier::Daemon) {
        auto values = read_daemon_values();
        values[key] = value;
        const auto config = power::daemon_config_from(values);
        const fs::path path(daemon_config_path());
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        // Temporal + rename: zeropowerd nunca lee un fichero a medias.
        const std::string tmp = path.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << power::daemon_config_text(config);
            if (!out) {
                result.message = "Cannot write " + path.string();
                return result;
            }
        }
        fs::rename(tmp, path, ec);
        if (ec) {
            result.message = "Cannot write " + path.string();
            return result;
        }
        result.ok = true;
        result.value = value;
        return result;
    }

    if (simulated_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // como si tardara
        if (!fake_sudo_ok_) {
            if (password_line.empty()) {
                result.needs_password = true;
                return result;
            }
            if (password_line != fake_sudo_password_) {
                result.wrong_password = true;
                result.message = "Wrong password";
                return result;
            }
            fake_sudo_ok_ = true;
        }
        fake_system_[key] = value;
        result.ok = true;
        result.value = value;
        return result;
    }

    // LC_ALL=C: los mensajes de sudo, en inglés, para poder entenderlos.
    // -p "": sin el texto "[sudo] password for…" mezclado con la respuesta del ayudante.
    const auto r = password_line.empty()
                       ? run_process({"env", "LC_ALL=C", "sudo", "-n", kHelper, "set", key, value})
                       : run_process({"env", "LC_ALL=C", "sudo", "-S", "-p", "", kHelper, "set", key, value},
                                     password_line);
    const std::string line = last_line(r.output);
    switch (power::classify_sudo(r.exit_code, r.output)) {
    case power::SudoOutcome::Ok: {
        const auto kv = power::parse_key_values(line);
        const auto it = kv.find(key);
        result.value = it != kv.end() ? it->second : value;
        result.ok = result.value == value;
        if (!result.ok) {
            result.message = "Now: " + power::option_label(*info, result.value);
        }
        break;
    }
    case power::SudoOutcome::NeedsPassword:
        result.needs_password = true;
        result.message = "Password required";
        break;
    case power::SudoOutcome::WrongPassword:
        result.wrong_password = true;
        result.message = "Wrong password";
        break;
    case power::SudoOutcome::NotAllowed:
        result.message = "This user can't use sudo";
        break;
    case power::SudoOutcome::Failed:
        result.message = line.empty() ? "Failed" : line;
        break;
    }
    return result;
}

} // namespace platform
