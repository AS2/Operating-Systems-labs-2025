cmake_minimum_required(VERSION 3.10)
project(Lab2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Флаги компиляции
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")

# Подключаем необходимые библиотеки
find_library(RT_LIBRARY rt)
if(RT_LIBRARY)
    link_libraries(${RT_LIBRARY})
endif()

# Находим все файлы conn_*.cpp
file(GLOB CONN_SOURCES "conn_*.cpp")

# Для каждого conn_*.cpp создаем исполняемые файлы host_* и client_*
foreach(CONN_SOURCE ${CONN_SOURCES})
    get_filename_component(CONN_NAME ${CONN_SOURCE} NAME_WE)
    string(REGEX REPLACE "^conn_" "" TYPE_CODE ${CONN_NAME})
    
    # Пропускаем mq на неподдерживаемых платформах
    if(TYPE_CODE STREQUAL "mq" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS "Пропуск сборки mq на ${CMAKE_SYSTEM_NAME} (требуется Linux)")
        continue()
    endif()
    
    # Создаем host_* со всеми необходимыми файлами соединений
    # host.cpp использует все типы соединений, поэтому нужно подключить все
    add_executable(host_${TYPE_CODE} 
        host.cpp
        ${CONN_SOURCES}
    )
    
    # Подключаем библиотеку rt для mq на Linux
    if(TYPE_CODE STREQUAL "mq" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(host_${TYPE_CODE} rt)
    endif()
    
    # Для независимой конфигурации создаем client_* (если есть client.cpp)
    if(EXISTS "client.cpp")
        add_executable(client_${TYPE_CODE}
            client.cpp
            ${CONN_SOURCES}
        )
        if(TYPE_CODE STREQUAL "mq" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_libraries(client_${TYPE_CODE} rt)
        endif()
    endif()
endforeach()
