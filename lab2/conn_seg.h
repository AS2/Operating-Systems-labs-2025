#ifndef CONN_SEG_H
#define CONN_SEG_H

#include "conn_base.h"
#include <sys/shm.h>
#include <semaphore.h>

// Класс для работы с общей памятью через shmget
class ConnSeg : public Conn {
private:
    int shmid;
    void* shm_ptr;
    size_t shm_size;
    sem_t* sem_read;
    sem_t* sem_write;
    bool is_creator;

public:
    ConnSeg(key_t key, size_t size, bool create);
    ~ConnSeg();
    bool Read(void *buf, size_t count) override;
    bool Write(const void *buf, size_t count) override;
};

#endif // CONN_SEG_H
