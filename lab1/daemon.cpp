#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <filesystem>

namespace fs = std::filesystem;

class Daemon {
private:
    static Daemon* instance;
    std::string configPath;
    std::string pidFilePath;
    std::string sourceDir;
    std::string destDir;
    int intervalSeconds;
    bool running;
    
    Daemon() : pidFilePath("/var/run/daemon.pid"), intervalSeconds(10), running(true) {
        configPath = fs::absolute("config.yaml").string();
    }
    
    static void signalHandler(int signum);
    void daemonize();
    bool killExisting();
    void loadConfig();
    void syncDirectories();
    std::string trim(const std::string& str);
    
public:
    static Daemon* getInstance();
    void start();
    void stop();
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
};

Daemon* Daemon::instance = nullptr;

void Daemon::signalHandler(int signum) {
    if (!instance) return;
    
    if (signum == SIGHUP) {
        syslog(LOG_INFO, "SIGHUP: перезагрузка конфигурации");
        instance->loadConfig();
    } else if (signum == SIGTERM || signum == SIGINT) {
        syslog(LOG_INFO, "Завершение работы");
        instance->stop();
        closelog();
        exit(0);
    }
}

bool Daemon::killExisting() {
    std::ifstream pidFile(pidFilePath);
    if (!pidFile.is_open()) return true;
    
    pid_t pid;
    pidFile >> pid;
    pidFile.close();
    
    if (kill(pid, 0) == 0) {
        kill(pid, SIGTERM);
        for (int i = 0; i < 10 && kill(pid, 0) == 0; i++) sleep(1);
    }
    
    unlink(pidFilePath.c_str());
    return true;
}

std::string Daemon::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    size_t end = str.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
}

void Daemon::loadConfig() {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        syslog(LOG_WARNING, "Не удалось открыть config.yaml");
        return;
    }
    
    intervalSeconds = 10;
    sourceDir.clear();
    destDir.clear();
    
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        size_t pos = line.find(':');
        if (pos == std::string::npos) continue;
        
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        
        if (key == "interval") intervalSeconds = std::stoi(value);
        else if (key == "source") sourceDir = value;
        else if (key == "destination") destDir = value;
    }
    
    syslog(LOG_INFO, "Конфиг загружен: интервал=%d, source=%s, dest=%s", 
           intervalSeconds, sourceDir.c_str(), destDir.c_str());
}

void Daemon::syncDirectories() {
    if (sourceDir.empty() || destDir.empty()) {
        syslog(LOG_WARNING, "Пустые пути source/destination");
        return;
    }
    
    if (!fs::exists(sourceDir) || !fs::is_directory(sourceDir)) {
        syslog(LOG_WARNING, "Source не существует: %s", sourceDir.c_str());
        return;
    }
    
    fs::create_directories(destDir);
    
    std::set<std::string> sourceFiles;
    
    try {
        for (const auto& entry : fs::directory_iterator(sourceDir)) {
            if (!entry.is_regular_file()) continue;
            
            std::string filename = entry.path().filename().string();
            sourceFiles.insert(filename);
            
            fs::path destPath = fs::path(destDir) / filename;
            
            if (!fs::exists(destPath) || 
                fs::last_write_time(entry.path()) != fs::last_write_time(destPath) ||
                fs::file_size(entry.path()) != fs::file_size(destPath)) {
                
                fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing);
                syslog(LOG_INFO, "Скопирован: %s", filename.c_str());
            }
        }
        
        for (const auto& entry : fs::directory_iterator(destDir)) {
            if (!entry.is_regular_file()) continue;
            
            std::string filename = entry.path().filename().string();
            if (sourceFiles.find(filename) == sourceFiles.end()) {
                fs::remove(entry.path());
                syslog(LOG_INFO, "Удален: %s", filename.c_str());
            }
        }
        
        syslog(LOG_INFO, "Синхронизация завершена: %zu файлов", sourceFiles.size());
    } catch (const std::exception& e) {
        syslog(LOG_ERR, "Ошибка синхронизации: %s", e.what());
    }
}

void Daemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    
    if (setsid() < 0) exit(EXIT_FAILURE);
    
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    
    chdir("/");
    umask(0);
    
    for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) close(x);
    
    int devnull = open("/dev/null", O_RDWR);
    dup(devnull);
    dup(devnull);
}

void Daemon::start() {
    openlog("sync_daemon", LOG_PID | LOG_PERROR, LOG_USER);
    syslog(LOG_INFO, "Запуск демона...");
    
    loadConfig();
    
    const char* dockerMode = getenv("DOCKER_MODE");
    bool isDocker = (dockerMode && std::string(dockerMode) == "1");
    
    if (!isDocker) {
        killExisting();
        daemonize();
    } else {
        syslog(LOG_INFO, "Docker режим: работа без демонизации");
    }
    
    signal(SIGHUP, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGINT, signalHandler);
    
    std::ofstream pidFile(pidFilePath);
    if (pidFile.is_open()) {
        pidFile << getpid();
        pidFile.close();
    }
    
    syslog(LOG_INFO, "Демон запущен (PID: %d)", getpid());
    
    while (running) {
        syncDirectories();
        sleep(intervalSeconds);
    }
    
    closelog();
}

void Daemon::stop() {
    running = false;
    unlink(pidFilePath.c_str());
}

Daemon* Daemon::getInstance() {
    if (!instance) instance = new Daemon();
    return instance;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--version") {
        std::cout << "Sync Daemon v2.0\n";
        return 0;
    }
    
    Daemon::getInstance()->start();
    return 0;
}