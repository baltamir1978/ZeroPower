/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace power {

// --- Batería ---

// Media móvil del consumo: `current_now` del bq27220 salta mucho de un segundo a otro (la
// CPU, la wifi), y una cifra quieta es la que permite comparar un ajuste con otro.
class CurrentAverage {
public:
    explicit CurrentAverage(std::size_t window = 30) : window_(window) {}
    void add(double milliamps);
    void clear() {
        samples_.clear();
    }
    bool empty() const {
        return samples_.empty();
    }
    std::size_t count() const {
        return samples_.size();
    }
    double value() const; // 0 si no hay muestras

private:
    std::size_t window_;
    std::deque<double> samples_;
};

// Horas que quedan con `charge_mah` al consumo `draw_ma` (positivo = gasta). -1 si no se
// puede saber (cargando, o consumo casi nulo).
double hours_left(double charge_mah, double draw_ma);

// "4 h 35 min", "52 min", "—".
std::string format_hours(double hours);

// --- Batería baja ---

// Apaga cuando, descargando, la carga está en el umbral o por debajo en `confirmations`
// lecturas seguidas: una lectura suelta baja (el medidor se recalibra, un pico) no apaga.
class LowBatteryGuard {
public:
    explicit LowBatteryGuard(int confirmations = 3) : confirmations_(confirmations) {}
    // Devuelve true cuando toca apagar.
    bool update(bool discharging, int capacity_percent, int threshold_percent);
    int strikes() const {
        return strikes_;
    }

private:
    int confirmations_;
    int strikes_ = 0;
};

// --- Pantalla apagada ---

// Qué hacer con el lanzador según la pantalla (brillo) y el teclado. Con la pantalla a 0
// durante `delay_s` se entra en reposo (pausar el lanzador, aparcar su audio); una tecla o
// la pantalla encendida despiertan.
class IdleTracker {
public:
    enum class Action { None, Sleep, Wake };

    explicit IdleTracker(double delay_s = 5.0) : delay_s_(delay_s) {}
    // `now_s`: reloj monótono en segundos.
    Action update(double now_s, int brightness, bool key_pressed, bool enabled);
    bool sleeping() const {
        return sleeping_;
    }
    void set_delay(double delay_s) {
        delay_s_ = delay_s;
    }

private:
    double delay_s_;
    bool sleeping_ = false;
    bool dark_ = false;
    double dark_since_ = 0.0;
};

// --- HDMI desconectado ---

// Cuenta cuánto lleva desconectado el HDMI y avisa una vez al pasar `minutes` (0 = nunca).
// Un cable flojo que vuelve antes de tiempo pone la cuenta a cero.
class UnplugTimer {
public:
    // true: es el momento de cerrar el escritorio.
    bool update(double now_s, bool connected, int minutes);

private:
    bool counting_ = false;
    bool fired_ = false;
    double since_ = 0.0;
};

// --- Tiempo de pantalla ---

// Red de seguridad: la pantalla la apaga el lanzador (su salvapantallas), pero si no lo hace
// (una app abierta, o el lanzador se queda despierto) se quedaría encendida hasta agotar la
// batería. Con la pantalla encendida y `timeout_s` sin teclas, la apaga el servicio; la
// siguiente tecla le devuelve el brillo que tenía. Si otro la enciende mientras tanto (una
// app, el lanzador), deja de ser cosa nuestra y se vuelve a contar desde ahí.
class ScreenTimeout {
public:
    enum class Action { None, TurnOff, TurnOn };

    // `brightness` < 0: no hay pantalla que mirar. `timeout_s` <= 0: desactivado.
    Action update(double now_s, int brightness, bool key_pressed, double timeout_s);
    bool forced_off() const {
        return forced_;
    }
    // El brillo que había al apagarla (para TurnOn y para dejarlo como estaba al parar).
    int saved_brightness() const {
        return saved_;
    }

private:
    bool started_ = false;
    bool forced_ = false;
    int saved_ = 0;
    int last_brightness_ = -1;
    double last_activity_ = 0.0;
};

/**
 * Posición del interruptor USB del lateral (Host/Slave). En Host está encendido el hub USB
 * interno (y con él el chip Ethernet, el USB-A, el USB-C de salida y el USB de los Cap); en
 * Slave el hub se desconecta y el controlador del CM0 se queda sin nada que atender: gasta
 * unos 90 mA menos (medido: 286 → 198 mA con la pantalla apagada). No hay un GPIO que lo diga,
 * pero se nota en el bus: en Host hay al menos un aparato que no es un root hub.
 */
enum class UsbSwitch { Unknown, Host, Slave };

/**
 * `vendors`: idVendor de cada aparato de /sys/bus/usb/devices (los root hubs son 1d6b).
 * `bus_present`: si hay bus USB (en el simulador o en otro aparato puede no haberlo).
 */
UsbSwitch usb_switch_state(bool bus_present, const std::vector<std::string>& vendors);

} // namespace power
