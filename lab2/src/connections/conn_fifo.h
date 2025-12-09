#pragma once

#include <sched.h>

#include <cstddef>

#include "conn.h"

class ConnFifo : public IConn {
 public:
  ConnFifo() = delete;
  ConnFifo(pid_t host_pid);
  ~ConnFifo();

  bool read(void* buf, size_t size) override;
  bool write(void* buf, size_t size) override;

 private:
  const std::filesystem::path m_fifo = "/tmp/lab2/fifo";
  int m_fd;
};
