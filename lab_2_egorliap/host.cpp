#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>

#include "message.h"

#if defined(SHM)
#include "conn_shm.h"
using ConnType = ConnShm;
#elif defined(MQ)
#include "conn_mq.h"
using ConnType = ConnMq;
#elif defined(FIFO)
#include "conn_fifo.h"
using ConnType = ConnFifo;
#else
#error "Define SHM, MQ or FIFO"
#endif

volatile bool running = true;
sem_t *shared_sem = nullptr;

namespace {

void log_message(const std::string &s) {
    // Пишем и в файл, и в консоль
    {
        std::ofstream log_file("log.txt", std::ios::app);
        if (log_file.is_open()) {
            std::time_t now = std::time(nullptr);
            char buf[64]{};
            std::tm tm_now{};
            localtime_r(&now, &tm_now);
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
            log_file << "[" << buf << "] " << s << '\n';
        }
    }

    std::cout << s << std::endl;
}

constexpr int NUM_CLIENTS        = 3;
constexpr int INACTIVITY_TIMEOUT = 60; // сек

std::vector<pid_t>        client_pids;
std::vector<ConnType *>   connections;
std::vector<std::time_t>  last_activity;
std::vector<std::thread>  receiver_threads;

std::mutex                     clients_mutex;
std::vector<bool>              client_ready(NUM_CLIENTS, false);
std::vector<bool>              client_listening(NUM_CLIENTS, false);
std::condition_variable        ready_cv;

std::queue<Message> incoming_queue;
std::mutex          queue_mutex;

bool write_msg(ConnType *conn, const Message &msg) {
    return conn->Write(&msg, MSG_SIZE);
}

void receiver_thread(int id, ConnType *conn) {
    log_message("Client " + std::to_string(id) + " joined");
    Message msg{};

    while (running && conn->Read(&msg, MSG_SIZE)) {
        bool is_system = (msg.target_id == TARGET_SYSTEM);
        if (is_system) {
            std::string text(msg.text);
            {
                std::lock_guard<std::mutex> lk(clients_mutex);
                if (id < static_cast<int>(last_activity.size())) {
                    last_activity[id] = std::time(nullptr);
                }
                if (text == "ready") {
                    if (id < static_cast<int>(client_ready.size()) && !client_ready[id]) {
                        client_ready[id] = true;
                        ready_cv.notify_all();
                        log_message("Client " + std::to_string(id) + " ready");
                    }
                } else if (text == "listening") {
                    if (id < static_cast<int>(client_listening.size()) && !client_listening[id]) {
                        client_listening[id] = true;
                        ready_cv.notify_all();
                        log_message("Client " + std::to_string(id) + " started listening");
                    }
                }
            }
            continue;
        }

        {
            std::lock_guard<std::mutex> lk(queue_mutex);
            incoming_queue.push(msg);
        }
    }

    log_message("Client " + std::to_string(id) + " disconnected");
}

void broadcaster_thread() {
    std::vector<Message> batch;

    while (running) {
        batch.clear();
        {
            std::lock_guard<std::mutex> lk(queue_mutex);
            while (!incoming_queue.empty()) {
                batch.push_back(incoming_queue.front());
                incoming_queue.pop();
            }
        }

        for (const auto &m : batch) {
            std::string text(m.text);

            if (m.target_id == TARGET_BROADCAST) {
                log_message("Broadcast from " + std::to_string(m.sender_id) + ": " + text);
                std::lock_guard<std::mutex> lk(clients_mutex);
                for (std::size_t idx = 0; idx < connections.size(); ++idx) {
                    if (!write_msg(connections[idx], m)) {
                        log_message("Failed to send broadcast to client " +
                                    std::to_string(idx) + ": " + std::strerror(errno));
                    }
                }
            } else {
                log_message("Private from " + std::to_string(m.sender_id) +
                            " to " + std::to_string(m.target_id) + ": " + text);
                std::lock_guard<std::mutex> lk(clients_mutex);
                if (m.target_id >= 0 &&
                    m.target_id < static_cast<int>(connections.size())) {
                    if (!write_msg(connections[static_cast<std::size_t>(m.target_id)], m)) {
                        log_message("Failed to send private to " +
                                    std::to_string(m.target_id) + ": " + std::strerror(errno));
                    }
                }
            }
        }

        ::usleep(100000); // 100 мс
    }
}

void inactivity_monitor() {
    while (running) {
        ::sleep(5);
        std::time_t now = std::time(nullptr);

        std::lock_guard<std::mutex> lk(clients_mutex);
        for (int i = 0; i < NUM_CLIENTS; ++i) {
            if (client_pids[i] > 0 && now - last_activity[i] > INACTIVITY_TIMEOUT) {
                log_message("Killing inactive client " + std::to_string(i));
                ::kill(client_pids[i], SIGTERM);
                ::sleep(1);
                ::kill(client_pids[i], SIGKILL);
                client_pids[i] = -1;
            }
        }
    }
}

void run_client(int id) {
    ConnType conn(id, false);

    Message ready{};
    ready.timestamp = std::time(nullptr);
    ready.sender_id = id;
    ready.target_id = TARGET_SYSTEM;
    std::strncpy(ready.text, "ready", sizeof(ready.text) - 1);
    ready.text[sizeof(ready.text) - 1] = '\0';

    if (!write_msg(&conn, ready)) {
        log_message("Client " + std::to_string(id) + " failed to send ready signal");
    }

    Message listening{};
    listening.timestamp = std::time(nullptr);
    listening.sender_id = id;
    listening.target_id = TARGET_SYSTEM;
    std::strncpy(listening.text, "listening", sizeof(listening.text) - 1);
    listening.text[sizeof(listening.text) - 1] = '\0';
    if (!write_msg(&conn, listening)) {
        log_message("Client " + std::to_string(id) + " failed to send listening signal");
    }

    log_message("Client " + std::to_string(id) + " waiting for messages");

    Message m{};
    while (conn.Read(&m, MSG_SIZE)) {
        std::string prefix;
        if (m.sender_id == -1) {
            prefix = (m.target_id == TARGET_BROADCAST)
                         ? "broadcast"
                         : ("host private to " + std::to_string(m.target_id));
        } else {
            prefix = "from " + std::to_string(m.sender_id);
        }
        log_message("Client " + std::to_string(id) +
                    " received " + prefix + ": " + m.text);
    }

    log_message("Client " + std::to_string(id) + " exiting");
}

}

int main() {
    void *ptr = ::mmap(nullptr,
                       sizeof(sem_t),
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS,
                       -1,
                       0);
    if (ptr == MAP_FAILED) {
        std::perror("mmap for semaphore");
        return 1;
    }

    shared_sem = static_cast<sem_t *>(ptr);
    if (::sem_init(shared_sem, 1, 1) != 0) {
        std::perror("sem_init");
        return 1;
    }

    log_message("=== Chat server starting ===");

#ifdef FIFO
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        ConnFifo::PrepareEndpoints(i);
    }
#endif

    // Запускаем дочерние процессы-клиенты
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        pid_t pid = ::fork();
        if (pid == -1) {
            std::perror("fork");
            return 1;
        }

        if (pid == 0) {
            // ======== CLIENT =========
            run_client(i);
            _exit(0);
        } else {
            // ======== HOST ==========
            auto *conn = new ConnType(i, true);
            client_pids.push_back(pid);
            connections.push_back(conn);
            last_activity.push_back(std::time(nullptr));
            receiver_threads.emplace_back(receiver_thread, i, conn);
        }
    }

    {
        std::unique_lock<std::mutex> lk(clients_mutex);
        bool all_ready = ready_cv.wait_for(
            lk,
            std::chrono::seconds(5),
            [] {
                return std::all_of(
                    client_ready.begin(),
                    client_ready.end(),
                    [](bool v) { return v; }
                );
            }
        );
        if (!all_ready) {
            log_message("Warning: not all clients confirmed readiness within 5 seconds");
        } else {
            log_message("All clients confirmed readiness");
        }
    }

    {
        std::unique_lock<std::mutex> lk(clients_mutex);
        bool all_listening = ready_cv.wait_for(
            lk,
            std::chrono::seconds(5),
            [] {
                return std::all_of(
                    client_listening.begin(),
                    client_listening.end(),
                    [](bool v) { return v; }
                );
            }
        );
        if (!all_listening) {
            log_message("Warning: not all clients started listening within 5 seconds");
        } else {
            log_message("All clients started listening");
        }
    }

    std::thread broadcaster(broadcaster_thread);
    std::thread monitor(inactivity_monitor);

    log_message("Chat server ready. Commands:");
    log_message("  all:<message>       - broadcast to all");
    log_message("  to <id>:<message>   - send to specific client (0-2)");
    log_message("  quit                - shutdown server");

    // Основной цикл ввода хоста
    std::string line;
    while (running && std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        if (line == "quit" || line == "exit") {
            log_message("Server shutting down...");
            running = false;
            break;
        }

        Message m{};
        m.timestamp = std::time(nullptr);
        m.sender_id = -1;
        bool sent = false;

        if (line.rfind("all:", 0) == 0) {
            m.target_id = TARGET_BROADCAST;
            std::string text = line.substr(4);
            if (!text.empty()) {
                std::strncpy(m.text, text.c_str(), sizeof(m.text) - 1);
                m.text[sizeof(m.text) - 1] = '\0';
                log_message("Host broadcast: " + text);
                sent = true;
            }
        } else if (line.rfind("to ", 0) == 0) {
            std::size_t colon = line.find(':', 3);
            if (colon != std::string::npos) {
                std::string num_str = line.substr(3, colon - 3);
                try {
                    int target = std::stoi(num_str);
                    std::string text = line.substr(colon + 1);
                    if (target >= 0 && target < NUM_CLIENTS && !text.empty()) {
                        m.target_id = target;
                        std::strncpy(m.text, text.c_str(), sizeof(m.text) - 1);
                        m.text[sizeof(m.text) - 1] = '\0';
                        log_message("Host private to " + std::to_string(target) + ": " + text);
                        sent = true;
                    } else {
                        log_message("Invalid target ID or empty message");
                    }
                } catch (...) {
                    log_message("Invalid command format");
                }
            }
        } else {
            log_message("Unknown command. Use 'all:<msg>' or 'to <id>:<msg>'");
        }

        if (sent) {
            std::lock_guard<std::mutex> lk(clients_mutex);
            if (m.target_id == TARGET_BROADCAST) {
                for (std::size_t idx = 0; idx < connections.size(); ++idx) {
                    if (!write_msg(connections[idx], m)) {
                        log_message("Failed to send manual broadcast to client " +
                                    std::to_string(idx) + ": " + std::strerror(errno));
                    }
                }
            } else if (m.target_id >= 0 &&
                       m.target_id < static_cast<int>(connections.size())) {
                if (!write_msg(connections[static_cast<std::size_t>(m.target_id)], m)) {
                    log_message("Failed to send manual private to " +
                                std::to_string(m.target_id) + ": " + std::strerror(errno));
                }
            }
        }
    }

    // Завершение
    running = false;
    log_message("Waiting for threads to finish...");

    if (broadcaster.joinable()) {
        broadcaster.join();
    }
    if (monitor.joinable()) {
        monitor.join();
    }
    for (auto &t : receiver_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    log_message("Terminating clients...");
    for (pid_t p : client_pids) {
        if (p > 0) {
            ::kill(p, SIGTERM);
        }
    }
    ::sleep(1);
    for (pid_t p : client_pids) {
        if (p > 0) {
            ::waitpid(p, nullptr, WNOHANG);
        }
    }

    for (auto *c : connections) {
        delete c;
    }

    ::sem_destroy(shared_sem);
    ::munmap(ptr, sizeof(sem_t));

    log_message("=== Chat server stopped ===");
    return 0;
}


