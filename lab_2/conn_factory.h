#pragma once

#include <cstddef>

#if defined(SHM)
#include "conn_shm.h"
#elif defined(MQ)
#include "conn_mq.h"
#elif defined(FIFO)
#include "conn_fifo.h"
#else
#error "Define SHM, MQ or FIFO"
#endif

#if !defined(SHM)
class ConnShm;
#endif
#if !defined(MQ)
class ConnMq;
#endif
#if !defined(FIFO)
class ConnFifo;
#endif

class ConnectionFactory {
public:
    #if defined(SHM)
    using ConnType = ConnShm;
    #elif defined(MQ)
    using ConnType = ConnMq;
    #elif defined(FIFO)
    using ConnType = ConnFifo;
    #else
    #error "Define SHM, MQ or FIFO"
    #endif

    static ConnType* Create(int id, bool create);
    
    static void Prepare(int id);
};
