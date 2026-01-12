#pragma once

#include <ctime>

struct Message
{
    std::time_t timestamp;
    int sender_id;
    int target_id;
    char text[256];
};

constexpr std::size_t MSG_SIZE = sizeof(Message);
constexpr int TARGET_BROADCAST = -1;
constexpr int TARGET_SYSTEM = -2;
