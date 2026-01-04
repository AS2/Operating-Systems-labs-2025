#include "conn_shm.h"
#include "message.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <string>

extern volatile bool running;

struct ConnShm::ShmBlock {
    sem_t sem_msg_from_client;
    sem_t sem_space_for_client;
    Message msg_from_client;

    sem_t sem_msg_from_host;
    sem_t sem_space_for_host;
    Message msg_from_host;
};

namespace {
bool wait_with_timeout(sem_t *sem, int timeout_sec) {
    while (true) {
        timespec ts{};
        ::clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_sec;

        int res = ::sem_timedwait(sem, &ts);
        if (res == 0) {
            return true;
        }
        if (errno == ETIMEDOUT) {
            if (!running) {
                return false;
            }
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
}
}

ConnShm::ConnShm(int id, bool create)
    : id_(id)
    , creator_(create) {
    std::string name = "/shm_chat_" + std::to_string(id);

    if (creator_) {
        ::shm_unlink(name.c_str());

        fd_ = ::shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd_ == -1) {
            std::perror("shm_open (creator)");
            return;
        }
        if (::ftruncate(fd_, static_cast<off_t>(sizeof(ShmBlock))) == -1) {
            std::perror("ftruncate (creator)");
            ::close(fd_);
            fd_ = -1;
            return;
        }
    } else {
        for (int retry = 0; retry < 50 && fd_ == -1; ++retry) {
            fd_ = ::shm_open(name.c_str(), O_RDWR, 0);
            if (fd_ != -1) {
                break;
            }
            ::usleep(100000);
        }
        if (fd_ == -1) {
            std::perror("shm_open (client)");
            return;
        }
    }

    void *addr = ::mmap(nullptr,
                        sizeof(ShmBlock),
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        fd_,
                        0);
    if (addr == MAP_FAILED) {
        std::perror("mmap ShmBlock");
        ::close(fd_);
        fd_ = -1;
        return;
    }

    block_ = static_cast<ShmBlock *>(addr);

    if (creator_) {
        if (::sem_init(&block_->sem_msg_from_client, 1, 0) != 0 ||
            ::sem_init(&block_->sem_space_for_client, 1, 1) != 0 ||
            ::sem_init(&block_->sem_msg_from_host, 1, 0) != 0 ||
            ::sem_init(&block_->sem_space_for_host, 1, 1) != 0) {
            std::perror("sem_init in ShmBlock");
        }

        std::memset(&block_->msg_from_client, 0, sizeof(Message));
        std::memset(&block_->msg_from_host,   0, sizeof(Message));
    }
}

bool ConnShm::Read(void *buf, std::size_t cnt) {
    if (!block_ || cnt != MSG_SIZE) {
        return false;
    }

    if (creator_) {
        if (!wait_with_timeout(&block_->sem_msg_from_client, 5)) {
            return false;
        }
        std::memcpy(buf, &block_->msg_from_client, MSG_SIZE);
        ::sem_post(&block_->sem_space_for_client);
    } else {
        if (!wait_with_timeout(&block_->sem_msg_from_host, 5)) {
            return false;
        }
        std::memcpy(buf, &block_->msg_from_host, MSG_SIZE);
        ::sem_post(&block_->sem_space_for_host);
    }

    return true;
}

bool ConnShm::Write(const void *buf, std::size_t cnt) {
    if (!block_ || cnt != MSG_SIZE) {
        return false;
    }

    if (creator_) {
        if (!wait_with_timeout(&block_->sem_space_for_host, 5)) {
            return false;
        }
        std::memcpy(&block_->msg_from_host, buf, MSG_SIZE);
        ::sem_post(&block_->sem_msg_from_host);
    } else {
        if (!wait_with_timeout(&block_->sem_space_for_client, 5)) {
            return false;
        }
        std::memcpy(&block_->msg_from_client, buf, MSG_SIZE);
        ::sem_post(&block_->sem_msg_from_client);
    }

    return true;
}

ConnShm::~ConnShm() {
    if (block_) {
        if (creator_) {
            ::sem_destroy(&block_->sem_msg_from_client);
            ::sem_destroy(&block_->sem_space_for_client);
            ::sem_destroy(&block_->sem_msg_from_host);
            ::sem_destroy(&block_->sem_space_for_host);
        }
        ::munmap(block_, sizeof(ShmBlock));
        block_ = nullptr;
    }

    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }

    if (creator_) {
        std::string name = "/shm_chat_" + std::to_string(id_);
        ::shm_unlink(name.c_str());
    }
}


