#!/bin/bash

set -euo pipefail

# Очистка предыдущих сборок
rm -f *.o mydaemon

# Компиляция с жёсткими флагами
g++ -Wall -Werror -c daemon.cpp -o daemon.o
g++ -Wall -Werror -c main.cpp -o main.o

# Линковка
g++ daemon.o main.o -o mydaemon

# Уборка
rm -f *.o

echo "Сборка завершена: ./mydaemon"
echo "Для запуска демона: sudo ./mydaemon  (нужен sudo из-за /var/run/)"