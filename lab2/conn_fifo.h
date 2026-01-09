#ifndef CONN_FIFO_H
#define CONN_FIFO_H

#include "conn_base.h"
#include <semaphore.h>
#include <string>

// Класс для работы с именованными каналами через mkfifo
class ConnFifo : public Conn {
private:
    int fd_read;
    int fd_write;
    std::string fifo_name_read;
    std::string fifo_name_write;
    std::string base_name;  // Исходное имя для формирования имен семафоров
    sem_t* sem_read;
    sem_t* sem_write;
    bool is_creator;

public:
    ConnFifo(const std::string& name, bool create);
    ~ConnFifo();
    bool Read(void *buf, size_t count) override;
    bool Write(const void *buf, size_t count) override;
    
    // Статический метод для создания ресурсов без открытия (для использования до fork)
    static bool CreateResources(const std::string& name);
};

#endif // CONN_FIFO_H
