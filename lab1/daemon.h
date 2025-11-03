#ifndef DAEMON_H
#define DAEMON_H

#include <filesystem>
#include <string>

void HandleSignal(int sigNum);

class Daemon
{
public:
    void ensureSingleInstance();
    void purgeSubdirectories();
    static Daemon& getInstance();

private:
    explicit Daemon(std::filesystem::path pidPath): pidFilePath(std::move(pidPath)) {}
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    const std::filesystem::path pidFilePath;
    static void safeChdir(const std::filesystem::path& path);
};

#endif //DAEMON_H