#include "conn_mq.h"
#include "message.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <ctime>
#include <cerrno>
#include <cstdio>
#include <string>
#include <unistd.h>

extern volatile bool running;

ConnMq::ConnMq(int id, bool create)
    : id_(id)
    , creator_(create) {
    std::string from = "/mq_from_" + std::to_string(id);
    std::string to   = "/mq_to_"   + std::to_string(id);

    mq_attr attr{};
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MSG_SIZE;
    attr.mq_flags = 0;

    if (creator_) {
        mq_unlink(from.c_str());
        mq_unlink(to.c_str());

        mq_read_  = mq_open(from.c_str(), O_CREAT | O_RDWR, 0666, &attr);
        mq_write_ = mq_open(to.c_str(),   O_CREAT | O_RDWR, 0666, &attr);
        if (mq_read_ == static_cast<mqd_t>(-1) || mq_write_ == static_cast<mqd_t>(-1)) {
            std::perror("mq_open (creator)");
        }
    } else {
        mq_write_ = mq_open(from.c_str(), O_CREAT | O_RDWR, 0666, &attr);
        mq_read_  = mq_open(to.c_str(),   O_CREAT | O_RDWR, 0666, &attr);
        if (mq_read_ == static_cast<mqd_t>(-1) || mq_write_ == static_cast<mqd_t>(-1)) {
            std::perror("mq_open (client)");
        }
    }
}

bool ConnMq::Read(void *buf, std::size_t cnt) {
    if (cnt != MSG_SIZE || mq_read_ == static_cast<mqd_t>(-1)) {
        return false;
    }

    while (true) {
        timespec ts{};
        ::clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        ssize_t received = ::mq_timedreceive(
            mq_read_,
            static_cast<char *>(buf),
            MSG_SIZE,
            nullptr,
            &ts
        );

        if (received >= 0) {
            return static_cast<std::size_t>(received) == MSG_SIZE;
        }

        if (errno == ETIMEDOUT) {
            if (!running) {
                return false;
            }
            continue;
        }
        return false;
    }
}

bool ConnMq::Write(const void *buf, std::size_t cnt) {
    if (cnt != MSG_SIZE || mq_write_ == static_cast<mqd_t>(-1)) {
        return false;
    }

    timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    int res = ::mq_timedsend(
        mq_write_,
        static_cast<const char *>(buf),
        MSG_SIZE,
        0,
        &ts
    );

    return res == 0;
}

ConnMq::~ConnMq() {
    if (mq_read_ != static_cast<mqd_t>(-1)) {
        ::mq_close(mq_read_);
    }
    if (mq_write_ != static_cast<mqd_t>(-1)) {
        ::mq_close(mq_write_);
    }
    if (creator_) {
        std::string from = "/mq_from_" + std::to_string(id_);
        std::string to   = "/mq_to_"   + std::to_string(id_);
        mq_unlink(from.c_str());
        mq_unlink(to.c_str());
    }
}


