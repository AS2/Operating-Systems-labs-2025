cmake_minimum_required(VERSION 3.14)
project(lab2)

set(CMAKE_CXX_STANDARD 17)

set (
  SRC
  ./src/host.cpp
  ./src/client.cpp
  ./src/connections/conn_pipe.cpp
  ./src/connections/conn_fifo.cpp
)

add_executable(host ${SRC})

target_include_directories(host PRIVATE ./src ./src/connections)
target_link_libraries(host PRIVATE pthread rt)


