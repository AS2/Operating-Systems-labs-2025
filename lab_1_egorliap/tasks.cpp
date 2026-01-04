#include "tasks.h"

#include <fstream>
#include <sstream>

#include <syslog.h>

TaskManager::TaskManager(const std::string& configPath)
    : configPath_(configPath) {}

void TaskManager::loadConfig() {
    namespace fs = std::filesystem;

    std::ifstream in(configPath_);
    if (!in.is_open()) {
        throw std::runtime_error("Cannot open config file");
    }

    std::vector<MoveRule> newRules;
    std::string line;
    auto now = std::chrono::steady_clock::now();

    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string srcStr, dstStr;
        int interval = 0;
        if (!(iss >> srcStr >> dstStr >> interval)) {
            syslog(LOG_ERR, "Invalid config line: %s", line.c_str());
            continue;
        }
        if (interval <= 0) {
            syslog(LOG_ERR, "Non-positive interval in line: %s", line.c_str());
            continue;
        }

        MoveRule rule;
        rule.source = fs::path(srcStr);
        rule.destination = fs::path(dstStr);
        rule.intervalSeconds = interval;
        rule.nextRun = now + std::chrono::seconds(interval);

        newRules.push_back(std::move(rule));
    }

    rules_ = std::move(newRules);
    syslog(LOG_INFO, "Loaded %zu rules from config", rules_.size());
}

void TaskManager::tick() {
    namespace fs = std::filesystem;
    auto now = std::chrono::steady_clock::now();

    for (auto& rule : rules_) {
        if (now < rule.nextRun) {
            continue;
        }

        rule.nextRun = now + std::chrono::seconds(rule.intervalSeconds);

        std::error_code ec;
        if (!fs::exists(rule.source, ec) || !fs::is_directory(rule.source, ec)) {
            syslog(LOG_ERR, "Source directory does not exist or is not a directory: %s",
                   rule.source.string().c_str());
            continue;
        }

        fs::create_directories(rule.destination, ec);
        if (ec) {
            syslog(LOG_ERR, "Failed to create destination directory %s: %s",
                   rule.destination.string().c_str(), ec.message().c_str());
            continue;
        }

        for (const auto& entry : fs::directory_iterator(rule.source, ec)) {
            if (ec) {
                syslog(LOG_ERR, "Error iterating directory %s: %s",
                       rule.source.string().c_str(), ec.message().c_str());
                break;
            }

            fs::path dstPath = rule.destination / entry.path().filename();

            fs::rename(entry.path(), dstPath, ec);
            if (ec) {
                syslog(LOG_ERR, "Failed to move %s to %s: %s",
                       entry.path().string().c_str(),
                       dstPath.string().c_str(),
                       ec.message().c_str());
            }
        }
    }
}


