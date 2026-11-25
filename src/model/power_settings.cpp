/*
 * SPDX-License-Identifier: MIT
 */

#include "power_settings.h"

#include <cstdlib>
#include <sstream>

namespace power {
namespace {

const std::vector<Option> kOnOff = {{"on", "On"}, {"off", "Off"}};

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return "";
    }
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

const std::vector<SettingInfo>& all_settings() {
    // Los ahorros que se citan son los medidos con el medidor de la batería (bq27220) en un
    // Cardputer Zero: los de cz-battery-saver (github.com/InstantComet/cz-battery-saver) y
    // los nuestros. Cada aparato y cada uso dan algo distinto: la pantalla Battery lo mide.
    static const std::vector<SettingInfo> kSettings = {
        // --- Hardware ---
        {"ethernet", Group::Hardware, Applier::System, "USB Ethernet",
         "Internal USB LAN chip. Auto-suspend saves ~100 mA; Off disables it.",
         {{"on", "On"}, {"auto", "Auto-suspend"}, {"off", "Off"}}, "on"},
        {"wifi", Group::Hardware, Applier::System, "Wi-Fi",
         "Off saves ~40 mA, but no network, no store, no remote access.", kOnOff, "on", false,
         true},
        {"bluetooth", Group::Hardware, Applier::System, "Bluetooth",
         "Radio and service off. Small saving, frees some RAM.", kOnOff, "on"},
        {"hdmi_desktop", Group::Hardware, Applier::System, "HDMI desktop",
         "Desktop for an HDMI screen (lightdm). Off frees ~25 MB RAM and CPU.", kOnOff, "on"},
        {"desktop_unplug", Group::Hardware, Applier::Daemon, "Desk Mode unplug",
         "Close the Desk Mode desktop after HDMI is unplugged this long. Unsaved work is lost.",
         {{"0", "Keep desktop"}, {"1", "1 min"}, {"5", "5 min"}, {"15", "15 min"}, {"30", "30 min"}}, "0"},
        {"gpu_mem", Group::Hardware, Applier::System, "GPU memory",
         "RAM kept for the GPU firmware. 16 MB is enough for the built-in screen.",
         {{"16", "16 MB"}, {"32", "32 MB"}, {"64", "64 MB"}, {"128", "128 MB"}}, "64", true},
        {"cpu_max", Group::Hardware, Applier::System, "CPU max speed",
         "Caps the CPU clock: apps get slower, busy moments draw less. Idle: ~5 mA at 600 MHz.",
         {{"1000", "1000 MHz"}, {"800", "800 MHz"}, {"600", "600 MHz"}}, "1000"},
        {"status_led", Group::Hardware, Applier::System, "Status LED",
         "The green activity LED of the CM0 module (~4 mA).", kOnOff, "on"},
        {"grove_5v", Group::Hardware, Applier::System, "Grove 5V",
         "5 V on the Grove port, for sensors and modules. ~3 mA with nothing plugged in.", kOnOff, "off"},
        {"gpio_5v", Group::Hardware, Applier::System, "GPIO 5V",
         "5 V on the GPIO header for Caps (~3 mA idle). Off cuts power to a plugged-in Cap.", kOnOff,
         "off"},
        // --- Services ---
        {"tailscale", Group::Services, Applier::System, "Tailscale",
         "VPN daemon. Off saves CPU and RAM, but no remote access through it.", kOnOff, "on",
         false, true},
        {"nfs", Group::Services, Applier::System, "NFS client",
         "rpcbind and nfs-blkmap, for network file shares. Unused on a handheld.", kOnOff, "on"},
        {"avahi", Group::Services, Applier::System, "Avahi (.local)",
         "Finds the device as hostname.local on the network. Off: use its IP address.", kOnOff,
         "on"},
        {"apt_timers", Group::Services, Applier::System, "Daily apt updates",
         "Background package list refresh and upgrades. apt by hand still works.", kOnOff, "on"},
        {"man_db", Group::Services, Applier::System, "Man page index",
         "Rebuilds the manual page index every day.", kOnOff, "on"},
        {"e2scrub", Group::Services, Applier::System, "Disk scrub timer",
         "Weekly check of ext4 on LVM volumes: the SD card has none.", kOnOff, "on"},
        {"journal_ram", Group::Services, Applier::System, "Logs in RAM",
         "System journal in RAM (16 MB): fewer SD card writes. Lost on reboot.", kOnOff, "off"},
        // --- Power saving (zeropowerd) ---
        {"idle_audio", Group::Saving, Applier::Daemon, "Sleep sound card",
         "Screen off: park the launcher's audio so the sound card sleeps (~40 mA).", kOnOff, "on"},
        {"idle_launcher", Group::Saving, Applier::Daemon, "Pause launcher",
         "Screen off: pause the launcher, which keeps redrawing (~30 mA). A key wakes it.", kOnOff,
         "on"},
        {"idle_delay", Group::Saving, Applier::Daemon, "Saver delay",
         "How long the screen must be off before the two above start.",
         {{"3", "3 s"}, {"5", "5 s"}, {"10", "10 s"}, {"30", "30 s"}, {"60", "60 s"}}, "5"},
        {"quiet_launcher", Group::Saving, Applier::Daemon, "Quiet launcher",
         "No launcher key sounds, so the audio amp can sleep (~35 mA).", kOnOff, "off"},
        {"low_battery", Group::Saving, Applier::Daemon, "Low battery off",
         "Power off cleanly when the battery drops to this level.",
         {{"0", "Never"}, {"5", "5 %"}, {"8", "8 %"}, {"10", "10 %"}, {"15", "15 %"}}, "0"},
        {"screen_timeout", Group::Saving, Applier::Daemon, "Screen timeout",
         "Screen off after this long without a key, even inside apps (~150 mA).",
         {{"0", "Never"}, {"60", "1 min"}, {"120", "2 min"}, {"300", "5 min"}, {"600", "10 min"}},
         "300"},
    };
    return kSettings;
}

std::vector<const SettingInfo*> settings_in(Group group) {
    std::vector<const SettingInfo*> out;
    for (const auto& info : all_settings()) {
        if (info.group == group) {
            out.push_back(&info);
        }
    }
    return out;
}

const SettingInfo* find_setting(const std::string& key) {
    for (const auto& info : all_settings()) {
        if (key == info.key) {
            return &info;
        }
    }
    return nullptr;
}

int option_index(const SettingInfo& info, const std::string& value) {
    for (std::size_t i = 0; i < info.options.size(); ++i) {
        if (value == info.options[i].value) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string step_option(const SettingInfo& info, const std::string& value, int delta) {
    const int n = static_cast<int>(info.options.size());
    if (n == 0) {
        return value;
    }
    int index = option_index(info, value);
    if (index < 0) {
        // Un valor que no está entre las opciones (p. ej. gpu_mem=48 puesto a mano): se
        // empieza por la primera.
        return info.options[delta >= 0 ? 0 : n - 1].value;
    }
    index = ((index + delta) % n + n) % n;
    return info.options[static_cast<std::size_t>(index)].value;
}

std::string option_label(const SettingInfo& info, const std::string& value) {
    const int index = option_index(info, value);
    return index >= 0 ? info.options[static_cast<std::size_t>(index)].label : value;
}

std::map<std::string, std::string> parse_key_values(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string clean = trim(line);
        if (clean.empty() || clean[0] == '#') {
            continue;
        }
        const auto eq = clean.find('=');
        if (eq == std::string::npos || eq == 0) {
            continue;
        }
        out[trim(clean.substr(0, eq))] = trim(clean.substr(eq + 1));
    }
    return out;
}

DaemonConfig daemon_config_from(const std::map<std::string, std::string>& values) {
    DaemonConfig config;
    auto flag = [&](const char* key, bool& out) {
        const auto it = values.find(key);
        if (it != values.end() && (it->second == "on" || it->second == "off")) {
            out = it->second == "on";
        }
    };
    flag("quiet_launcher", config.quiet_launcher);
    // Hasta 0.1.3 era un solo ajuste, idle_saver=on|off: vale mientras no estén los nuevos.
    const auto idle = values.find("idle_saver");
    if (idle != values.end() && (idle->second == "on" || idle->second == "off")) {
        config.idle_audio = config.idle_launcher = idle->second == "on";
    }
    flag("idle_audio", config.idle_audio);
    flag("idle_launcher", config.idle_launcher);
    auto number = [&](const char* key, int& out) {
        const auto it = values.find(key);
        const SettingInfo* info = find_setting(key);
        if (it != values.end() && info && option_index(*info, it->second) >= 0) {
            out = std::atoi(it->second.c_str());
        }
    };
    number("idle_delay", config.idle_delay_s);
    number("low_battery", config.low_battery_percent);
    number("screen_timeout", config.screen_timeout_s);
    number("desktop_unplug", config.desktop_unplug_min);
    return config;
}

std::string daemon_config_text(const DaemonConfig& config) {
    std::ostringstream out;
    out << "# ZeroPower: options for zeropowerd (changed from the app)\n";
    out << "idle_audio=" << (config.idle_audio ? "on" : "off") << "\n";
    out << "idle_launcher=" << (config.idle_launcher ? "on" : "off") << "\n";
    out << "idle_delay=" << config.idle_delay_s << "\n";
    out << "quiet_launcher=" << (config.quiet_launcher ? "on" : "off") << "\n";
    out << "low_battery=" << config.low_battery_percent << "\n";
    out << "screen_timeout=" << config.screen_timeout_s << "\n";
    out << "desktop_unplug=" << config.desktop_unplug_min << "\n";
    return out.str();
}

} // namespace power
