#include "conn_seg.h"
#include <cstring>
#include <cerrno>
#include <sys/time.h>
#include <time.h>
#include <iostream>
#include <unistd.h>

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

ConnSeg::ConnSeg(key_t key, size_t size, bool create) : shmid(-1), shm_ptr(nullptr), 
    shm_size(size), sem_read(nullptr), sem_write(nullptr), is_creator(create) {
    
    if (create) {
        // Создаем сегмент разделяемой памяти
        shmid = shmget(key, size, IPC_CREAT | IPC_EXCL | 0666);
        if (shmid == -1) {
            std::cerr << "[SEG] Ошибка создания сегмента: " << strerror(errno) << std::endl;
            return;
        }
        std::cout << "[SEG] Создан сегмент разделяемой памяти, key=" << key << ", size=" << size << std::endl;
        
        // Присоединяем сегмент
        shm_ptr = shmat(shmid, nullptr, 0);
        if (shm_ptr == (void*)-1) {
            std::cerr << "[SEG] Ошибка присоединения сегмента: " << strerror(errno) << std::endl;
            shmctl(shmid, IPC_RMID, nullptr);
            shmid = -1;
            return;
        }
        
        // Создаем локальные семафоры в разделяемой памяти
        sem_read = (sem_t*)shm_ptr;
        sem_write = (sem_t*)((char*)shm_ptr + sizeof(sem_t));
        
        if (sem_init(sem_read, 1, 0) == -1) {
            std::cerr << "[SEG] Ошибка инициализации sem_read: " << strerror(errno) << std::endl;
        }
        if (sem_init(sem_write, 1, 1) == -1) {
            std::cerr << "[SEG] Ошибка инициализации sem_write: " << strerror(errno) << std::endl;
        }
        
        std::cout << "[SEG] Инициализированы семафоры" << std::endl;
    } else {
        // Присоединяемся к существующему сегменту
        shmid = shmget(key, 0, 0);
        if (shmid == -1) {
            std::cerr << "[SEG] Ошибка получения сегмента: " << strerror(errno) << std::endl;
            return;
        }
        std::cout << "[SEG] Присоединен к сегменту разделяемой памяти, key=" << key << std::endl;
        
        shm_ptr = shmat(shmid, nullptr, 0);
        if (shm_ptr == (void*)-1) {
            std::cerr << "[SEG] Ошибка присоединения сегмента: " << strerror(errno) << std::endl;
            shmid = -1;
            return;
        }
        
        sem_read = (sem_t*)shm_ptr;
        sem_write = (sem_t*)((char*)shm_ptr + sizeof(sem_t));
    }
}

ConnSeg::~ConnSeg() {
    if (shm_ptr != nullptr && shm_ptr != (void*)-1) {
        // Уничтожаем семафоры только если мы создатель
        if (is_creator && sem_read) {
            sem_destroy(sem_read);
        }
        if (is_creator && sem_write) {
            sem_destroy(sem_write);
        }
        
        shmdt(shm_ptr);
        std::cout << "[SEG] Отсоединен от сегмента разделяемой памяти" << std::endl;
    }
    
    // Удаляем сегмент только если мы создатель
    if (is_creator && shmid != -1) {
        shmctl(shmid, IPC_RMID, nullptr);
        std::cout << "[SEG] Удален сегмент разделяемой памяти" << std::endl;
    }
}

bool ConnSeg::Read(void *buf, size_t count) {
    if (shm_ptr == nullptr || shm_ptr == (void*)-1 || !sem_read || !sem_write) {
        return false;
    }
    
    // Ожидаем семафор чтения с таймаутом 5 секунд
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;
    
    if (sem_timedwait(sem_read, &timeout) == -1) {
        if (errno == ETIMEDOUT) {
            std::cerr << "[SEG] Таймаут ожидания чтения" << std::endl;
        } else {
            std::cerr << "[SEG] Ошибка ожидания sem_read: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    // Читаем данные из разделяемой памяти
    void* data_ptr = (char*)shm_ptr + 2 * sizeof(sem_t);
    size_t available = shm_size - 2 * sizeof(sem_t);
    size_t to_read = (count < available) ? count : available;
    memcpy(buf, data_ptr, to_read);
    
    // Освобождаем семафор записи
    sem_post(sem_write);
    
    return true;
}

bool ConnSeg::Write(const void *buf, size_t count) {
    if (shm_ptr == nullptr || shm_ptr == (void*)-1 || !sem_read || !sem_write) {
        return false;
    }
    
    // Ожидаем семафор записи с таймаутом 5 секунд
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;
    
    if (sem_timedwait(sem_write, &timeout) == -1) {
        if (errno == ETIMEDOUT) {
            std::cerr << "[SEG] Таймаут ожидания записи" << std::endl;
        } else {
            std::cerr << "[SEG] Ошибка ожидания sem_write: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    // Записываем данные в разделяемую память
    void* data_ptr = (char*)shm_ptr + 2 * sizeof(sem_t);
    size_t available = shm_size - 2 * sizeof(sem_t);
    size_t to_write = (count < available) ? count : available;
    memcpy(data_ptr, buf, to_write);
    
    // Освобождаем семафор чтения
    sem_post(sem_read);
    
    return true;
}
