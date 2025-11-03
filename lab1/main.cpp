#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>

#include <iostream>

#include "config.h"
#include "daemon.h"

using namespace std;

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        cout << "Usage: ./daemon config.cfg" << endl;
        exit(EXIT_FAILURE);
    }

    openlog("daemon", LOG_CONS | LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Opened a connection to the system logger.");
    syslog(LOG_INFO, "Run.");

    Config &cfg = Config::GetInstance(argv[1]);

    pid_t pid = fork();

    if (pid == -1)
    {
        syslog(LOG_ERR, "First fork error.");
        exit(EXIT_FAILURE);
    }

    if (pid != 0)
        exit(EXIT_SUCCESS);
    
    if (setsid() == (pid_t)(-1))
    {
        syslog(LOG_ERR, "Setsid error.");
        exit(EXIT_FAILURE);
    }

    umask(0);

    pid = fork();

    if (pid == -1)
    {
        syslog(LOG_ERR, "Second fork error.");
        exit(EXIT_FAILURE);
    }

    if (pid != 0)
        exit(EXIT_SUCCESS);
    
    if (chdir("/") == -1)
    {
        syslog(LOG_ERR, "Chdir error.");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    auto& daemon = Daemon::getInstance();

    signal(SIGHUP, HandleSignal);
    signal(SIGTERM, HandleSignal);

    daemon.ensureSingleInstance();

    while (true)
    {
        daemon.purgeSubdirectories();
        sleep(cfg.getIntervalSeconds());
    }
}