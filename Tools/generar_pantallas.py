#!/usr/bin/env python3
"""Dibuja la pantalla que zeropowerd pone antes de apagar por batería baja, ya en el formato
del framebuffer del Cardputer Zero (320×170, RGB565 little-endian, sin cabecera), para que el
servicio sólo tenga que copiarla a /dev/fb0 sin LVGL ni fuentes. Necesita Pillow y la fuente
DejaVu Sans (fonts-dejavu-core).

    Tools/generar_pantallas.py

Escribe assets/screens/low-battery.rgb565 (lo que se instala) y low-battery.png (para verla).
"""

import os

from PIL import Image, ImageDraw, ImageFont

ANCHO, ALTO = 320, 170
FUENTES = "/usr/share/fonts/truetype/dejavu"
RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def fuente(nombre, tamano):
    return ImageFont.truetype(os.path.join(FUENTES, nombre), tamano)


def pantalla_bateria_baja():
    img = Image.new("RGB", (ANCHO, ALTO), (12, 12, 14))
    d = ImageDraw.Draw(img)
    rojo, blanco, gris = (235, 64, 52), (240, 240, 240), (150, 150, 155)

    # Batería vacía con una rayita roja: se entiende sin leer.
    x, y, w, h = 24, 46, 74, 38
    d.rounded_rectangle([x, y, x + w, y + h], radius=6, outline=blanco, width=4)
    d.rounded_rectangle([x + w + 2, y + 11, x + w + 9, y + h - 11], radius=2, fill=blanco)
    d.rectangle([x + 8, y + 8, x + 16, y + h - 8], fill=rojo)

    d.text((118, 40), "Battery empty", font=fuente("DejaVuSans-Bold.ttf", 22), fill=blanco)
    d.text((118, 70), "Switching off…", font=fuente("DejaVuSans.ttf", 17), fill=rojo)

    pequena = fuente("DejaVuSans.ttf", 14)
    d.text((24, 106), "Plug in the charger now to keep it on.", font=pequena, fill=gris)
    d.text((24, 125), "Once off, charge it and press the power", font=pequena, fill=gris)
    d.text((24, 143), "button to turn it back on.", font=pequena, fill=gris)
    return img


def a_rgb565(img):
    datos = bytearray()
    for r, g, b in img.getdata():
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        datos += v.to_bytes(2, "little")
    return bytes(datos)


def main():
    carpeta = os.path.join(RAIZ, "assets", "screens")
    os.makedirs(carpeta, exist_ok=True)
    img = pantalla_bateria_baja()
    img.save(os.path.join(carpeta, "low-battery.png"))
    with open(os.path.join(carpeta, "low-battery.rgb565"), "wb") as f:
        f.write(a_rgb565(img))


if __name__ == "__main__":
    main()
