#ifndef CONN_MQ_H
#define CONN_MQ_H

#include "conn_base.h"
#include <mqueue.h>
#include <semaphore.h>
#include <string>

// Класс для работы с очередями сообщений через mq_open
class ConnMq : public Conn {
private:
    mqd_t mq;
    std::string mq_name;
    sem_t* sem_read;
    sem_t* sem_write;
    bool is_creator;

public:
    ConnMq(const std::string& name, bool create);
    ~ConnMq();
    bool Read(void *buf, size_t count) override;
    bool Write(const void *buf, size_t count) override;
    
    // Статический метод для создания ресурсов без открытия (для использования до fork)
    static bool CreateResources(const std::string& name);
};

#endif // CONN_MQ_H
