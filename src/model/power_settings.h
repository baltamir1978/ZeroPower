/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <map>
#include <string>
#include <vector>

namespace power {

// Las tres listas de ajustes del menú.
enum class Group { Hardware, Services, Saving };

// Quién aplica un ajuste:
// - System: el ayudante con root (`zeropower-helper set`), que lo deja puesto en el sistema
//   (servicios, regla de udev, config.txt…). El estado real se lee con `zeropower-helper status`.
// - Daemon: el servicio de usuario `zeropowerd`, que lo lee de ~/.config/zeropower/power.conf.
enum class Applier { System, Daemon };

struct Option {
    const char* value; // lo que se guarda / se pasa al ayudante
    const char* label; // lo que se ve
};

struct SettingInfo {
    const char* key;   // "ethernet", "bluetooth"…: el mismo en el ayudante y en power.conf
    Group group;
    Applier applier;
    const char* title; // en la lista
    const char* hint;  // una línea: qué hace y cuánto ahorra
    std::vector<Option> options;
    const char* default_value; // el de un sistema recién instalado
    bool needs_reboot = false;
    bool warn_remote = false; // al apagarlo se puede perder el acceso remoto (Tailscale, Wi-Fi)
};

// Todos los ajustes, en el orden en que salen.
const std::vector<SettingInfo>& all_settings();
std::vector<const SettingInfo*> settings_in(Group group);
const SettingInfo* find_setting(const std::string& key);

// Índice de `value` entre las opciones (o -1) y el valor `delta` posiciones más allá (con vuelta).
int option_index(const SettingInfo& info, const std::string& value);
std::string step_option(const SettingInfo& info, const std::string& value, int delta);
// Etiqueta de un valor ("Off", "Auto-suspend"…); el propio valor si no es una opción.
std::string option_label(const SettingInfo& info, const std::string& value);

// Líneas "clave=valor" (de `zeropower-helper status` o de power.conf). Se ignoran líneas
// vacías, comentarios (#) y basura. Los espacios alrededor se quitan.
std::map<std::string, std::string> parse_key_values(const std::string& text);

// Opciones del servicio, con sus valores por defecto si faltan o no son válidos.
struct DaemonConfig {
    bool quiet_launcher = false;  // el audio del lanzador siempre aparcado (sin sonido de teclas)
    // Con la pantalla apagada: su audio aparcado (la tarjeta de sonido duerme) y el lanzador
    // en pausa (idle_audio, idle_launcher; antes, los dos juntos en idle_saver).
    bool idle_audio = true;
    bool idle_launcher = true;
    int idle_delay_s = 5;         // segundos con la pantalla apagada antes de lo anterior
    int low_battery_percent = 0;  // 0 = no apagar
    // Con Desk Mode (cz-desk-mode): cerrar su escritorio HDMI tras estos minutos con el HDMI
    // desconectado (0 = nunca). Lo que no se haya guardado en él se pierde.
    int desktop_unplug_min = 0;
    // Segundos sin tocar una tecla con la pantalla encendida para apagarla el propio servicio
    // (0 = nunca). Es la red de seguridad por si el lanzador o una app la dejan encendida.
    int screen_timeout_s = 300;
};
// Desk Mode (github.com/InstantComet/cz-desk-mode): su escritorio HDMI es esta unidad de
// usuario.
constexpr const char* kDeskModeDir = "/usr/lib/cz-desk-mode";
constexpr const char* kDeskModeUnit = "cz-desk-mode-desktop.service";

DaemonConfig daemon_config_from(const std::map<std::string, std::string>& values);
std::string daemon_config_text(const DaemonConfig& config);

} // namespace power
