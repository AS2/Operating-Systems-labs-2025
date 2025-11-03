#include <syslog.h>
#include <fstream>
#include <filesystem>
#include "config.h"

using namespace std;

const filesystem::path& Config::getTargetDirectoryPath()
{
    return targetDirectoryPath;
}

int Config::getIntervalSeconds()
{
    return intervalSeconds;
}

void Config::reload()
{
    ifstream cfgFile(configPath);
    filesystem::path dir;
    int interval{};

    cfgFile >> dir >> interval;

    if (!cfgFile || dir.empty() || interval <= 0)
    {
        syslog(LOG_ERR, "Bad config.");
        exit(EXIT_FAILURE);
    }

    if (!dir.is_absolute())
    {
        syslog(LOG_ERR, "Config path must be absolute.");
        exit(EXIT_FAILURE);
    }

    targetDirectoryPath = dir;
    intervalSeconds = interval;
    syslog(LOG_INFO, "Read config.");
}

Config::Config(char *configFilePath)
{
    if (configFilePath == nullptr)
    {
        syslog(LOG_ERR, "Config path is not provided.");
        exit(EXIT_FAILURE);
    }

    configPath = filesystem::weakly_canonical(filesystem::path(configFilePath));
    if (configPath.empty())
    {
        syslog(LOG_ERR, "Config doesn't exist.");
        exit(EXIT_FAILURE);
    }

    reload();
}

Config& Config::GetInstance(char *configFilePath)
{
    static Config instance(configFilePath);
    return instance;
}