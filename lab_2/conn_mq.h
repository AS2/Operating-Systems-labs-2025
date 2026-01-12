#pragma once

#include <mqueue.h>
#include <cstddef>

class ConnMq {
public:
    ConnMq(int id, bool create);
    bool Read(void *buf, std::size_t count);
    bool Write(const void *buf, std::size_t count);
    ~ConnMq();

private:
    int  id_{};
    bool creator_{false};
    mqd_t mq_read_{-1};
    mqd_t mq_write_{-1};
};


