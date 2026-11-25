/*
 * SPDX-License-Identifier: MIT
 */

#include "power_logic.h"

#include <cmath>
#include <cstdio>

namespace power {

void CurrentAverage::add(double milliamps) {
    samples_.push_back(milliamps);
    while (samples_.size() > window_) {
        samples_.pop_front();
    }
}

double CurrentAverage::value() const {
    if (samples_.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (double s : samples_) {
        sum += s;
    }
    return sum / static_cast<double>(samples_.size());
}

double hours_left(double charge_mah, double draw_ma) {
    if (draw_ma < 5.0 || charge_mah <= 0.0) {
        return -1.0;
    }
    return charge_mah / draw_ma;
}

std::string format_hours(double hours) {
    if (hours < 0.0) {
        return "—";
    }
    const long minutes = std::lround(hours * 60.0);
    char buf[32];
    if (minutes < 60) {
        std::snprintf(buf, sizeof(buf), "%ld min", minutes);
    } else {
        std::snprintf(buf, sizeof(buf), "%ld h %02ld min", minutes / 60, minutes % 60);
    }
    return buf;
}

bool LowBatteryGuard::update(bool discharging, int capacity_percent, int threshold_percent) {
    if (threshold_percent <= 0 || !discharging || capacity_percent < 0 ||
        capacity_percent > threshold_percent) {
        strikes_ = 0;
        return false;
    }
    ++strikes_;
    return strikes_ >= confirmations_;
}

IdleTracker::Action IdleTracker::update(double now_s, int brightness, bool key_pressed, bool enabled) {
    if (!enabled) {
        dark_ = false;
        if (sleeping_) {
            sleeping_ = false;
            return Action::Wake; // se ha apagado la opción en reposo: despertar
        }
        return Action::None;
    }
    if (key_pressed || brightness > 0) {
        dark_ = false;
        if (sleeping_) {
            sleeping_ = false;
            return Action::Wake;
        }
        return Action::None;
    }
    // Pantalla apagada y sin teclas.
    if (!dark_) {
        dark_ = true;
        dark_since_ = now_s;
    }
    if (!sleeping_ && now_s - dark_since_ >= delay_s_) {
        sleeping_ = true;
        return Action::Sleep;
    }
    return Action::None;
}

bool UnplugTimer::update(double now_s, bool connected, int minutes) {
    if (connected || minutes <= 0) {
        counting_ = false;
        fired_ = false;
        return false;
    }
    if (fired_) {
        return false; // una vez por desconexión
    }
    if (!counting_) {
        counting_ = true;
        since_ = now_s;
        return false;
    }
    if (now_s - since_ >= minutes * 60.0) {
        fired_ = true;
        return true;
    }
    return false;
}

ScreenTimeout::Action ScreenTimeout::update(double now_s, int brightness, bool key_pressed,
                                           double timeout_s) {
    if (!started_) {
        started_ = true;
        last_activity_ = now_s;
        last_brightness_ = brightness;
    }
    // Encenderse la pantalla (de 0 a algo) cuenta como actividad: si no, una app que la
    // enciende sola tras un rato a oscuras se apagaría al instante.
    const bool lit_up = brightness > 0 && last_brightness_ == 0;
    last_brightness_ = brightness;

    if (forced_) {
        if (key_pressed || timeout_s <= 0) {
            forced_ = false;
            last_activity_ = now_s;
            return Action::TurnOn;
        }
        if (brightness > 0) {
            // La ha encendido otro: ya no es nuestra.
            forced_ = false;
            last_activity_ = now_s;
        }
        return Action::None;
    }
    if (key_pressed || lit_up) {
        last_activity_ = now_s;
        return Action::None;
    }
    if (timeout_s <= 0 || brightness <= 0) {
        return Action::None;
    }
    if (now_s - last_activity_ >= timeout_s) {
        forced_ = true;
        saved_ = brightness;
        return Action::TurnOff;
    }
    return Action::None;
}

UsbSwitch usb_switch_state(bool bus_present, const std::vector<std::string>& vendors) {
    if (!bus_present) {
        return UsbSwitch::Unknown;
    }
    for (const auto& vendor : vendors) {
        if (vendor != "1d6b") { // cualquier cosa que no sea un root hub de Linux: el hub interno
            return UsbSwitch::Host;
        }
    }
    return UsbSwitch::Slave;
}

} // namespace power
