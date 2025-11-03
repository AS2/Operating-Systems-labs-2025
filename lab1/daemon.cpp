
#include <signal.h>
#include <unistd.h>
#include <syslog.h>

#include <fstream>
#include <filesystem>

#include "config.h"
#include "daemon.h"

using namespace std;

void Daemon::ensureSingleInstance()
{
    ifstream pidInputFile(pidFilePath);
    pid_t previousPid{};
    if (pidInputFile >> previousPid)
    {
        kill(previousPid, SIGTERM);
    }
    pidInputFile.close();

    ofstream pidOutputFile(pidFilePath);
    pidOutputFile << getpid() << endl;
    pidOutputFile.close();
}

void HandleSignal(int sigNum)
{
    switch(sigNum)
    {
    case SIGHUP:
    {
        syslog(LOG_INFO, "Catched SIGHUP.");
        Config &cfg = Config::GetInstance();
        cfg.reload();
        break;
    }
    case SIGTERM:
        syslog(LOG_INFO, "Catched SIGTERM.");
        syslog(LOG_INFO, "Completed.");
        exit(EXIT_SUCCESS);
    default:
        break;
    }
}

static void removeRecursively(const filesystem::path& p)
{
    error_code ec;
    filesystem::remove_all(p, ec);
    if (ec)
    {
        syslog(LOG_ERR, "Couldn't remove path");
    }
}

void Daemon::purgeSubdirectories()
{
    Config &cfg = Config::GetInstance();
    const auto& rootDir = cfg.getTargetDirectoryPath();

    safeChdir("/");

    if (!filesystem::exists(rootDir) || !filesystem::is_directory(rootDir))
    {
        syslog(LOG_ERR, "Couldn't open folder.");
        exit(EXIT_FAILURE);
    }

    for (const auto& entry : filesystem::directory_iterator(rootDir))
    {
        if (entry.is_directory())
        {
            removeRecursively(entry.path());
        }
    }

    safeChdir(rootDir);
}

Daemon& Daemon::getInstance() {
    static Daemon instance("/var/run/daemon.pid");
    return instance;
}

void Daemon::safeChdir(const std::filesystem::path& path) {
    if (chdir(path.c_str()) == -1) {
        syslog(LOG_ERR, "Error with chdir");
        exit(EXIT_FAILURE);
    }
}
