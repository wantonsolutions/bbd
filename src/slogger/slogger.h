#ifndef SLOG
#define SLOG

#include "../slib/log.h"
#include "../slib/state_machines.h"
#include "replicated_log.h"

using namespace state_machines;
using namespace replicated_log;

namespace slogger {

    class SLogger : public State_Machine {
        public:
            SLogger(){};
            SLogger(unordered_map<string, string> config);
            ~SLogger() {ALERT("SLOG", "deleting slog");}

            void init_rdma_structures(rdma_info info);
            void fsm();
            void clear_statistics();

        private:

            //RDMA Variables
            ibv_qp * _qp;
            ibv_pd *_protection_domain;
            struct ibv_cq * _completion_queue;

            Replicated_Log _replicated_log;

    };
}

#endif