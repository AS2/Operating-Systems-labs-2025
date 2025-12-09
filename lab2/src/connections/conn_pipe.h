#pragma once

#include <sched.h>

#include <cstddef>

#include "conn.h"

class ConnPipe : public IConn {
 public:
  ConnPipe() = delete;
  ConnPipe(pid_t host_pid);
  ~ConnPipe();
  bool read(void* buf, size_t size) override;
  bool write(void* buf, size_t size) override;

 private:
  int m_desc[2];
};
