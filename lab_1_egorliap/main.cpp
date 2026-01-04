#include "daemon.h"
#include <iostream>

int main(int argc, char* argv[]) {
    std::string configPath;
    if (argc > 1) {
        configPath = argv[1];
    } else {
        configPath = "config.txt";
    }

    try {
        Daemon& daemon = Daemon::instance();
        daemon.setConfigPath(configPath);
        daemon.run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}


