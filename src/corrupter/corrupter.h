#ifndef CORRUPTER_H
#define CORRUPTER_H

#include "../slib/log.h"
#include "../slib/config.h"
#include "../slib/state_machines.h"
#include "../rdma/rdma_common.h"
#include "mem_chunks.h"

using namespace state_machines;
using namespace mem_chunks;

#define MAX_CONCURRENT_MESSAGES 32
#define ID_SIZE 64

namespace corrupter {

    class Corrupter : public State_Machine {
        public:
            Corrupter(){};
            Corrupter(unordered_map<string, string> config);
            // ~SLogger() {ALERT("SLOG", "deleting slog");}
            // uint64_t local_to_remote_log_address(uint64_t local_address);
            void init_rdma_structures(rdma_info info);
            void fsm();
            void clear_statistics();
            const char * log_id();
            unordered_map<string, string> get_stats();

        protected:
            char _log_identifier[ID_SIZE];
            uint32_t _id;


        private:
            Mem_Chunks _mem_chunks;

            //RDMA Variables
            ibv_qp * _qp;
            ibv_pd *_protection_domain;
            struct ibv_cq * _completion_queue;
            corrupter_config *_corrupter_config;

            ibv_mr *_log_mr;
            ibv_mr *_tail_pointer_mr;

            struct ibv_wc *_wc;
            uint64_t _wr_id;



            //silly variables
            int _entry_size;

    };
}

#endif