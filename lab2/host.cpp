#include <iostream>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <thread>
#include <chrono>
#include "conn_seg.h"
#ifdef __linux__
#include "conn_mq.h"
#endif
#include "conn_fifo.h"
#include "conn_base.h"
#include "game_logic.h"

const int MAX_GOATS = 7;
const int TIMEOUT_SECONDS = 3;
const int GAME_OVER_TURNS = 2;

int get_wolf_number() {
    std::cout << "\nВведите число волка (1-100) или подождите 3 секунды для случайного: ";
    std::cout.flush();
    
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    
    int result = poll(&pfd, 1, TIMEOUT_SECONDS * 1000);
    
    if (result > 0 && (pfd.revents & POLLIN)) {
        int user_input;
        std::cin >> user_input;
        if (user_input >= 1 && user_input <= 100) {
            std::cout << "Введено число: " << user_input << std::endl;
            return user_input;
        }
    }
    
    // Случайное число, если не введено
    int random_num = (rand() % 100) + 1;
    std::cout << "Случайное число: " << random_num << std::endl;
    return random_num;
}

void play_game_with_connection(Conn* conn, const std::string& conn_type, int num_goats) {
    std::cout << "\n=== Игра началась с типом соединения: " << conn_type << " ===" << std::endl;
    std::cout << "Количество козлят: " << num_goats << std::endl;
    
    GameState state;
    state.round = 0;
    state.game_over = false;
    state.goats.resize(num_goats);
    
    for (int i = 0; i < num_goats; i++) {
        state.goats[i].number = i + 1;
        state.goats[i].is_alive = true;
        state.goats[i].consecutive_dead_turns = 0;
    }
    
    int all_dead_consecutive = 0;
    
    while (!state.game_over) {
        state.round++;
        std::cout << "\n--- Раунд " << state.round << " ---" << std::endl;
        
        // Получаем число волка
        state.wolf_number = get_wolf_number();
        std::cout << "Волк выбрасывает: " << state.wolf_number << std::endl;
        
        // Получаем числа от козлят (в конфигурации один к одному - один клиент, но играем с n козлятами)
        // Клиент будет отправлять числа для всех козлят последовательно
        std::vector<int> goat_numbers(num_goats);
        for (int i = 0; i < num_goats; i++) {
            GameMessage msg;
            if (!conn->Read(&msg, sizeof(msg))) {
                std::cerr << "Ошибка чтения от козленка " << (i+1) << std::endl;
                state.game_over = true;
                break;
            }
            goat_numbers[i] = msg.thrown_number;
            std::cout << "Козленок " << (i+1) << " выбрасывает: " << goat_numbers[i];
            if (state.goats[i].is_alive) {
                std::cout << " (жив)";
            } else {
                std::cout << " (мертв)";
            }
            std::cout << std::endl;
        }
        
        if (state.game_over) break;
        
        // Обрабатываем логику игры
        state.hidden_count = 0;
        state.caught_count = 0;
        state.dead_count = 0;
        
        int alive_count = 0;
        for (int i = 0; i < num_goats; i++) {
            int diff = abs(goat_numbers[i] - state.wolf_number);
            int threshold_alive = 70 / num_goats;
            int threshold_dead = 20 / num_goats;
            
            if (state.goats[i].is_alive) {
                if (diff <= threshold_alive) {
                    state.hidden_count++;
                    std::cout << "Козленок " << (i+1) << " спрятался!" << std::endl;
                } else {
                    state.goats[i].is_alive = false;
                    state.goats[i].consecutive_dead_turns = 1;
                    state.caught_count++;
                    std::cout << "Козленок " << (i+1) << " попался!" << std::endl;
                }
            } else {
                if (diff <= threshold_dead) {
                    state.goats[i].is_alive = true;
                    state.goats[i].consecutive_dead_turns = 0;
                    std::cout << "Козленок " << (i+1) << " воскрес!" << std::endl;
                } else {
                    state.goats[i].consecutive_dead_turns++;
                }
            }
            
            if (state.goats[i].is_alive) {
                alive_count++;
            } else {
                state.dead_count++;
            }
        }
        
        // Отправляем статусы козлятам (клиент получает все статусы)
        for (int i = 0; i < num_goats; i++) {
            GameMessage response;
            response.goat_number = i + 1;
            response.thrown_number = 0; // Не используется в ответе
            response.status = state.goats[i].is_alive ? 0 : 1;
            if (!conn->Write(&response, sizeof(response))) {
                std::cerr << "Ошибка записи козленку " << (i+1) << std::endl;
            }
        }
        
        // Выводим статистику раунда
        std::cout << "\nСтатистика раунда:" << std::endl;
        std::cout << "  Спрятавшихся: " << state.hidden_count << std::endl;
        std::cout << "  Попавшихся: " << state.caught_count << std::endl;
        std::cout << "  Мертвых: " << state.dead_count << std::endl;
        std::cout << "  Живых: " << alive_count << std::endl;
        
        // Проверяем условие окончания игры
        if (alive_count == 0) {
            all_dead_consecutive++;
            if (all_dead_consecutive >= GAME_OVER_TURNS) {
                state.game_over = true;
                std::cout << "\nИгра окончена! Все козлята мертвы " << GAME_OVER_TURNS << " хода подряд." << std::endl;
            }
        } else {
            all_dead_consecutive = 0;
        }
        
        // Небольшая задержка между раундами
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    std::cout << "\n=== Игра завершена ===" << std::endl;
}

void run_client_seg_impl(ConnSeg& conn, int num_goats) {
    srand(time(nullptr) ^ getpid());
    std::vector<bool> goats_alive(num_goats, true);
    
    while (true) {
        // Отправляем числа для всех козлят последовательно
        // Каждое сообщение отправляется отдельно, хост читает их последовательно
        for (int i = 0; i < num_goats; i++) {
            int goat_number;
            if (goats_alive[i]) {
                goat_number = (rand() % 100) + 1;
            } else {
                goat_number = (rand() % 50) + 1;
            }
            
            GameMessage msg;
            msg.goat_number = i + 1; // Номер козленка
            msg.thrown_number = goat_number; // Выброшенное число
            msg.status = 0;
            
            if (!conn.Write(&msg, sizeof(msg))) {
                std::cerr << "[CLIENT_SEG] Ошибка записи для козленка " << (i+1) << std::endl;
                return;
            }
            // Небольшая задержка между отправками, чтобы хост успел прочитать
            usleep(50000);
        }
        
        // Получаем статусы всех козлят
        for (int i = 0; i < num_goats; i++) {
            GameMessage response;
            if (!conn.Read(&response, sizeof(response))) {
                std::cerr << "[CLIENT_SEG] Ошибка чтения для козленка " << (i+1) << std::endl;
                return;
            }
            goats_alive[response.goat_number - 1] = (response.status == 0);
        }
        
        // Небольшая задержка перед следующим раундом
        usleep(100000);
    }
}

void run_client_seg(int num_goats) {
    key_t key = 1234;
    size_t size = 4096;
    // Клиент подключается к существующему сегменту
    ConnSeg conn(key, size, false);
    run_client_seg_impl(conn, num_goats);
}

void run_client_mq(int num_goats) {
#ifdef __linux__
    std::string mq_name = "/wolf_goats_mq";
    ConnMq conn(mq_name, false);
    
    srand(time(nullptr) ^ getpid());
    std::vector<bool> goats_alive(num_goats, true);
    
    while (true) {
        // Отправляем числа для всех козлят
        for (int i = 0; i < num_goats; i++) {
            int goat_number;
            if (goats_alive[i]) {
                goat_number = (rand() % 100) + 1;
            } else {
                goat_number = (rand() % 50) + 1;
            }
            
            GameMessage msg;
            msg.goat_number = i + 1; // Номер козленка
            msg.thrown_number = goat_number; // Выброшенное число
            msg.status = 0;
            
            if (!conn.Write(&msg, sizeof(msg))) {
                std::cerr << "[CLIENT_MQ] Ошибка записи для козленка " << (i+1) << std::endl;
                return;
            }
            // Небольшая задержка между отправками, чтобы хост успел прочитать
            usleep(50000);
        }
        
        // Получаем статусы всех козлят
        for (int i = 0; i < num_goats; i++) {
            GameMessage response;
            if (!conn.Read(&response, sizeof(response))) {
                std::cerr << "[CLIENT_MQ] Ошибка чтения для козленка " << (i+1) << std::endl;
                return;
            }
            goats_alive[response.goat_number - 1] = (response.status == 0);
        }
        
        usleep(100000);
    }
#else
    (void)num_goats; // Подавляем предупреждение о неиспользуемом параметре
    std::cerr << "[CLIENT_MQ] Не поддерживается на этой платформе" << std::endl;
#endif
}

void run_client_fifo(int num_goats) {
    std::string fifo_name = "/tmp/wolf_goats_fifo";
    ConnFifo conn(fifo_name, false);
    
    srand(time(nullptr) ^ getpid());
    std::vector<bool> goats_alive(num_goats, true);
    
    while (true) {
        // Отправляем числа для всех козлят
        for (int i = 0; i < num_goats; i++) {
            int goat_number;
            if (goats_alive[i]) {
                goat_number = (rand() % 100) + 1;
            } else {
                goat_number = (rand() % 50) + 1;
            }
            
            GameMessage msg;
            msg.goat_number = i + 1; // Номер козленка
            msg.thrown_number = goat_number; // Выброшенное число
            msg.status = 0;
            
            if (!conn.Write(&msg, sizeof(msg))) {
                std::cerr << "[CLIENT_FIFO] Ошибка записи для козленка " << (i+1) << std::endl;
                return;
            }
            // Небольшая задержка между отправками, чтобы хост успел прочитать
            usleep(50000);
        }
        
        // Получаем статусы всех козлят
        for (int i = 0; i < num_goats; i++) {
            GameMessage response;
            if (!conn.Read(&response, sizeof(response))) {
                std::cerr << "[CLIENT_FIFO] Ошибка чтения для козленка " << (i+1) << std::endl;
                return;
            }
            goats_alive[response.goat_number - 1] = (response.status == 0);
        }
        
        usleep(100000);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Использование: " << argv[0] << " <seg|mq|fifo> [num_goats]" << std::endl;
        return 1;
    }
    
    std::string conn_type = argv[1];
    int num_goats = (argc > 2) ? std::atoi(argv[2]) : MAX_GOATS;
    if (num_goats < 1 || num_goats > MAX_GOATS) {
        num_goats = MAX_GOATS;
    }
    
    srand(time(nullptr));
    
    Conn* conn = nullptr;
    pid_t child_pid = -1;
    
    if (conn_type == "seg") {
        key_t key = 1234;
        size_t size = 4096;
        // Создаем соединение ДО fork
        conn = new ConnSeg(key, size, true);
        
        child_pid = fork();
        if (child_pid == 0) {
            // Дочерний процесс - клиент
            // НЕ удаляем conn хоста, просто отсоединяемся от него
            // Создаем новое соединение для клиента (is_creator=false)
            ConnSeg client_conn(key, size, false);
            usleep(50000); // Небольшая задержка для завершения инициализации хоста
            // Используем локальный объект вместо указателя
            run_client_seg_impl(client_conn, num_goats);
            exit(0);
        } else if (child_pid > 0) {
            // Родительский процесс - хост
            std::cout << "[HOST] Создан дочерний процесс (клиент) с PID: " << child_pid << std::endl;
            usleep(100000); // Задержка для инициализации клиента
        } else {
            std::cerr << "[HOST] Ошибка fork: " << strerror(errno) << std::endl;
            delete conn;
            return 1;
        }
    } else if (conn_type == "mq") {
#ifdef __linux__
        std::string mq_name = "/wolf_goats_mq";
        // Создаем ресурсы ДО fork (как для несвязанных процессов)
        if (!ConnMq::CreateResources(mq_name)) {
            std::cerr << "[HOST] Ошибка создания ресурсов MQ" << std::endl;
            return 1;
        }
        
        child_pid = fork();
        if (child_pid == 0) {
            // Дочерний процесс - клиент
            // Открываем ресурсы после fork (как несвязанный процесс)
            usleep(50000); // Небольшая задержка для завершения инициализации хоста
            run_client_mq(num_goats);
            exit(0);
        } else if (child_pid > 0) {
            // Родительский процесс - хост
            // Открываем ресурсы после fork (как несвязанный процесс)
            usleep(50000); // Небольшая задержка для завершения fork
            conn = new ConnMq(mq_name, true);
            std::cout << "[HOST] Создан дочерний процесс (клиент) с PID: " << child_pid << std::endl;
            usleep(100000); // Задержка для инициализации клиента
        } else {
            std::cerr << "[HOST] Ошибка fork: " << strerror(errno) << std::endl;
            return 1;
        }
#else
        std::cerr << "Тип соединения mq поддерживается только на Linux" << std::endl;
        return 1;
#endif
    } else if (conn_type == "fifo") {
        std::string fifo_name = "/tmp/wolf_goats_fifo";
        // Создаем ресурсы ДО fork (как для несвязанных процессов)
        if (!ConnFifo::CreateResources(fifo_name)) {
            std::cerr << "[HOST] Ошибка создания ресурсов FIFO" << std::endl;
            return 1;
        }
        
        child_pid = fork();
        if (child_pid == 0) {
            // Дочерний процесс - клиент
            // Открываем ресурсы после fork (как несвязанный процесс)
            usleep(50000); // Небольшая задержка для завершения инициализации хоста
            run_client_fifo(num_goats);
            exit(0);
        } else if (child_pid > 0) {
            // Родительский процесс - хост
            // Открываем ресурсы после fork (как несвязанный процесс)
            usleep(50000); // Небольшая задержка для завершения fork
            conn = new ConnFifo(fifo_name, true);
            std::cout << "[HOST] Создан дочерний процесс (клиент) с PID: " << child_pid << std::endl;
            usleep(100000); // Задержка для инициализации клиента
        } else {
            std::cerr << "[HOST] Ошибка fork: " << strerror(errno) << std::endl;
            return 1;
        }
    } else {
        std::cerr << "Неизвестный тип соединения: " << conn_type << std::endl;
        return 1;
    }
    
    if (child_pid > 0 && conn != nullptr) {
        // Хост играет игру
        play_game_with_connection(conn, conn_type, num_goats);
        
        // Ждем завершения дочернего процесса
        int status;
        waitpid(child_pid, &status, 0);
        std::cout << "[HOST] Дочерний процесс завершен" << std::endl;
        
        delete conn;
    }
    
    return 0;
}
