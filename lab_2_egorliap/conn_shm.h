#pragma once

#include <semaphore.h>
#include <cstddef>

class ConnShm {
public:
    ConnShm(int id, bool create);
    bool Read(void *buf, std::size_t count);
    bool Write(const void *buf, std::size_t count);
    ~ConnShm();

private:
    struct ShmBlock;

    int  id_{};
    bool creator_{false};
    int  fd_{-1};
    ShmBlock *block_{nullptr};
};


