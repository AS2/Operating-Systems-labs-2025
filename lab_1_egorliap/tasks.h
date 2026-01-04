#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

struct MoveRule {
    std::filesystem::path source;
    std::filesystem::path destination;
    int intervalSeconds{};
    std::chrono::steady_clock::time_point nextRun;
};

class TaskManager {
public:
    explicit TaskManager(const std::string& configPath);

    void loadConfig();
    void tick();

private:
    std::string configPath_;
    std::vector<MoveRule> rules_;
};


