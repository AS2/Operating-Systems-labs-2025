#ifndef CONN_BASE_H
#define CONN_BASE_H

#include <cstddef>

// Базовый интерфейс для всех типов соединений
class Conn {
public:
    virtual ~Conn() = default;
    virtual bool Read(void *buf, size_t count) = 0;
    virtual bool Write(const void *buf, size_t count) = 0;
};

#endif // CONN_BASE_H
