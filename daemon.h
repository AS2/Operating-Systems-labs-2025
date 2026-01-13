#ifndef DAEMON_H
#define DAEMON_H

#include <string>
#include <syslog.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

class Daemon {
private:
    static Daemon* instance;  // Singleton instance
    std::string configPath;   // Absolute path to config
    std::string dir1;         // Directory to clean
    std::string pidFile;      // PID file path
    int interval;             // Interval in seconds

    Daemon(const std::string& config);  // Private constructor
    void daemonize();                  // Daemonization process
    void readConfig();                 // Read or reread config
    void cleanDirectory();             // Clean *.tmp files
    void handleSignals();              // Setup signal handlers
    void checkExistingInstance();      // Check and kill existing daemon
    void writePid();                   // Write PID to file

    static void signalHandler(int sig);  // Static handler for signals

public:
    static Daemon* getInstance(const std::string& config);
    void run();  // Main loop
};

#endif // DAEMON_H