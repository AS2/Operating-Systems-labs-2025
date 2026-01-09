#!/bin/bash

# Скрипт для сборки проекта с очисткой промежуточных файлов cmake

echo "Очистка предыдущих файлов сборки..."
rm -rf CMakeFiles
rm -f CMakeCache.txt
rm -f cmake_install.cmake
rm -f Makefile
rm -f *.o
rm -f host_*
rm -f client_*

echo "Запуск cmake..."
cmake .

if [ $? -eq 0 ]; then
    echo "Запуск make..."
    make
    
    if [ $? -eq 0 ]; then
        echo "Сборка завершена успешно!"
        echo "Исполняемые файлы:"
        ls -la host_* 2>/dev/null || echo "Нет файлов host_*"
        ls -la client_* 2>/dev/null || echo "Нет файлов client_*"
    else
        echo "Ошибка при выполнении make"
        exit 1
    fi
else
    echo "Ошибка при выполнении cmake"
    exit 1
fi
