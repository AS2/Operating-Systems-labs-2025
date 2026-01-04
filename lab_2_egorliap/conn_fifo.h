#pragma once

#include <cstddef>
#include <string>

class ConnFifo {
public:
    static void PrepareEndpoints(int id);

    ConnFifo(int id, bool create);
    bool Read(void *buf, std::size_t count);
    bool Write(const void *buf, std::size_t count);
    ~ConnFifo();

private:
    int  id_{};
    bool creator_{false};

    std::string path_c2h_; // client -> host
    std::string path_h2c_; // host   -> client

    int fd_read_{-1};
    int fd_write_{-1};
};


