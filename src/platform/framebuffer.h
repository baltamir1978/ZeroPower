/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

// Escribir una imagen ya hecha en el framebuffer de la pantalla (/dev/fb0), sin LVGL: lo usa
// zeropowerd para avisar antes de apagar por batería baja. El lanzador pinta en el mismo
// framebuffer, así que hay que pausarlo antes o lo tapará en su siguiente refresco.
class FramebufferImage {
public:
    // `image`: fichero RGB565 en crudo del tamaño exacto de la pantalla.
    // Guarda lo que había para poder devolverlo con restore(). false si no se puede (sin
    // fichero, tamaño distinto o sin permiso: hace falta el grupo `video`).
    bool show(const std::string& image, const std::string& device = "/dev/fb0");
    // Vuelve a poner lo que había antes de show().
    void restore();
    bool shown() const {
        return !saved_.empty();
    }

private:
    std::string device_;
    std::vector<std::uint8_t> saved_;
};

} // namespace platform
