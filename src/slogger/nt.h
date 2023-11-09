#ifndef NT_H
#define NT_H

#include "slogger.h"
#include "replicated_log.h"

#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"

using namespace replicated_log;
using namespace slogger;
using namespace rdma_helper;

namespace nt {
    class NT : public SLogger {
        public:
            NT(unordered_map<string,string> config);
            void fsm();
    };
}

#endif