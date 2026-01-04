cmake_minimum_required(VERSION 3.10)
project(chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -lrt -Wall -Werror")

# Вариант 19: Независимые. Один ко многим, типы взаимодействия:
# 3. Общие файлы (shmopen + mmap)  TYPE_CODE shm
# 4. Очереди сообщений (mq_open)   TYPE_CODE mq
# 6. Именованные каналы (mkfifo)   TYPE_CODE fifo
#
# В соответствии с методичкой:
#   host.cpp содержит main хоста;
#   для каждого типа взаимодействия собираются отдельные host_* исполняемые файлы.

add_executable(host_shm
    host.cpp
    conn_shm.cpp
    conn_mq.cpp
    conn_fifo.cpp
)
target_compile_definitions(host_shm PRIVATE SHM)

add_executable(host_mq
    host.cpp
    conn_shm.cpp
    conn_mq.cpp
    conn_fifo.cpp
)
target_compile_definitions(host_mq PRIVATE MQ)

add_executable(host_fifo
    host.cpp
    conn_shm.cpp
    conn_mq.cpp
    conn_fifo.cpp
)
target_compile_definitions(host_fifo PRIVATE FIFO)


