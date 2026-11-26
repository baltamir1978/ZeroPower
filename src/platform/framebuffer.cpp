/*
 * SPDX-License-Identifier: MIT
 */

#include "framebuffer.h"

#include <fstream>
#include <iterator>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace platform {

namespace {

bool write_all(int fd, const std::uint8_t* data, std::size_t size) {
    if (::lseek(fd, 0, SEEK_SET) != 0) {
        return false;
    }
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n <= 0) {
            return false;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace

bool FramebufferImage::show(const std::string& image, const std::string& device) {
    std::ifstream in(image, std::ios::binary);
    if (!in) {
        return false;
    }
    const std::vector<std::uint8_t> pixels((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const int fd = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    fb_var_screeninfo var{};
    fb_fix_screeninfo fix{};
    bool ok = ::ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0 && ::ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0 &&
              var.bits_per_pixel == 16 && fix.line_length == var.xres * 2 &&
              pixels.size() == static_cast<std::size_t>(fix.line_length) * var.yres;
    if (ok) {
        std::vector<std::uint8_t> before(pixels.size());
        ok = ::lseek(fd, 0, SEEK_SET) == 0 &&
             ::read(fd, before.data(), before.size()) == static_cast<ssize_t>(before.size()) &&
             write_all(fd, pixels.data(), pixels.size());
        if (ok) {
            saved_ = std::move(before);
            device_ = device;
        }
    }
    ::close(fd);
    return ok;
}

void FramebufferImage::restore() {
    if (saved_.empty()) {
        return;
    }
    const int fd = ::open(device_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        write_all(fd, saved_.data(), saved_.size());
        ::close(fd);
    }
    saved_.clear();
}

} // namespace platform
