#include "conn_mq.h"
#include <cstring>
#include <cerrno>
#include <sys/time.h>
#include <time.h>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>

#ifndef MQ_INVALID_HANDLE
#define MQ_INVALID_HANDLE ((mqd_t)-1)
#endif

#ifdef __APPLE__
// Альтернативная реализация sem_timedwait для macOS
static int sem_timedwait_macos(sem_t *sem, const struct timespec *abs_timeout) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    
    while (now.tv_sec < abs_timeout->tv_sec || 
           (now.tv_sec == abs_timeout->tv_sec && now.tv_nsec < abs_timeout->tv_nsec)) {
        if (sem_trywait(sem) == 0) {
            return 0;
        }
        if (errno != EAGAIN) {
            return -1;
        }
        usleep(10000); // 10ms задержка
        clock_gettime(CLOCK_REALTIME, &now);
    }
    errno = ETIMEDOUT;
    return -1;
}
#define sem_timedwait sem_timedwait_macos
#endif

bool ConnMq::CreateResources(const std::string& name) {
    // Создаем очередь сообщений
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    // Размер сообщения должен быть достаточным для GameMessage (3 int = 12 байт, но округляем до 64 для выравнивания)
    attr.mq_msgsize = 64;
    attr.mq_curmsgs = 0;
    
    mqd_t mq = mq_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666, &attr);
    if (mq == MQ_INVALID_HANDLE && errno != EEXIST) {
        std::cerr << "[MQ] Ошибка создания очереди: " << strerror(errno) << std::endl;
        return false;
    }
    if (mq != MQ_INVALID_HANDLE) {
        mq_close(mq);
    }
    std::cout << "[MQ] Создана очередь сообщений: " << name << std::endl;
    
    // Создаем глобальные семафоры для синхронизации
    std::string sem_read_name = name + "_read";
    std::string sem_write_name = name + "_write";
    
    sem_t* sem_read = sem_open(sem_read_name.c_str(), O_CREAT | O_EXCL, 0666, 0);
    if (sem_read == SEM_FAILED && errno != EEXIST) {
        std::cerr << "[MQ] Ошибка создания sem_read: " << strerror(errno) << std::endl;
        return false;
    }
    if (sem_read != SEM_FAILED) {
        sem_close(sem_read);
    }
    
    sem_t* sem_write = sem_open(sem_write_name.c_str(), O_CREAT | O_EXCL, 0666, 1);
    if (sem_write == SEM_FAILED && errno != EEXIST) {
        std::cerr << "[MQ] Ошибка создания sem_write: " << strerror(errno) << std::endl;
        return false;
    }
    if (sem_write != SEM_FAILED) {
        sem_close(sem_write);
    }
    
    std::cout << "[MQ] Созданы ресурсы (очередь и семафоры)" << std::endl;
    return true;
}

ConnMq::ConnMq(const std::string& name, bool create) : mq(MQ_INVALID_HANDLE), 
    mq_name(name), sem_read(nullptr), sem_write(nullptr), is_creator(create) {
    
    if (create) {
        // Открываем очередь сообщений (ресурсы должны быть созданы заранее через CreateResources())
        mq = mq_open(name.c_str(), O_RDWR);
        if (mq == MQ_INVALID_HANDLE) {
            std::cerr << "[MQ] Ошибка открытия очереди: " << strerror(errno) << std::endl;
            return;
        }
        std::cout << "[MQ] Открыта очередь сообщений: " << name << std::endl;
        
        // Открываем существующие глобальные семафоры
        std::string sem_read_name = name + "_read";
        std::string sem_write_name = name + "_write";
        
        sem_read = sem_open(sem_read_name.c_str(), 0);
        if (sem_read == SEM_FAILED) {
            std::cerr << "[MQ] Ошибка открытия sem_read: " << strerror(errno) << std::endl;
        }
        
        sem_write = sem_open(sem_write_name.c_str(), 0);
        if (sem_write == SEM_FAILED) {
            std::cerr << "[MQ] Ошибка открытия sem_write: " << strerror(errno) << std::endl;
        }
        
        std::cout << "[MQ] Открыты очередь и семафоры" << std::endl;
    } else {
        // Открываем существующую очередь
        mq = mq_open(name.c_str(), O_RDWR);
        if (mq == MQ_INVALID_HANDLE) {
            std::cerr << "[MQ] Ошибка открытия очереди: " << strerror(errno) << std::endl;
            return;
        }
        std::cout << "[MQ] Открыта очередь сообщений: " << name << std::endl;
        
        // Открываем существующие семафоры
        std::string sem_read_name = name + "_read";
        std::string sem_write_name = name + "_write";
        
        sem_read = sem_open(sem_read_name.c_str(), 0);
        if (sem_read == SEM_FAILED) {
            std::cerr << "[MQ] Ошибка открытия sem_read: " << strerror(errno) << std::endl;
        }
        
        sem_write = sem_open(sem_write_name.c_str(), 0);
        if (sem_write == SEM_FAILED) {
            std::cerr << "[MQ] Ошибка открытия sem_write: " << strerror(errno) << std::endl;
        }
    }
}

ConnMq::~ConnMq() {
    if (mq != MQ_INVALID_HANDLE) {
        mq_close(mq);
        std::cout << "[MQ] Закрыта очередь сообщений: " << mq_name << std::endl;
    }
    
    if (is_creator) {
        if (mq != MQ_INVALID_HANDLE) {
            mq_unlink(mq_name.c_str());
            std::cout << "[MQ] Удалена очередь сообщений: " << mq_name << std::endl;
        }
        
        if (sem_read != SEM_FAILED && sem_read != nullptr) {
            std::string sem_read_name = mq_name + "_read";
            sem_close(sem_read);
            sem_unlink(sem_read_name.c_str());
        }
        
        if (sem_write != SEM_FAILED && sem_write != nullptr) {
            std::string sem_write_name = mq_name + "_write";
            sem_close(sem_write);
            sem_unlink(sem_write_name.c_str());
        }
    } else {
        if (sem_read != SEM_FAILED && sem_read != nullptr) {
            sem_close(sem_read);
        }
        if (sem_write != SEM_FAILED && sem_write != nullptr) {
            sem_close(sem_write);
        }
    }
}

bool ConnMq::Read(void *buf, size_t count) {
    if (mq == MQ_INVALID_HANDLE || !sem_read || !sem_write) {
        return false;
    }
    
    // Ожидаем семафор чтения с таймаутом 5 секунд
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;
    
    if (sem_timedwait(sem_read, &timeout) == -1) {
        if (errno == ETIMEDOUT) {
            std::cerr << "[MQ] Таймаут ожидания чтения" << std::endl;
        } else {
            std::cerr << "[MQ] Ошибка ожидания sem_read: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    // Читаем сообщение из очереди
    // Нужно использовать буфер размером не меньше mq_msgsize
    char read_buf[64];
    ssize_t received = mq_receive(mq, read_buf, sizeof(read_buf), nullptr);
    if (received == -1) {
        std::cerr << "[MQ] Ошибка чтения из очереди: " << strerror(errno) << std::endl;
        sem_post(sem_write);
        return false;
    }
    // Копируем данные в переданный буфер
    size_t copy_size = (count < (size_t)received) ? count : (size_t)received;
    memcpy(buf, read_buf, copy_size);
    
    // Освобождаем семафор записи
    sem_post(sem_write);
    
    return true;
}

bool ConnMq::Write(const void *buf, size_t count) {
    if (mq == MQ_INVALID_HANDLE || !sem_read || !sem_write) {
        return false;
    }
    
    // Ожидаем семафор записи с таймаутом 5 секунд
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;
    
    if (sem_timedwait(sem_write, &timeout) == -1) {
        if (errno == ETIMEDOUT) {
            std::cerr << "[MQ] Таймаут ожидания записи" << std::endl;
        } else {
            std::cerr << "[MQ] Ошибка ожидания sem_write: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    // Отправляем сообщение в очередь
    if (mq_send(mq, (const char*)buf, count, 0) == -1) {
        std::cerr << "[MQ] Ошибка записи в очередь: " << strerror(errno) << std::endl;
        sem_post(sem_write);
        return false;
    }
    
    // Освобождаем семафор чтения
    sem_post(sem_read);
    
    return true;
}
