/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "power_control.h"
#include "power_settings.h"
#include "sudo_password.h"
#include "theme.h"

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

/**
 * Una lista de ajustes (hardware, servicios o ahorro). F/X elige fila. Los de sí/no son un
 * interruptor (como en iOS) que ENTER, Z o C cambian y aplican al momento; los de varias
 * opciones se eligen con Z/C (queda pendiente, marcado) y ENTER los aplica. Leer el estado y aplicar se hace en otro hilo: el
 * ayudante tarda unas décimas y la pantalla no se queda congelada.
 *
 * Si sudo pide contraseña, se pide encima de la lista y el cambio se repite con ella. Sudo
 * la recuerda unos minutos: los cambios siguientes ya no la piden.
 */
class SettingsScreen : public BaseScreen {
public:
    SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, power::Group group);
    ~SettingsScreen() override;

protected:
    void build_content(lv_obj_t* content) override;

private:
    // Filas que se ven a la vez; con más, la lista se desplaza (con su barra a la derecha).
    static constexpr int kMaxRows = 5;

    bool handle_key(uint32_t key);
    bool handle_password_key(uint32_t key);
    void start_job(std::function<void()> job);
    void apply_value(const power::SettingInfo& info, const std::string& target);
    void run_apply(const std::string& key, const std::string& target, std::string password_line);
    void open_password_prompt(bool wrong);
    void close_password_prompt();
    void refresh();
    void refresh_password_prompt(const view::ThemePalette& colors);

    static void timer_cb(lv_timer_t* timer);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    power::Group group_;
    std::vector<const power::SettingInfo*> settings_;
    platform::PowerControl control_;

    int row_ = 0;
    int first_ = 0; // primera fila que se ve
    std::map<std::string, std::string> pending_; // valor elegido y aún sin aplicar
    std::string confirm_key_;                    // segundo ENTER pendiente (acceso remoto)
    std::string applying_key_;                   // el interruptor ya se ve en su nuevo valor
    std::string applying_value_;

    // Lo que comparte el hilo de trabajo con la pantalla.
    std::mutex mutex_;
    std::map<std::string, std::string> values_; // estado real
    bool loaded_ = false;
    std::string message_;
    bool message_error_ = false;
    bool password_wanted_ = false; // el último cambio necesita contraseña
    bool password_wrong_ = false;  // ... porque la que se dio no valía
    std::string password_key_;     // el cambio que hay que repetir con ella
    std::string password_value_;
    std::atomic<bool> busy_{false};
    std::thread worker_;

    // La pantalla de la contraseña (sólo en el hilo de LVGL).
    power::SecretInput password_;
    bool prompt_open_ = false;
    bool prompt_checking_ = false; // sudo la está comprobando
    bool prompt_wrong_ = false;
    std::string prompt_key_;
    std::string prompt_value_;
    lv_obj_t* prompt_ = nullptr;
    lv_obj_t* prompt_title_ = nullptr;
    lv_obj_t* prompt_text_ = nullptr;
    lv_obj_t* prompt_field_ = nullptr;
    lv_obj_t* prompt_dots_ = nullptr;
    lv_obj_t* prompt_error_ = nullptr;
    lv_obj_t* prompt_footer_ = nullptr;

    std::array<lv_obj_t*, kMaxRows> row_boxes_{};
    std::array<lv_obj_t*, kMaxRows> row_titles_{};
    std::array<lv_obj_t*, kMaxRows> row_values_{};
    std::array<lv_obj_t*, kMaxRows> row_switches_{};
    lv_obj_t* scroll_track_ = nullptr;
    lv_obj_t* scroll_thumb_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_timer_t* timer_ = nullptr;
};

} // namespace screen
