#pragma once

#include <string>

class Daemon {
public:
    static Daemon& instance();

    void setConfigPath(const std::string& path);
    void run();

private:
    Daemon() = default;
    ~Daemon() = default;

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    std::string configPath_;
};


