#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <vector>
#include <string>

struct GoatStatus {
    int number;
    bool is_alive;
    int consecutive_dead_turns;
};

struct GameMessage {
    int goat_number;  // Номер козленка (1-n) в ответе от хоста
    int thrown_number; // Выброшенное число (1-100 для живых, 1-50 для мертвых) в запросе от клиента
    int status; // 0 - жив, 1 - мертв
};

struct GameState {
    int wolf_number;
    std::vector<GoatStatus> goats;
    int round;
    int hidden_count;
    int caught_count;
    int dead_count;
    bool game_over;
};

#endif // GAME_LOGIC_H
