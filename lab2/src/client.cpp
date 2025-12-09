#include "client.h"

#include <fcntl.h>
#include <linux/prctl.h>
#include <semaphore.h>
#include <sys/prctl.h>
#include <sys/syslog.h>
#include <unistd.h>

#include <chrono>
#include <climits>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "conn.h"
#include "conn_fifo.h"
#include "conn_pipe.h"

Client::Client(ConnectionType conn_type) {
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

  m_sem_write = sem_open("/sem_write", 0);
  if (m_sem_write == SEM_FAILED) {
    syslog(LOG_ERR, "ERROR: failed to open /sem_write semaphore");
    exit(1);
  }

  m_pid_host = getpid();
  m_conn_type = conn_type;
  switch (conn_type) {
    case ConnectionType::PIPE:
      m_conn = std::make_unique<ConnPipe>(m_pid_host);
      break;
    case ConnectionType::FIFO:
      m_conn = std::make_unique<ConnFifo>(m_pid_host);
      break;
    default:
      throw std::runtime_error("Unsupported connection type");
  }

  pid_t pid;
  switch (pid = fork()) {
    case -1:
      syslog(LOG_ERR, "ERROR: unable to fork(), host terminating");
      exit(1);
    case 0:
      prctl(PR_SET_NAME, "client");
      syslog(LOG_INFO, "INFO: client started");
      m_pid_client = getpid();
      break;
    default:
      m_pid_client = pid;
      return;
  }

  m_term_stdin = "/tmp/lab2/" + std::to_string(getpid()) + ".in";
  m_term_stdout = "/tmp/lab2/" + std::to_string(getpid()) + ".out";
  std::ofstream in(m_term_stdin);
  std::ofstream out(m_term_stdout);
}

Client::~Client() {
  sem_close(m_sem_client);
  sem_close(m_sem_host);
  sem_close(m_sem_write);
}

void Client::run() {
  if (getpid() != m_pid_client) return;

  fork_term_listener();
  syslog(LOG_INFO, "INFO: created terminal for client");

  std::string conn;
  switch (m_conn_type) {
    case ConnectionType::PIPE:
      conn = "PIPE";
      break;
    case ConnectionType::FIFO:
      conn = "FIFO";
      break;
    default:
      break;
  }

  std::ofstream term(m_term_stdout);
  term << "You are in child process " + std::to_string(m_pid_client) +
              " connection type: \"" + conn + "\". Type something:"
       << std::endl;

  char buf[1000];
  while (true) {
    sem_wait(m_sem_host);
    syslog(LOG_DEBUG, "DEBUG: client aquired host-read semaphore");
    syslog(LOG_DEBUG, "DEBUG: client reading host message");
    if (m_conn->read(buf, 1000)) {
      syslog(LOG_DEBUG, "DEBUG: client[%d] read \"%s\" from host", m_pid_client,
             buf);
      term << "[host]: " << buf << std::endl;
    } else {
      syslog(LOG_DEBUG, "DEBUG: client[%d] read nothing from host",
             m_pid_client);
    }

    int val;
    sem_getvalue(m_sem_host, &val);
    while (val != 0) {
      sem_getvalue(m_sem_host, &val);
    }
  }
}

void Client::open_term() {
  prctl(PR_SET_PDEATHSIG, SIGTERM);
  int res = execl("/usr/bin/xterm", "xterm", "-e",
                  ("bash -c \"cp /dev/stdin " + m_term_stdin.string() +
                   " | tail -f " + m_term_stdout.string() + "\"")
                      .c_str(),
                  (char*)NULL);
  if (res < 0) {
    syslog(LOG_ERR, "ERROR: in execl(), client terminating");
    exit(1);
  }
}

void Client::fork_term_listener() {
  switch (fork()) {
    case -1:
      syslog(LOG_ERR, "ERROR: in fork(), client terminating");
      exit(1);
      break;
    case 0:
      break;
    default:
      std::signal(SIGCHLD, [](int) {
        syslog(LOG_INFO, "INFO: listener terminated => client terminating");
        exit(0);
      });
      return;
      break;
  }

  prctl(PR_SET_NAME, "term_listener");
  prctl(PR_SET_PDEATHSIG, SIGTERM);

  switch (fork()) {
    case -1:
      syslog(LOG_ERR, "ERROR: in fork(), client terminating");
      exit(1);
      break;
    case 0:
      open_term();
      break;
  }

  std::signal(SIGCHLD, [](int) {
    syslog(LOG_INFO, "INFO: terminal closed => listener terminating");
    kill(getpid(), SIGKILL);
  });

  std::ifstream tin(m_term_stdin);
  const int buf_size = 1000;
  char buf[buf_size];
  std::string line;
  while (true) {
    if (std::getline(tin, line) && line.size() != 0) {
      strcpy(buf, line.c_str());
      sem_wait(m_sem_write);
      syslog(LOG_DEBUG, "DEBUG: client aquired write semaphore");

      m_conn->write(buf, buf_size);
      syslog(LOG_DEBUG, "DEBUG: client[%d] wrote \"%s\" to host", m_pid_client,
             buf);
      sem_post(m_sem_client);
      sem_post(m_sem_write);
      syslog(LOG_DEBUG, "DEBUG: client released write semaphore");
    }
    tin.clear();
  }
}
