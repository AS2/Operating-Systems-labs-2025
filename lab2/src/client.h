#pragma once

#include <semaphore.h>

#include <memory>
#include <filesystem>

#include "conn.h"

class Client {
 public:
  Client(ConnectionType connection_type);
  Client() = delete;
  ~Client();

  void run();

  pid_t m_pid_client;
  std::unique_ptr<IConn> m_conn;

 private:
  pid_t m_pid_host;
  sem_t* m_sem_client;
  sem_t* m_sem_host;
  sem_t* m_sem_write;
  ConnectionType m_conn_type;
  std::filesystem::path m_term_stdin;
  std::filesystem::path m_term_stdout;

  void open_term();
  void fork_term_listener();
};
