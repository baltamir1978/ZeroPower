/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <map>
#include <string>

namespace platform {

/**
 * Lee y cambia los ajustes de ZeroPower:
 * - los del sistema, con el ayudante (`zeropower-helper status` sin root; `sudo -n
 *   zeropower-helper set` para cambiar),
 * - los de zeropowerd, en ~/.config/zeropower/power.conf (el servicio lo relee solo).
 *
 * Sin el ayudante instalado (el simulador de escritorio) simula el sistema en memoria.
 * Las llamadas bloquean (el ayudante tarda unas décimas): la pantalla las hace en otro hilo.
 */
class PowerControl {
public:
    PowerControl();

    bool simulated() const {
        return simulated_;
    }

    // Todos los valores actuales, por clave (incluye "gpu_mem_active" si se sabe).
    std::map<std::string, std::string> read_all();

    struct Result {
        bool ok = false;
        std::string value;    // el valor que queda puesto
        std::string message;  // por qué falló
        bool needs_password = false; // sudo pide la contraseña: hay que preguntarla y repetir
        bool wrong_password = false; // la que se le dio no vale
    };
    // Sin `password_line`, prueba `sudo -n` (sin contraseña, o con la que sudo recuerda de
    // hace poco). Con ella (la contraseña y un salto de línea) se la pasa a `sudo -S` por la
    // entrada estándar, y la borra al acabar. Nunca se guarda ni va en la línea de órdenes.
    Result apply(const std::string& key, const std::string& value, std::string password_line = "");

private:
    std::map<std::string, std::string> read_daemon_values() const;

    bool simulated_ = false;
    std::map<std::string, std::string> fake_system_;
    // Para probar la pantalla de la contraseña en el simulador: ZEROPOWER_SIMULATE_SUDO=<clave>
    // hace que el sistema simulado la pida (una vez, como sudo, que luego la recuerda).
    std::string fake_sudo_password_;
    bool fake_sudo_ok_ = true;
};

} // namespace platform
