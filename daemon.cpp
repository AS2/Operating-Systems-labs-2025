#include "daemon.h"

Daemon* Daemon::instance = nullptr;

Daemon::Daemon(const std::string& config) : configPath(config), pidFile("/var/run/mydaemon.pid"), interval(10) {
    openlog("mydaemon", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Daemon initialized with config: %s", config.c_str());
    readConfig();
}

Daemon* Daemon::getInstance(const std::string& config) {
    if (instance == nullptr) {
        instance = new Daemon(config);
    }
    return instance;
}

void Daemon::readConfig() {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        syslog(LOG_ERR, "Failed to open config file: %s", configPath.c_str());
        exit(EXIT_FAILURE);
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == ';') continue;  // Skip comments/empty
        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            // Trim spaces (simple trim)
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            if (key == "dir1") dir1 = value;
            else if (key == "interval_seconds") interval = std::stoi(value);
        }
    }
    file.close();
    syslog(LOG_INFO, "Config loaded: dir1=%s, interval=%d", dir1.c_str(), interval);
}

void Daemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);  // Parent exits
    }

    umask(0);
    pid_t sid = setsid();
    if (sid < 0) {
        syslog(LOG_ERR, "setsid failed");
        exit(EXIT_FAILURE);
    }

    if (chdir("/") < 0) {
        syslog(LOG_ERR, "chdir failed");
        exit(EXIT_FAILURE);
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect to /dev/null
    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);

    syslog(LOG_INFO, "Daemonized successfully");
}

void Daemon::checkExistingInstance() {
    std::ifstream pidStream(pidFile);
    if (pidStream.is_open()) {
        pid_t oldPid;
        pidStream >> oldPid;
        pidStream.close();

        std::string procPath = "/proc/" + std::to_string(oldPid);
        struct stat statbuf;
        if (stat(procPath.c_str(), &statbuf) == 0) {
            syslog(LOG_INFO, "Killing existing daemon with PID %d", oldPid);
            kill(oldPid, SIGTERM);
            sleep(1);  // Wait a bit for termination
        }
    }
}

void Daemon::writePid() {
    std::ofstream pidStream(pidFile);
    if (!pidStream.is_open()) {
        syslog(LOG_ERR, "Failed to open PID file: %s", pidFile.c_str());
        exit(EXIT_FAILURE);
    }
    pidStream << getpid();
    pidStream.close();
}

void Daemon::cleanDirectory() {
    DIR* dir = opendir(dir1.c_str());
    if (dir == nullptr) {
        syslog(LOG_ERR, "Failed to open directory: %s", dir1.c_str());
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".tmp") {
            std::string fullPath = dir1 + "/" + filename;
            if (unlink(fullPath.c_str()) == 0) {
                syslog(LOG_INFO, "Deleted file: %s", fullPath.c_str());
            } else {
                syslog(LOG_ERR, "Failed to delete file: %s", fullPath.c_str());
            }
        }
    }
    closedir(dir);
}

void Daemon::signalHandler(int sig) {
    if (sig == SIGHUP) {
        syslog(LOG_INFO, "Received SIGHUP, reloading config");
        instance->readConfig();
    } else if (sig == SIGTERM) {
        syslog(LOG_INFO, "Received SIGTERM, exiting");
        closelog();
        exit(EXIT_SUCCESS);
    }
}

void Daemon::handleSignals() {
    signal(SIGHUP, signalHandler);
    signal(SIGTERM, signalHandler);
}

void Daemon::run() {
    checkExistingInstance();
    daemonize();
    writePid();
    handleSignals();

    while (true) {
        cleanDirectory();
        sleep(interval);
    }
}