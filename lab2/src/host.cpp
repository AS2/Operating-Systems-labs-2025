#include "host.h"

#include <fcntl.h>
#include <linux/prctl.h>
#include <semaphore.h>
#include <sys/prctl.h>
#include <sys/syslog.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "client.h"
#include "conn.h"
#include "conn_pipe.h"

Host& Host::getInstance() {
  static Host instance;
  return instance;
}

Host::Host() {
  s_pid_host = getpid();
  m_sem_client = sem_open("/client", 0);
  if (m_sem_client == SEM_FAILED) {
    syslog(LOG_ERR, "ERROR: failed to open /client semaphore");
    exit(1);
  }

  m_sem_host = sem_open("/host", 0);
  if (m_sem_host == SEM_FAILED) {
    syslog(LOG_ERR, "ERROR: failed to open /host semaphore");
    exit(1);
  }

  // Should already exist; opened with read/write protection semaphore
  m_sem_write = sem_open("/sem_write", 0);
  if (m_sem_write == SEM_FAILED) {
    syslog(LOG_ERR, "ERROR: failed to open /sem_write semaphore");
    exit(1);
  }

  std::filesystem::remove_all("/tmp/lab2");
  if (!std::filesystem::exists("/tmp/lab2")) {
    std::filesystem::create_directory("/tmp/lab2");
  }
  std::ofstream in(m_term_in);
  std::ofstream out(m_term_out);

  std::signal(SIGTERM, [](int signum) {
    if (getpid() != s_pid_host) exit(0);
    for (auto& c : s_clients) {
      syslog(LOG_DEBUG, "%s",
             ("DEBUG: killing " + std::to_string(c.first)).c_str());
      kill(c.first, SIGTERM);
    }

    syslog(LOG_INFO, "INFO: Host terminating");
    exit(0);
  });

  signal(SIGCHLD, [](int) {
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
      if (pid == s_pid_terminal) {
        syslog(LOG_INFO, "INFO: host terminal closed, host terminating");
        kill(getpid(), SIGTERM);
      }

      syslog(LOG_DEBUG, "DEBUG: caught terminating child %d", pid);
      auto it = s_clients.find(pid);
      if (it != s_clients.end()) {
        delete it->second.client;
        s_clients.erase(it);
      }

      if (s_clients.size() == 0) {
        syslog(LOG_INFO, "INFO: No remaining clients, host terminating");
        exit(0);
      }
    }
  });
}

Host::~Host() {
  sem_close(m_sem_client);
  sem_close(m_sem_host);
  sem_close(m_sem_write);

  for (auto& client : s_clients) {
    delete client.second.client;
  }
}

void Host::create_client(ConnectionType id) {
  syslog(LOG_INFO, "INFO: Creating connection...");
  Client* client = new Client(id);

  client->run();

  if (getpid() == s_pid_host) {
    s_clients[client->m_pid_client] =
        ClientInfo{client, std::chrono::steady_clock::now()};
  }
}

namespace {
// Формат личного сообщения: "@<pid> <text>"
std::optional<std::pair<pid_t, std::string>> parse_private(
    const std::string& line) {
  if (line.size() < 2 || line[0] != '@') return std::nullopt;
  size_t space = line.find(' ');
  if (space == std::string::npos) return std::nullopt;
  std::string pid_str = line.substr(1, space - 1);
  try {
    pid_t target = static_cast<pid_t>(std::stol(pid_str));
    std::string payload = line.substr(space + 1);
    if (payload.empty()) return std::nullopt;
    return std::make_pair(target, payload);
  } catch (...) {
    return std::nullopt;
  }
}

void send_to_client(pid_t pid, const std::string& msg) {
  auto it = Host::s_clients.find(pid);
  if (it == Host::s_clients.end()) return;
  char buf[1000] = {0};
  std::snprintf(buf, sizeof(buf), "%s", msg.c_str());
  it->second.client->m_conn->write(buf, sizeof(buf));
}
}  // namespace

void Host::run() {
  fork_terminal();
  std::fstream term(m_term_out);
  term << "You are in host terminal" << std::endl;

  const int buf_size = 1000;
  char buf[buf_size];
  while (true) {
    sem_wait(m_sem_client);
    syslog(LOG_DEBUG, "DEBUG: host aquired client-read semaphore");
    for (auto& c : s_clients) {
      syslog(LOG_DEBUG, "DEBUG: host reading %d", c.first);
      if (c.second.client->m_conn->read(buf, buf_size)) {
        syslog(LOG_DEBUG, "DEBUG: host read \"%s\" from %d", buf, c.first);
        term << "[" << c.first << "]: " << buf << std::endl;
        c.second.last_activity = std::chrono::steady_clock::now();

        std::string msg(buf);
        auto p = parse_private(msg);
        if (p) {
          pid_t target = p->first;
          std::string payload =
              "[pm " + std::to_string(c.first) + "]: " + p->second;
          send_to_client(target, payload);
          send_to_client(c.first, payload);
          if (s_clients.count(target)) sem_post(m_sem_host);
          if (target != c.first) sem_post(m_sem_host);
        } else {
          std::string payload = "[" + std::to_string(c.first) + "]: " + msg;
          for (auto& other : s_clients) {
            other.second.client->m_conn->write(payload.data(), buf_size);
            syslog(LOG_DEBUG, "DEBUG: host broadcast \"%s\" to client[%d]",
                   payload.c_str(), other.first);
          }
          for (int i = 0; i < static_cast<int>(s_clients.size()); i++) {
            sem_post(m_sem_host);
          }
        }
      } else {
        syslog(LOG_DEBUG, "DEBUG: host read nothing from %d", c.first);
      }
    }

    // Контроль таймаута неактивных клиентов
    const auto now = std::chrono::steady_clock::now();
    for (auto it = s_clients.begin(); it != s_clients.end();) {
      auto& info = it->second;
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         now - info.last_activity)
                         .count();
      if (elapsed > 60) {
        syslog(LOG_INFO, "INFO: killing inactive client %d", it->first);
        kill(it->first, SIGKILL);
        delete info.client;
        it = s_clients.erase(it);
        continue;
      }
      ++it;
    }
  }
}

void Host::open_terminal() {
  prctl(PR_SET_PDEATHSIG, SIGTERM);
  int res = execl("/usr/bin/xterm", "xterm", "-e",
                  ("bash -c \"cp /dev/stdin " + m_term_in.string() +
                   " | tail -f " + m_term_out.string() + "\"")
                      .c_str(),
                  (char*)NULL);
  if (res < 0) {
    syslog(LOG_ERR, "ERROR: in execl(), client terminating");
    exit(1);
  }
}

void Host::fork_terminal() {
  switch (pid_t pid = fork()) {
    case -1:
      syslog(LOG_ERR, "ERROR: in fork(), host terminating");
      exit(1);
      break;
    case 0:
      s_pid_terminal = getpid();
      break;
    default:
      s_pid_terminal = pid;
      return;
  }

  prctl(PR_SET_NAME, "host_term_listener");
  prctl(PR_SET_PDEATHSIG, SIGTERM);

  switch (fork()) {
    case -1:
      syslog(LOG_ERR, "ERROR: in fork(), host terminating");
      exit(1);
      break;
    case 0:
      open_terminal();
      break;
  }

  std::signal(SIGCHLD, [](int) {
    syslog(LOG_INFO, "INFO: host terminal closed => host listener terminating");
    kill(getpid(), SIGKILL);
  });

  std::ifstream tin(m_term_in);
  const int buf_size = 1000;
  char buf[buf_size];
  std::string line;
  while (true) {
    if (std::getline(tin, line) && !line.empty()) {
      sem_wait(m_sem_write);
      syslog(LOG_DEBUG, "DEBUG: host aquired write semaphore");
      strcpy(buf, line.c_str());
      std::string msg(buf);
      auto p = parse_private(msg);
      if (p) {
        pid_t target = p->first;
        std::string payload = "[pm host]: " + p->second;
        send_to_client(target, payload);
        if (s_clients.count(target)) sem_post(m_sem_host);
        syslog(LOG_INFO, "INFO: host sent pm to %d: %s", target,
               p->second.c_str());
      } else {
        std::string payload = "[host]: " + msg;
        for (auto& c : s_clients) {
          c.second.client->m_conn->write(payload.data(), buf_size);
          syslog(LOG_DEBUG, "DEBUG: host wrote \"%s\" to client[%d]",
                 payload.c_str(), c.first);
        }
        for (int i = 0; i < static_cast<int>(s_clients.size()); i++) {
          sem_post(m_sem_host);
        }
      }
      sem_post(m_sem_write);
      syslog(LOG_DEBUG, "DEBUG: host released write semaphore");
    }
    tin.clear();
  }
}

void open_semaphores(sem_t*& sem_host, sem_t*& sem_client, sem_t*& sem_write) {
  sem_unlink("/host");
  sem_host =
      sem_open("/host", O_CREAT | O_EXCL, S_IRWXU | S_IRWXG | S_IRWXO, 0);
  if (sem_host == SEM_FAILED) {
    std::cerr << "sem_open() host failed: " << errno << std::endl;
    exit(1);
  }

  sem_unlink("/client");
  sem_client =
      sem_open("/client", O_CREAT | O_EXCL, S_IRWXU | S_IRWXG | S_IRWXO, 0);
  if (sem_client == SEM_FAILED) {
    sem_close(sem_host);
    std::cerr << "sem_open() client failed: " << errno << std::endl;
    exit(1);
  }

  sem_unlink("/sem_write");
  sem_write =
      sem_open("/sem_write", O_CREAT | O_EXCL, S_IRWXU | S_IRWXG | S_IRWXO, 1);
  if (sem_write == SEM_FAILED) {
    sem_close(sem_client);
    sem_close(sem_host);
    std::cerr << "sem_open() write failed: " << errno << std::endl;
    exit(1);
  }
}

auto main() -> int {
  openlog("host", LOG_NDELAY | LOG_PID, LOG_DAEMON);
  sem_t *sem_host, *sem_client, *sem_write;
  open_semaphores(sem_host, sem_client, sem_write);

  syslog(LOG_INFO, "INFO: host starting...");
  Host& host = Host::getInstance();
  host.create_client(ConnectionType::PIPE);
  // std::this_thread::sleep_for(std::chrono::seconds(5));
  host.create_client(ConnectionType::FIFO);

  host.run();
  sem_close(sem_host);
  sem_close(sem_client);
  sem_close(sem_write);
  sem_unlink("/host");
  sem_unlink("/client");
  sem_unlink("/sem_write");
  syslog(LOG_INFO, "INFO: Timeout");
  closelog();
}
