#include "nt.h"
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

    NT::NT(unordered_map<string,string> config) : SLogger(config) {
        ALERT("NT", "NT constructor called");
        // _replicated_log = Replicated_Log();
    }

    void NT::fsm() {
        ALERT("NT", "NT fsm called");
        SLogger::fsm();
    }
}