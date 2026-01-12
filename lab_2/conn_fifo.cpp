#include "conn_fifo.h"
#include "message.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

extern volatile bool running;

namespace {

std::string path_c2h(int id) {
    return "/tmp/chat_c2h_" + std::to_string(id);
}

std::string path_h2c(int id) {
    return "/tmp/chat_h2c_" + std::to_string(id);
}

int open_with_retry(const std::string &path, int flags, int max_attempts) {
    for (int attempt = 0; attempt < max_attempts && running; ++attempt) {
        int fd = ::open(path.c_str(), flags);
        if (fd != -1) {
            return fd;
        }

        if (errno == ENOENT || errno == ENXIO) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::string msg = "open fifo " + path;
        std::perror(msg.c_str());
        return -1;
    }

    errno = ENXIO;
    return -1;
}

void set_blocking(int fd) {
    if (fd == -1) {
        return;
    }
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return;
    }
    flags &= ~O_NONBLOCK;
    ::fcntl(fd, F_SETFL, flags);
}

}

void ConnFifo::PrepareEndpoints(int id) {
    std::string c2h = path_c2h(id);
    std::string h2c = path_h2c(id);

    ::unlink(c2h.c_str());
    ::unlink(h2c.c_str());

    if (::mkfifo(c2h.c_str(), 0666) == -1 && errno != EEXIST) {
        std::perror(("mkfifo " + c2h).c_str());
    }
    if (::mkfifo(h2c.c_str(), 0666) == -1 && errno != EEXIST) {
        std::perror(("mkfifo " + h2c).c_str());
    }
}

ConnFifo::ConnFifo(int id, bool create)
    : id_(id)
    , creator_(create)
    , path_c2h_(path_c2h(id))
    , path_h2c_(path_h2c(id)) {
    constexpr int kAttempts = 100;

    if (creator_) {
        fd_read_ = open_with_retry(path_c2h_, O_RDONLY | O_NONBLOCK, kAttempts);
        fd_write_ = open_with_retry(path_h2c_, O_WRONLY | O_NONBLOCK, kAttempts);
    } else {
        fd_read_ = open_with_retry(path_h2c_, O_RDONLY | O_NONBLOCK, kAttempts);
        fd_write_ = open_with_retry(path_c2h_, O_WRONLY | O_NONBLOCK, kAttempts);
    }

    set_blocking(fd_write_);
}

bool ConnFifo::Read(void *buf, std::size_t cnt) {
    if (fd_read_ == -1) {
        return false;
    }

    std::size_t total_read = 0;
    auto *ptr = static_cast<char *>(buf);

    while (total_read < cnt) {
        pollfd pfd{};
        pfd.fd = fd_read_;
        pfd.events = POLLIN;

        int rv = ::poll(&pfd, 1, 500);
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (rv == 0) {
            if (!running) {
                return false;
            }
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            return false;
        }

        ssize_t n = ::read(fd_read_, ptr + total_read, cnt - total_read);
        if (n <= 0) {
            if (n == 0) {
                return false;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        total_read += static_cast<std::size_t>(n);
    }

    return total_read == cnt;
}

bool ConnFifo::Write(const void *buf, std::size_t cnt) {
    if (fd_write_ == -1) {
        return false;
    }

    std::size_t total_written = 0;
    auto *ptr = static_cast<const char *>(buf);

    while (total_written < cnt) {
        ssize_t n = ::write(fd_write_, ptr + total_written, cnt - total_written);
        if (n <= 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EPIPE) {
                return false;
            }
            return false;
        }
        total_written += static_cast<std::size_t>(n);
    }

    return total_written == cnt;
}

ConnFifo::~ConnFifo() {
    if (fd_read_ != -1) {
        ::close(fd_read_);
        fd_read_ = -1;
    }
    if (fd_write_ != -1) {
        ::close(fd_write_);
        fd_write_ = -1;
    }

    if (creator_) {
        ::unlink(path_c2h_.c_str());
        ::unlink(path_h2c_.c_str());
    }
}


