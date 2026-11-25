/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace power {

// Qué ha pasado con una orden lanzada con sudo, según su código de salida y lo que dice
// (sudo se lanza con LC_ALL=C: los mensajes llegan en inglés).
enum class SudoOutcome {
    Ok,            // la orden se ejecutó y acabó bien
    NeedsPassword, // `sudo -n` sin contraseña guardada: hay que pedirla
    WrongPassword, // la contraseña no vale
    NotAllowed,    // el usuario no puede usar sudo para esto
    Failed,        // la orden se ejecutó y falló, o sudo falló por otra cosa
};

SudoOutcome classify_sudo(int exit_code, const std::string& output);

// Borra el contenido de verdad (el compilador no puede quitarse la escritura de encima) y
// deja la cadena vacía.
void wipe(std::string& text);

// Lo que se teclea en la pantalla de la contraseña. Reserva la memoria de una vez, para
// que al crecer no queden copias de la contraseña por el montón, y la borra al limpiar.
class SecretInput {
public:
    static constexpr std::size_t kMaxLength = 128;

    SecretInput();
    ~SecretInput();
    SecretInput(const SecretInput&) = delete;
    SecretInput& operator=(const SecretInput&) = delete;

    bool add(std::uint32_t ch); // sólo ASCII imprimible; false si no cabe o no vale
    bool backspace();
    void clear();

    std::size_t size() const {
        return text_.size();
    }
    bool empty() const {
        return text_.empty();
    }
    // Un punto por carácter, para enseñarla sin enseñarla. Si son más de `max_dots`, sólo
    // los últimos, detrás de «…».
    std::string masked(std::size_t max_dots = kMaxLength) const;
    // Lo tecleado seguido de un salto de línea (lo que espera `sudo -S`), y se borra de aquí.
    // Quien lo recibe tiene que borrarlo con wipe() al acabar.
    std::string take_line();

private:
    std::string text_;
};

} // namespace power
