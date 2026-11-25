/*
 * SPDX-License-Identifier: MIT
 */

#include "settings_screen.h"

#include "asset_manager.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <pwd.h>
#include <unistd.h>

namespace screen {
namespace {

constexpr int32_t kW = view::kScreenWidth;
constexpr int32_t kMargin = 6;
constexpr int32_t kRowH = 17;
// Barra de desplazamiento, a la derecha de las filas, cuando no caben todas.
constexpr int32_t kBarW = 3;
constexpr int32_t kBarGap = 3;
constexpr int32_t kTop = 3;

const lv_color_t kWarnColor = lv_color_hex(0xF9A825);
const lv_color_t kErrorColor = lv_color_hex(0xE53935);

// Un ajuste de sí/no se pinta como interruptor.
bool is_toggle(const power::SettingInfo& info) {
    return info.options.size() == 2 && std::strcmp(info.options[0].value, "on") == 0 &&
           std::strcmp(info.options[1].value, "off") == 0;
}

// Verde de «encendido» de iOS: se reconoce al momento, en tema claro y oscuro.
const lv_color_t kSwitchOn = lv_color_hex(0x34C759);

void set_text(lv_obj_t* label, const std::string& text) {
    const char* current = lv_label_get_text(label);
    if (!current || text != current) {
        lv_label_set_text(label, text.c_str());
    }
}

// La contraseña que pide sudo es la del usuario con el que corre la app.
std::string user_name() {
    if (const passwd* pw = ::getpwuid(::geteuid())) {
        return pw->pw_name;
    }
    const char* user = std::getenv("USER");
    return user ? user : "this user";
}

// Los puntos que caben en la casilla de la contraseña.
constexpr std::size_t kMaxDots = 34;

} // namespace

SettingsScreen::SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets,
                               power::Group group)
    : BaseScreen(view_model, assets), group_(group), settings_(power::settings_in(group)) {
    // Sin Desk Mode instalado, su ajuste no pinta nada: ni se enseña.
    std::error_code ec;
    if (!std::filesystem::exists(power::kDeskModeDir, ec)) {
        settings_.erase(std::remove_if(settings_.begin(), settings_.end(),
                                       [](const power::SettingInfo* info) {
                                           return std::strcmp(info->key, "desktop_unplug") == 0;
                                       }),
                        settings_.end());
    }
    init();
    this->view_model().set_page_handlers(this, [this](uint32_t key) { return handle_key(key); },
                                         [this]() {
                                             // ESC en la contraseña: no se cambia nada (mientras
                                             // sudo la comprueba no se puede deshacer: espera).
                                             if (prompt_open_) {
                                                 if (!prompt_checking_) {
                                                     close_password_prompt();
                                                     std::lock_guard<std::mutex> lock(mutex_);
                                                     message_ = "Not changed";
                                                     message_error_ = false;
                                                 }
                                                 refresh();
                                                 return true;
                                             }
                                             // ESC con un valor elegido sin aplicar: se descarta
                                             // y se queda; otro ESC vuelve al menú.
                                             if (!pending_.empty() || !confirm_key_.empty()) {
                                                 pending_.clear();
                                                 confirm_key_.clear();
                                                 refresh();
                                                 return true;
                                             }
                                             return false;
                                         });
    start_job([this]() {
        auto values = control_.read_all();
        std::lock_guard<std::mutex> lock(mutex_);
        values_ = std::move(values);
        loaded_ = true;
    });
}

SettingsScreen::~SettingsScreen() {
    view_model().clear_page_handlers(this);
    if (timer_) {
        lv_timer_delete(timer_);
    }
    if (worker_.joinable()) {
        worker_.join(); // un cambio a medias se termina: no se deja el sistema a medias
    }
}

void SettingsScreen::build_content(lv_obj_t* content) {
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    const lv_font_t* title_font = assets().load_standard_font(13);
    const lv_font_t* value_font = assets().load_standard_font(13, app::StandardFontWeight::Regular);
    const lv_font_t* small = assets().load_standard_font(11, app::StandardFontWeight::Regular);

    const int n = static_cast<int>(settings_.size());
    const int visible = n < kMaxRows ? n : kMaxRows;
    const bool scrolls = n > kMaxRows;
    const int32_t row_w = kW - 2 * kMargin - (scrolls ? kBarW + kBarGap : 0);
    for (int i = 0; i < kMaxRows; ++i) {
        auto* box = lv_obj_create(content);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, row_w, kRowH);
        lv_obj_set_pos(box, kMargin, kTop + i * kRowH);
        lv_obj_set_style_radius(box, 3, 0);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        row_boxes_[i] = box;

        row_titles_[i] = lv_label_create(box);
        lv_obj_set_style_text_font(row_titles_[i], title_font ? title_font : &lv_font_montserrat_12, 0);
        lv_obj_align(row_titles_[i], LV_ALIGN_LEFT_MID, 5, 0);

        row_values_[i] = lv_label_create(box);
        lv_obj_set_style_text_font(row_values_[i], value_font ? value_font : &lv_font_montserrat_12, 0);
        lv_obj_align(row_values_[i], LV_ALIGN_RIGHT_MID, -5, 0);

        // Interruptor: pista gris o verde y botón blanco, sin animación (se repinta lo justo).
        auto* sw = lv_switch_create(box);
        lv_obj_set_size(sw, 28, 14);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -5, 0);
        lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_anim_duration(sw, 0, 0);
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x8E8E93), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, kSwitchOn, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_pad_all(sw, -2, LV_PART_KNOB);
        lv_obj_add_flag(sw, LV_OBJ_FLAG_HIDDEN);
        row_switches_[i] = sw;
    }

    if (scrolls) {
        scroll_track_ = lv_obj_create(content);
        lv_obj_remove_style_all(scroll_track_);
        lv_obj_set_size(scroll_track_, kBarW, visible * kRowH);
        lv_obj_set_pos(scroll_track_, kW - kMargin - kBarW, kTop);
        lv_obj_set_style_radius(scroll_track_, kBarW / 2 + 1, 0);
        lv_obj_set_style_bg_opa(scroll_track_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(scroll_track_, LV_OBJ_FLAG_SCROLLABLE);

        scroll_thumb_ = lv_obj_create(scroll_track_);
        lv_obj_remove_style_all(scroll_thumb_);
        lv_obj_set_size(scroll_thumb_, kBarW, visible * kRowH * kMaxRows / n);
        lv_obj_set_style_radius(scroll_thumb_, kBarW / 2 + 1, 0);
        lv_obj_set_style_bg_opa(scroll_thumb_, LV_OPA_COVER, 0);
    }

    hint_ = lv_label_create(content);
    lv_obj_set_style_text_font(hint_, small ? small : &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint_, kW - 2 * kMargin - 6);
    // Justo debajo de la última fila que se ve, con las líneas un poco juntas: con dos líneas
    // tiene que caber encima de la línea de estado.
    lv_obj_set_style_text_line_space(hint_, -2, 0);
    lv_obj_set_pos(hint_, kMargin + 3, kTop + visible * kRowH + 1);

    status_ = lv_label_create(content);
    lv_obj_set_style_text_font(status_, small ? small : &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(status_, kW - 2 * kMargin - 6);
    lv_obj_align(status_, LV_ALIGN_BOTTOM_LEFT, kMargin + 3, -2);

    // La contraseña: tapa la lista entera mientras se pide.
    prompt_ = lv_obj_create(content);
    lv_obj_remove_style_all(prompt_);
    lv_obj_set_size(prompt_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(prompt_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(prompt_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(prompt_, LV_OBJ_FLAG_HIDDEN);

    prompt_title_ = lv_label_create(prompt_);
    lv_obj_set_style_text_font(prompt_title_, title_font ? title_font : &lv_font_montserrat_12, 0);
    lv_obj_set_pos(prompt_title_, kMargin + 3, kTop + 2);
    lv_label_set_text(prompt_title_, "Password required");

    prompt_text_ = lv_label_create(prompt_);
    lv_obj_set_style_text_font(prompt_text_, small ? small : &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(prompt_text_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(prompt_text_, kW - 2 * kMargin - 6);
    lv_obj_set_pos(prompt_text_, kMargin + 3, kTop + 22);

    prompt_field_ = lv_obj_create(prompt_);
    lv_obj_remove_style_all(prompt_field_);
    lv_obj_set_size(prompt_field_, kW - 2 * kMargin, 22);
    lv_obj_set_pos(prompt_field_, kMargin, kTop + 56);
    lv_obj_set_style_radius(prompt_field_, 3, 0);
    lv_obj_set_style_border_width(prompt_field_, 1, 0);
    lv_obj_set_style_bg_opa(prompt_field_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(prompt_field_, LV_OBJ_FLAG_SCROLLABLE);

    prompt_dots_ = lv_label_create(prompt_field_);
    lv_obj_set_style_text_font(prompt_dots_, value_font ? value_font : &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(prompt_dots_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(prompt_dots_, kW - 2 * kMargin - 12);
    lv_obj_align(prompt_dots_, LV_ALIGN_LEFT_MID, 6, 0);

    prompt_error_ = lv_label_create(prompt_);
    lv_obj_set_style_text_font(prompt_error_, small ? small : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(prompt_error_, kErrorColor, 0);
    lv_obj_set_pos(prompt_error_, kMargin + 3, kTop + 82);

    prompt_footer_ = lv_label_create(prompt_);
    lv_obj_set_style_text_font(prompt_footer_, small ? small : &lv_font_montserrat_12, 0);
    lv_obj_align(prompt_footer_, LV_ALIGN_BOTTOM_LEFT, kMargin + 3, -2);

    lv_subject_add_observer_obj(view_model().dark_mode_subject(), theme_observer_cb, content, this);
    timer_ = lv_timer_create(timer_cb, 150, this);
    refresh();
}

void SettingsScreen::start_job(std::function<void()> job) {
    if (busy_) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    busy_ = true;
    worker_ = std::thread([this, job = std::move(job)]() {
        job();
        busy_ = false;
    });
}

bool SettingsScreen::handle_key(uint32_t key) {
    if (prompt_open_) {
        return handle_password_key(key);
    }
    if (settings_.empty()) {
        return false;
    }
    const int n = static_cast<int>(settings_.size());
    const power::SettingInfo& info = *settings_[static_cast<std::size_t>(row_)];
    std::string current;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = values_.find(info.key);
        current = it != values_.end() ? it->second : "";
    }
    const bool absent = current == "absent";

    switch (key) {
        case LV_KEY_UP:
        case 'f':
        case 'F':
            row_ = (row_ + n - 1) % n;
            confirm_key_.clear();
            break;
        case LV_KEY_DOWN:
        case 'x':
        case 'X':
            row_ = (row_ + 1) % n;
            confirm_key_.clear();
            break;
        case LV_KEY_LEFT:
        case 'z':
        case 'Z':
        case LV_KEY_RIGHT:
        case 'c':
        case 'C': {
            if (absent || busy_ || current.empty()) {
                break;
            }
            if (is_toggle(info)) {
                apply_value(info, current == "on" ? "off" : "on");
                break;
            }
            const int delta = (key == LV_KEY_LEFT || key == 'z' || key == 'Z') ? -1 : 1;
            const auto it = pending_.find(info.key);
            const std::string from = it != pending_.end() ? it->second : current;
            const std::string next = power::step_option(info, from, delta);
            if (next == current) {
                pending_.erase(info.key);
            } else {
                pending_[info.key] = next;
            }
            confirm_key_.clear();
            break;
        }
        case LV_KEY_ENTER:
        case '\r':
        case ' ': {
            if (absent || busy_ || current.empty()) {
                break;
            }
            // Sin valor elegido, ENTER pasa al siguiente (en un interruptor, lo cambia).
            const std::string target =
                pending_.count(info.key) ? pending_[info.key] : power::step_option(info, current, 1);
            apply_value(info, target);
            break;
        }
        default:
            return false;
    }
    refresh();
    return true;
}

void SettingsScreen::apply_value(const power::SettingInfo& info, const std::string& target) {
    const bool losing_remote = info.warn_remote && target == "off";
    if (losing_remote && confirm_key_ != info.key) {
        // Wi-Fi o Tailscale: el primer intento sólo avisa; el segundo apaga.
        if (!is_toggle(info)) {
            pending_[info.key] = target;
        }
        confirm_key_ = info.key;
        return;
    }
    confirm_key_.clear();
    pending_.erase(info.key);
    run_apply(info.key, target, "");
}

void SettingsScreen::run_apply(const std::string& key, const std::string& target, std::string password_line) {
    const power::SettingInfo* info = power::find_setting(key);
    if (!info || busy_) {
        power::wipe(password_line);
        return;
    }
    applying_key_ = key;
    applying_value_ = target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        message_ = "Applying " + std::string(info->title) + "…";
        message_error_ = false;
    }
    // La contraseña se mueve al hilo y PowerControl::apply la borra al acabar.
    start_job([this, key_name = key, target, line = std::move(password_line)]() mutable {
        const auto result = control_.apply(key_name, target, std::move(line));
        power::wipe(line);
        if (result.needs_password || result.wrong_password) {
            std::lock_guard<std::mutex> lock(mutex_);
            password_wanted_ = true;
            password_wrong_ = result.wrong_password;
            password_key_ = key_name;
            password_value_ = target;
            message_.clear();
            message_error_ = false;
            return;
        }
        auto values = control_.read_all();
        std::lock_guard<std::mutex> lock(mutex_);
        values_ = std::move(values);
        const power::SettingInfo* applied = power::find_setting(key_name);
        if (result.ok) {
            message_ = std::string(applied->title) + ": " + power::option_label(*applied, result.value);
            if (applied->needs_reboot) {
                message_ += " (restart to apply)";
            }
            message_error_ = false;
        } else {
            message_ = std::string(applied->title) + ": " + result.message;
            message_error_ = true;
        }
    });
}

bool SettingsScreen::handle_password_key(uint32_t key) {
    if (prompt_checking_) {
        return true; // sudo la está comprobando: se espera
    }
    switch (key) {
        case LV_KEY_ENTER:
        case '\r':
            // El espacio no: puede ir en la contraseña.
            if (!password_.empty() && !busy_) {
                prompt_checking_ = true;
                run_apply(prompt_key_, prompt_value_, password_.take_line());
            }
            break;
        case LV_KEY_BACKSPACE:
            password_.backspace();
            prompt_wrong_ = false;
            break;
        default:
            if (password_.add(key)) {
                prompt_wrong_ = false;
            }
            break;
    }
    refresh();
    return true; // con la contraseña abierta ninguna tecla hace otra cosa
}

void SettingsScreen::open_password_prompt(bool wrong) {
    password_.clear();
    prompt_open_ = true;
    prompt_checking_ = false;
    prompt_wrong_ = wrong;
    view_model().set_text_entry(true);
}

void SettingsScreen::close_password_prompt() {
    password_.clear();
    prompt_open_ = false;
    prompt_checking_ = false;
    prompt_wrong_ = false;
    prompt_key_.clear();
    prompt_value_.clear();
    view_model().set_text_entry(false);
}

void SettingsScreen::refresh_password_prompt(const view::ThemePalette& colors) {
    if (!prompt_open_) {
        lv_obj_add_flag(prompt_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(prompt_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(prompt_, colors.background, 0);
    lv_obj_set_style_text_color(prompt_title_, colors.text, 0);
    lv_obj_set_style_text_color(prompt_text_, colors.text_disabled, 0);
    lv_obj_set_style_text_color(prompt_footer_, prompt_checking_ ? colors.text_disabled : colors.text, 0);
    lv_obj_set_style_text_color(prompt_dots_, colors.text, 0);
    lv_obj_set_style_bg_color(prompt_field_, colors.surface, 0);
    lv_obj_set_style_border_color(prompt_field_, prompt_wrong_ ? kErrorColor : colors.primary, 0);

    const power::SettingInfo* info = power::find_setting(prompt_key_);
    const std::string what = info ? std::string(info->title) + " → " + power::option_label(*info, prompt_value_)
                                  : std::string("this setting");
    set_text(prompt_text_, "Changing " + what + " needs root. Enter the sudo password for " + user_name() + ".");
    set_text(prompt_dots_, password_.masked(kMaxDots) + (prompt_checking_ ? "" : "_"));
    set_text(prompt_error_, prompt_wrong_ ? "Wrong password, try again" : "");
    set_text(prompt_footer_, prompt_checking_ ? "Checking password…" : "ENTER confirm   ESC cancel");
}

void SettingsScreen::refresh() {
    if (!status_) {
        return;
    }
    const auto colors = view::palette(view_model().is_dark_mode());
    std::map<std::string, std::string> values;
    bool loaded;
    std::string message;
    bool message_error;
    bool password_wanted;
    bool password_wrong;
    std::string password_key;
    std::string password_value;
    // Antes que lo demás: el hilo deja su resultado y luego baja `busy_`, así que si aquí ya
    // no trabaja, lo que se lea a continuación es lo último.
    const bool busy = busy_;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        values = values_;
        loaded = loaded_;
        message = message_;
        message_error = message_error_;
        password_wanted = password_wanted_;
        password_wrong = password_wrong_;
        password_key = password_key_;
        password_value = password_value_;
        password_wanted_ = false;
    }

    if (password_wanted) {
        open_password_prompt(password_wrong);
        prompt_key_ = password_key;
        prompt_value_ = password_value;
    } else if (prompt_open_ && prompt_checking_ && !busy) {
        close_password_prompt(); // la aceptó: el resultado sale abajo, como siempre
    }
    refresh_password_prompt(colors);

    // Ventana de filas que se ven: la elegida siempre dentro.
    const int n = static_cast<int>(settings_.size());
    if (row_ < first_) {
        first_ = row_;
    } else if (row_ >= first_ + kMaxRows) {
        first_ = row_ - kMaxRows + 1;
    }
    if (scroll_track_) {
        lv_obj_set_style_bg_color(scroll_track_, colors.border, 0);
        lv_obj_set_style_bg_color(scroll_thumb_, colors.text_disabled, 0);
        const int32_t track_h = lv_obj_get_height(scroll_track_);
        const int32_t thumb_h = lv_obj_get_height(scroll_thumb_);
        lv_obj_set_y(scroll_thumb_, (track_h - thumb_h) * first_ / (n - kMaxRows));
    }

    for (int i = 0; i < kMaxRows; ++i) {
        const int index = first_ + i;
        if (index >= n) {
            lv_obj_add_flag(row_boxes_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const power::SettingInfo& info = *settings_[static_cast<std::size_t>(index)];
        const bool selected = index == row_;
        lv_obj_remove_flag(row_boxes_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(row_boxes_[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row_boxes_[i], colors.primary, 0);
        set_text(row_titles_[i], info.title);

        std::string text;
        const auto it = values.find(info.key);
        const auto pend = pending_.find(info.key);
        bool dim = false;
        const bool have_value = loaded && it != values.end() && it->second != "absent";
        if (is_toggle(info) && have_value) {
            // Mientras se aplica, el interruptor ya enseña el valor nuevo; si falla, vuelve.
            const bool applying = busy_ && applying_key_ == info.key;
            const std::string shown = applying ? applying_value_ : it->second;
            if (shown == "on") {
                lv_obj_add_state(row_switches_[i], LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(row_switches_[i], LV_STATE_CHECKED);
            }
            // Pendiente de confirmar (apagar Wi-Fi/Tailscale): el interruptor se pone ámbar.
            lv_obj_set_style_bg_color(row_switches_[i], confirm_key_ == info.key ? kWarnColor : kSwitchOn,
                                      LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_set_style_border_width(row_switches_[i], selected ? 1 : 0, LV_PART_MAIN);
            lv_obj_set_style_border_color(row_switches_[i], colors.background, LV_PART_MAIN);
            lv_obj_remove_flag(row_switches_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(row_values_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(row_titles_[i], selected ? colors.background : colors.text, 0);
            continue;
        }
        lv_obj_add_flag(row_switches_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(row_values_[i], LV_OBJ_FLAG_HIDDEN);
        if (!loaded || it == values.end()) {
            text = "…";
        } else if (it->second == "absent") {
            text = "not installed";
            dim = true;
        } else if (pend != pending_.end()) {
            text = "‹ " + power::option_label(info, pend->second) + " › •";
        } else {
            text = "‹ " + power::option_label(info, it->second) + " ›";
            // La memoria de la GPU de config.txt vale tras reiniciar: si no es la de ahora, se dice.
            if (std::strcmp(info.key, "gpu_mem") == 0) {
                const auto active = values.find("gpu_mem_active");
                if (active != values.end() && !active->second.empty() && active->second != it->second) {
                    text = "‹ " + power::option_label(info, it->second) + " › (now " + active->second + ")";
                }
            }
        }
        set_text(row_values_[i], text);
        lv_obj_set_style_text_color(row_titles_[i], selected ? colors.background : colors.text, 0);
        lv_obj_set_style_text_color(row_values_[i],
                                    selected ? colors.background : (dim ? colors.text_disabled : colors.text), 0);
    }

    if (!settings_.empty()) {
        const power::SettingInfo& info = *settings_[static_cast<std::size_t>(row_)];
        set_text(hint_, info.needs_reboot ? std::string(info.hint) + " Needs restart." : std::string(info.hint));
        lv_obj_set_style_text_color(hint_, colors.text_disabled, 0);
    }

    std::string status;
    lv_color_t status_color = colors.text_disabled;
    if (!confirm_key_.empty()) {
        status = "Remote access will be lost. Press again to turn off.";
        status_color = kWarnColor;
    } else if (busy_ && loaded) {
        status = message.empty() ? "Working…" : message;
    } else if (!loaded) {
        status = "Reading the system…";
    } else if (!pending_.empty()) {
        status = "ENTER apply   ESC discard";
        status_color = kWarnColor;
    } else if (!message.empty()) {
        status = message;
        status_color = message_error ? kErrorColor : colors.text_disabled;
    } else {
        status = control_.simulated() ? "Simulated system (no helper)" : "F/X choose   ENTER switch / apply";
    }
    set_text(status_, status);
    lv_obj_set_style_text_color(status_, status_color, 0);
}

void SettingsScreen::timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<SettingsScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->refresh();
    }
}

void SettingsScreen::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<SettingsScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
