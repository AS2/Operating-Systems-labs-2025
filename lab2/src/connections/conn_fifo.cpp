#include "conn_fifo.h"

#include <fcntl.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

ConnFifo::ConnFifo(pid_t host_pid) {
  if (!std::filesystem::exists("/tmp/lab2")) {
    std::filesystem::create_directory("/tmp/lab2");
  }

  mkfifo(m_fifo.c_str(), 0666);
  m_fd = open(m_fifo.c_str(), O_RDWR | O_NONBLOCK);
}

ConnFifo::~ConnFifo() {
  close(m_fd);
}

bool ConnFifo::write(void* buf, size_t size) {
  int fd = open(m_fifo.c_str(), O_WRONLY | O_NONBLOCK);
  if (fd == -1) return false;
  int res = ::write(fd, buf, size);
  return res > 0;
};

bool ConnFifo::read(void* buf, size_t size) {
  int fd = open(m_fifo.c_str(), O_RDONLY | O_NONBLOCK);
  if (fd == -1) return false;
  int res = ::read(fd, buf, size);
  return res > 0;
};
