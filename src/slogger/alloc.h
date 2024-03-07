#ifndef ALLOC_H
#define ALLOC_H

#include "slogger.h"
#include "replicated_log.h"

#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"

using namespace slogger;
using namespace rdma_helper;




class RMalloc : public SLogger {
    public:
        RMalloc(unordered_map<string,string> config);
        void fsm();

    private:
};

#endif