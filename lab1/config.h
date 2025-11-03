#ifndef CONFIG_H
#define CONFIG_H

#include <filesystem>
#include <string>

class Config
{
public:
    static Config& GetInstance(char *configFilePath = nullptr);

    const std::filesystem::path& getTargetDirectoryPath();
    int getIntervalSeconds();
    void reload();

private:
    std::filesystem::path configPath;
    std::filesystem::path targetDirectoryPath;
    int intervalSeconds;

    Config(char *configFilePath);
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
};

#endif //CONFIG_H