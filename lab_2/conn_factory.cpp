#include "conn_factory.h"

ConnectionFactory::ConnType* ConnectionFactory::Create(int id, bool create) {
    return new ConnectionFactory::ConnType(id, create);
}

void ConnectionFactory::Prepare(int id) {
#if defined(FIFO)
    ConnFifo::PrepareEndpoints(id);
#endif
}
