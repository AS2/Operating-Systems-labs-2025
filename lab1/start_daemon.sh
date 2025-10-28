#!/bin/bash

DAEMON_NAME="daemon"
DAEMON_PATH="./daemon"
SOURCE_FILE="./daemon.cpp"
PID_FILE="/var/run/${DAEMON_NAME}.pid"

if ! [ -w "/var/run" ]; then
    PID_FILE="$HOME/.${DAEMON_NAME}.pid"
fi

compile_daemon() {
    echo "Компиляция демона..."
    if [ ! -f "$SOURCE_FILE" ]; then
        echo "Ошибка: файл $SOURCE_FILE не найден"
        exit 1
    fi
    
    g++ -Wall -Wextra -std=c++17 -o daemon daemon.cpp
    if [ $? -ne 0 ]; then
        echo "Ошибка компиляции"
        exit 1
    fi
    echo "Компиляция завершена успешно"
}

case "$1" in
    start)
        echo "Запуск демона синхронизации..."
        
        if [ ! -f "$DAEMON_PATH" ]; then
            compile_daemon
        fi
        
        if [ -f "$PID_FILE" ]; then
            PID=$(cat "$PID_FILE")
            if ps -p $PID > /dev/null 2>&1; then
                echo "Демон уже запущен с PID: $PID"
                exit 1
            else
                rm -f "$PID_FILE"
            fi
        fi
        
        $DAEMON_PATH
        
        sleep 2
        
        if [ -f "$PID_FILE" ]; then
            PID=$(cat "$PID_FILE")
            if ps -p $PID > /dev/null 2>&1; then
                echo "✓ Демон запущен с PID: $PID"
                echo "Логи: log show --predicate 'process == \"sync_daemon\"' --last 1m"
            else
                echo "✗ Ошибка запуска демона"
                exit 1
            fi
        else
            echo "⚠ PID файл не создан, но демон может работать"
        fi
        ;;
    
    stop)
        echo "Остановка демона..."
        
        if [ -f "$PID_FILE" ]; then
            PID=$(cat "$PID_FILE")
            if ps -p $PID > /dev/null 2>&1; then
                kill $PID
                echo "Сигнал остановки отправлен ($PID)"
                
                for i in {1..5}; do
                    if ! ps -p $PID > /dev/null 2>&1; then
                        break
                    fi
                    sleep 1
                done
                
                if ps -p $PID > /dev/null 2>&1; then
                    kill -9 $PID
                    echo "Принудительная остановка"
                fi
                
                rm -f "$PID_FILE"
                echo "✓ Демон остановлен"
            else
                echo "Процесс не найден"
                rm -f "$PID_FILE"
            fi
        else
            echo "Демон не запущен"
        fi
        ;;
    
    status)
        if [ -f "$PID_FILE" ]; then
            PID=$(cat "$PID_FILE")
            if ps -p $PID > /dev/null 2>&1; then
                echo "✓ Демон работает (PID: $PID)"
                ps -p $PID -o pid,rss,vsz,command
            else
                echo "✗ Демон не работает"
                rm -f "$PID_FILE"
            fi
        else
            echo "✗ Демон не запущен"
        fi
        ;;
    
    restart)
        $0 stop
        sleep 1
        $0 start
        ;;
    
    compile)
        compile_daemon
        ;;
    
    logs)
        echo "Логи демона (последние 50 записей):"
        if [[ "$OSTYPE" == "darwin"* ]]; then
            log show --predicate 'process == "sync_daemon"' --last 5m --style compact
        else
            sudo journalctl -t sync_daemon -n 50 -f
        fi
        ;;
    
    clean)
        echo "Очистка..."
        $0 stop 2>/dev/null
        rm -f daemon
        rm -f "$PID_FILE"
        echo "✓ Очистка завершена"
        ;;
    
    *)
        echo "Использование: $0 {start|stop|status|restart|compile|logs|clean}"
        echo ""
        echo "  start    - Компилировать и запустить демон"
        echo "  stop     - Остановить демон"
        echo "  status   - Проверить статус демона"
        echo "  restart  - Перезапустить демон"
        echo "  compile  - Только скомпилировать"
        echo "  logs     - Показать логи"
        echo "  clean    - Остановить и удалить файлы"
        exit 1
        ;;
esac

exit 0