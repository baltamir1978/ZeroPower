/*
 * SPDX-License-Identifier: MIT
 */

#include "sudo_password.h"

namespace power {
namespace {

bool contains(const std::string& text, const char* what) {
    return text.find(what) != std::string::npos;
}

} // namespace

SudoOutcome classify_sudo(int exit_code, const std::string& output) {
    if (exit_code == 0) {
        return SudoOutcome::Ok;
    }
    // "Sorry, try again." sale en cuanto una contraseña falla, aunque luego diga otra cosa.
    if (contains(output, "incorrect password") || contains(output, "Sorry, try again")) {
        return SudoOutcome::WrongPassword;
    }
    if (contains(output, "a password is required") || contains(output, "no password was provided")) {
        return SudoOutcome::NeedsPassword;
    }
    if (contains(output, "is not in the sudoers") || contains(output, "is not allowed to") ||
        contains(output, "may not run sudo")) {
        return SudoOutcome::NotAllowed;
    }
    return SudoOutcome::Failed;
}

void wipe(std::string& text) {
    volatile char* p = text.empty() ? nullptr : &text[0];
    for (std::size_t i = 0; i < text.size(); ++i) {
        p[i] = '\0';
    }
    text.clear();
}

SecretInput::SecretInput() {
    text_.reserve(kMaxLength + 1); // +1: el salto de línea de take_line()
}

SecretInput::~SecretInput() {
    clear();
}

bool SecretInput::add(std::uint32_t ch) {
    if (ch < 0x20 || ch > 0x7E || text_.size() >= kMaxLength) {
        return false;
    }
    text_.push_back(static_cast<char>(ch));
    return true;
}

bool SecretInput::backspace() {
    if (text_.empty()) {
        return false;
    }
    text_.back() = '\0';
    text_.pop_back();
    return true;
}

void SecretInput::clear() {
    wipe(text_);
}

std::string SecretInput::masked(std::size_t max_dots) const {
    std::string out;
    std::size_t dots = text_.size();
    if (dots > max_dots) {
        out = "\u2026";
        dots = max_dots;
    }
    for (std::size_t i = 0; i < dots; ++i) {
        out += "\u2022";
    }
    return out;
}

std::string SecretInput::take_line() {
    // Siempre en el montón (más de lo que cabe dentro del propio std::string): al moverla de
    // un sitio a otro se pasa el puntero y no quedan copias sueltas.
    std::string line;
    line.reserve(kMaxLength + 1);
    line = text_;
    line.push_back('\n');
    clear();
    return line;
}

} // namespace power
