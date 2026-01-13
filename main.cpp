#include "daemon.h"
#include <limits.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
    char absPath[PATH_MAX];
    if (realpath("config.ini", absPath) == nullptr) {
        perror("realpath failed for config.ini");
        return 1;
    }

    Daemon* daemon = Daemon::getInstance(absPath);
    daemon->run();

    return 0;
}