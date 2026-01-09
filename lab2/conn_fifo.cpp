#include "conn_fifo.h"
#include <cstring>
#include <cerrno>
#include <sys/time.h>
#include <time.h>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>

bool ConnFifo::CreateResources(const std::string& name) {
    std::string fifo_name_read = name + "_read";
    std::string fifo_name_write = name + "_write";
    
    // Создаем именованные каналы
    if (mkfifo(fifo_name_read.c_str(), 0666) == -1 && errno != EEXIST) {
        std::cerr << "[FIFO] Ошибка создания канала чтения: " << strerror(errno) << std::endl;
        return false;
    }
    std::cout << "[FIFO] Создан канал чтения: " << fifo_name_read << std::endl;
    
    if (mkfifo(fifo_name_write.c_str(), 0666) == -1 && errno != EEXIST) {
        std::cerr << "[FIFO] Ошибка создания канала записи: " << strerror(errno) << std::endl;
        return false;
    }
    std::cout << "[FIFO] Создан канал записи: " << fifo_name_write << std::endl;
    
    // Создаем глобальные семафоры для синхронизации
    // Имена семафоров должны начинаться с "/" и не содержать других "/"
    // Заменяем "/" на "_" в имени
    std::string sem_base_name = name;
    std::replace(sem_base_name.begin(), sem_base_name.end(), '/', '_');
    std::string sem_read_name = "/" + sem_base_name + "_read_sem";
    std::string sem_write_name = "/" + sem_base_name + "_write_sem";
    
    sem_t* sem_read = sem_open(sem_read_name.c_str(), O_CREAT | O_EXCL, 0666, 0);
    if (sem_read == SEM_FAILED && errno != EEXIST) {
        std::cerr << "[FIFO] Ошибка создания sem_read: " << strerror(errno) << std::endl;
        return false;
    }
    if (sem_read != SEM_FAILED) {
        sem_close(sem_read);
    }
    
    sem_t* sem_write = sem_open(sem_write_name.c_str(), O_CREAT | O_EXCL, 0666, 1);
    if (sem_write == SEM_FAILED && errno != EEXIST) {
        std::cerr << "[FIFO] Ошибка создания sem_write: " << strerror(errno) << std::endl;
        return false;
    }
    if (sem_write != SEM_FAILED) {
        sem_close(sem_write);
    }
    
    std::cout << "[FIFO] Созданы ресурсы (каналы и семафоры)" << std::endl;
    return true;
}

ConnFifo::ConnFifo(const std::string& name, bool create) : fd_read(-1), fd_write(-1),
    fifo_name_read(name + "_read"), fifo_name_write(name + "_write"),
    base_name(name), sem_read(nullptr), sem_write(nullptr), is_creator(create) {
    
    if (create) {
        // Открываем каналы (хост открывает для записи в read и чтения из write)
        // Ресурсы должны быть созданы заранее через CreateResources()
        fd_write = open(fifo_name_read.c_str(), O_WRONLY);
        if (fd_write == -1) {
            std::cerr << "[FIFO] Ошибка открытия канала записи: " << strerror(errno) << std::endl;
            return;
        }
        
        fd_read = open(fifo_name_write.c_str(), O_RDONLY);
        if (fd_read == -1) {
            std::cerr << "[FIFO] Ошибка открытия канала чтения: " << strerror(errno) << std::endl;
            return;
        }
        
        // Открываем существующие глобальные семафоры
        std::string sem_base_name = name;
        std::replace(sem_base_name.begin(), sem_base_name.end(), '/', '_');
        std::string sem_read_name = "/" + sem_base_name + "_read_sem";
        std::string sem_write_name = "/" + sem_base_name + "_write_sem";
        
        sem_read = sem_open(sem_read_name.c_str(), 0);
        if (sem_read == SEM_FAILED) {
            std::cerr << "[FIFO] Ошибка открытия sem_read: " << strerror(errno) << std::endl;
        }
        
        sem_write = sem_open(sem_write_name.c_str(), 0);
        if (sem_write == SEM_FAILED) {
            std::cerr << "[FIFO] Ошибка открытия sem_write: " << strerror(errno) << std::endl;
        }
        
        std::cout << "[FIFO] Открыты каналы и семафоры" << std::endl;
    } else {
        // Клиент открывает каналы в обратном порядке
        fd_read = open(fifo_name_read.c_str(), O_RDONLY);
        if (fd_read == -1) {
            std::cerr << "[FIFO] Ошибка открытия канала чтения: " << strerror(errno) << std::endl;
            return;
        }
        std::cout << "[FIFO] Открыт канал чтения: " << fifo_name_read << std::endl;
        
        fd_write = open(fifo_name_write.c_str(), O_WRONLY);
        if (fd_write == -1) {
            std::cerr << "[FIFO] Ошибка открытия канала записи: " << strerror(errno) << std::endl;
            return;
        }
        std::cout << "[FIFO] Открыт канал записи: " << fifo_name_write << std::endl;
        
        // Открываем существующие семафоры
        std::string sem_base_name = name;
        std::replace(sem_base_name.begin(), sem_base_name.end(), '/', '_');
        std::string sem_read_name = "/" + sem_base_name + "_read_sem";
        std::string sem_write_name = "/" + sem_base_name + "_write_sem";
        
        sem_read = sem_open(sem_read_name.c_str(), 0);
        if (sem_read == SEM_FAILED) {
            std::cerr << "[FIFO] Ошибка открытия sem_read: " << strerror(errno) << std::endl;
        }
        
        sem_write = sem_open(sem_write_name.c_str(), 0);
        if (sem_write == SEM_FAILED) {
            std::cerr << "[FIFO] Ошибка открытия sem_write: " << strerror(errno) << std::endl;
        }
    }
}

ConnFifo::~ConnFifo() {
    if (fd_read != -1) {
        close(fd_read);
        std::cout << "[FIFO] Закрыт канал чтения" << std::endl;
    }
    
    if (fd_write != -1) {
        close(fd_write);
        std::cout << "[FIFO] Закрыт канал записи" << std::endl;
    }
    
    if (is_creator) {
        // Удаляем именованные каналы
        unlink(fifo_name_read.c_str());
        unlink(fifo_name_write.c_str());
        std::cout << "[FIFO] Удалены именованные каналы" << std::endl;
        
        if (sem_read != SEM_FAILED && sem_read != nullptr) {
            std::string sem_base_name = base_name;
            std::replace(sem_base_name.begin(), sem_base_name.end(), '/', '_');
            std::string sem_read_name = "/" + sem_base_name + "_read_sem";
            sem_close(sem_read);
            sem_unlink(sem_read_name.c_str());
        }
        
        if (sem_write != SEM_FAILED && sem_write != nullptr) {
            std::string sem_base_name = base_name;
            std::replace(sem_base_name.begin(), sem_base_name.end(), '/', '_');
            std::string sem_write_name = "/" + sem_base_name + "_write_sem";
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

bool ConnFifo::Read(void *buf, size_t count) {
    if (fd_read == -1 || !sem_read || !sem_write) {
        return false;
    }
    
    // Ожидаем семафор чтения с таймаутом 5 секунд
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;
    
    if (sem_timedwait(sem_read, &timeout) == -1) {
        if (errno == ETIMEDOUT) {
            std::cerr << "[FIFO] Таймаут ожидания чтения" << std::endl;
        } else {
            std::cerr << "[FIFO] Ошибка ожидания sem_read: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    // Читаем данные из канала
    ssize_t received = read(fd_read, buf, count);
    if (received == -1) {
        std::cerr << "[FIFO] Ошибка чтения из канала: " << strerror(errno) << std::endl;
        sem_post(sem_write);
        return false;
    }
    
    // Освобождаем семафор записи
    sem_post(sem_write);
    
    return received > 0;
}

bool ConnFifo::Write(const void *buf, size_t count) {
    if (fd_write == -1 || !sem_read || !sem_write) {
        return false;
    }
    
    // Ожидаем семафор записи с таймаутом 5 секунд
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;
    
    if (sem_timedwait(sem_write, &timeout) == -1) {
        if (errno == ETIMEDOUT) {
            std::cerr << "[FIFO] Таймаут ожидания записи" << std::endl;
        } else {
            std::cerr << "[FIFO] Ошибка ожидания sem_write: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    // Записываем данные в канал
    ssize_t written = write(fd_write, buf, count);
    if (written == -1) {
        std::cerr << "[FIFO] Ошибка записи в канал: " << strerror(errno) << std::endl;
        sem_post(sem_write);
        return false;
    }
    
    // Освобождаем семафор чтения
    sem_post(sem_read);
    
    return written == (ssize_t)count;
}
