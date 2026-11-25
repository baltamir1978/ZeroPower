/*
 * SPDX-License-Identifier: MIT
 */

#include "process.h"

#include <csignal>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace platform {

ProcessResult run_process(const std::vector<std::string>& argv, const std::string& input) {
    ProcessResult result;
    if (argv.empty()) {
        return result;
    }
    int pipefd[2];
    if (::pipe2(pipefd, O_CLOEXEC) != 0) {
        return result;
    }
    int infd[2] = {-1, -1};
    if (!input.empty()) {
        // Si el hijo sale sin leer la entrada, escribir daría SIGPIPE y mataría la app.
        ::signal(SIGPIPE, SIG_IGN);
        if (::pipe2(infd, O_CLOEXEC) != 0) {
            ::close(pipefd[0]);
            ::close(pipefd[1]);
            return result;
        }
    }
    // Todo lo que reserva memoria, antes del fork (la app tiene hilos).
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& a : argv) {
        args.push_back(const_cast<char*>(a.c_str()));
    }
    args.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        if (infd[0] >= 0) {
            ::close(infd[0]);
            ::close(infd[1]);
        }
        return result;
    }
    if (pid == 0) {
        ::signal(SIGPIPE, SIG_DFL); // un SIG_IGN se heredaría a través de exec
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        if (infd[0] >= 0) {
            ::dup2(infd[0], STDIN_FILENO);
        } else {
            const int devnull = ::open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                ::dup2(devnull, STDIN_FILENO);
            }
        }
        ::execvp(args[0], args.data());
        ::_exit(127);
    }
    ::close(pipefd[1]);
    if (infd[0] >= 0) {
        // Cabe de sobra en el búfer de la tubería: se escribe entero antes de leer la salida.
        ::close(infd[0]);
        std::size_t done = 0;
        while (done < input.size()) {
            const ssize_t w = ::write(infd[1], input.data() + done, input.size() - done);
            if (w <= 0) {
                break;
            }
            done += static_cast<std::size_t>(w);
        }
        ::close(infd[1]);
    }
    char buf[512];
    ssize_t n;
    while ((n = ::read(pipefd[0], buf, sizeof(buf))) > 0) {
        result.output.append(buf, static_cast<std::size_t>(n));
    }
    ::close(pipefd[0]);
    int status = 0;
    if (::waitpid(pid, &status, 0) == pid && WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
    return result;
}

std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return "";
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

} // namespace platform
