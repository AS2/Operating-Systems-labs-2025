#include "daemon.h"
#include "tasks.h"

#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <chrono>

#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>

namespace {

volatile sig_atomic_t g_reloadRequested = 0;
volatile sig_atomic_t g_terminateRequested = 0;

void handleSignal(int sig) {
    if (sig == SIGHUP) {
        g_reloadRequested = 1;
    } else if (sig == SIGTERM) {
        g_terminateRequested = 1;
    }
}

const char* PID_FILE = "/tmp/os_lab1_daemon.pid";

bool processExists(pid_t pid) {
    std::filesystem::path procPath = "/proc";
    procPath /= std::to_string(pid);
    return std::filesystem::exists(procPath);
}

void ensureSingleInstance() {
    std::ifstream pidIn(PID_FILE);
    if (!pidIn.is_open()) {
        return;
    }

    pid_t oldPid = 0;
    pidIn >> oldPid;
    pidIn.close();

    if (oldPid <= 0) {
        return;
    }

    if (processExists(oldPid)) {
        // Попробовать аккуратно завершить старый демон
        kill(oldPid, SIGTERM);
        // Дать время на завершение
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void writePidFile() {
    std::ofstream pidOut(PID_FILE, std::ios::trunc);
    if (!pidOut.is_open()) {
        syslog(LOG_ERR, "Failed to open pid file %s: %s", PID_FILE, std::strerror(errno));
        return;
    }
    pidOut << getpid() << std::endl;
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("fork failed");
    }
    if (pid > 0) {
        // Родительский процесс завершается
        _exit(0);
    }

    // Делаем дочерний процесс лидером сессии
    if (setsid() < 0) {
        throw std::runtime_error("setsid failed");
    }

    // Меняем рабочий каталог на корень
    if (chdir("/") < 0) {
        throw std::runtime_error("chdir failed");
    }

    umask(0);

    // Закрываем стандартные файловые дескрипторы
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Перенаправляем std дескрипторы в /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) {
            close(fd);
        }
    }
}

} // namespace

Daemon& Daemon::instance() {
    static Daemon instance;
    return instance;
}

void Daemon::setConfigPath(const std::string& path) {
    namespace fs = std::filesystem;
    fs::path p(path);
    if (!p.is_absolute()) {
        p = fs::absolute(p);
    }
    configPath_ = p.string();
}

void Daemon::run() {
    if (configPath_.empty()) {
        throw std::runtime_error("Config path is not set");
    }

    ensureSingleInstance();

    daemonize();

    // Открываем syslog
    openlog("os_lab1_daemon", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Daemon started");

    writePidFile();

    // Настройка обработчиков сигналов
    struct sigaction sa {};
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    TaskManager tasks(configPath_);

    try {
        tasks.loadConfig();
    } catch (const std::exception& ex) {
        syslog(LOG_ERR, "Failed to load config: %s", ex.what());
    }

    while (!g_terminateRequested) {
        if (g_reloadRequested) {
            g_reloadRequested = 0;
            syslog(LOG_INFO, "Reloading configuration");
            try {
                tasks.loadConfig();
            } catch (const std::exception& ex) {
                syslog(LOG_ERR, "Failed to reload config: %s", ex.what());
            }
        }

        tasks.tick();

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    syslog(LOG_INFO, "Daemon exiting");

    std::error_code ec;
    std::filesystem::remove(PID_FILE, ec);
    if (ec) {
        syslog(LOG_ERR, "Failed to remove pid file %s: %s", PID_FILE, ec.message().c_str());
    }

    closelog();
}


