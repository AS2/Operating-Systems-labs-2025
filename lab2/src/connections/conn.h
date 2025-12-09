#pragma once

#include <sched.h>

#include <cstddef>
#include <filesystem>
#include <thread>

enum ConnectionType { PIPE, FIFO };

class IConn {
 public:
  virtual ~IConn(){};
  virtual bool write(void* buf, size_t size) = 0;
  virtual bool read(void* buf, size_t size) = 0;
};
