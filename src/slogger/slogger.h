#ifndef SLOG
#define SLOG

#include "../slib/log.h"
#include "../slib/config.h"
#include "../slib/state_machines.h"
#include "../rdma/rdma_common.h"
#include "replicated_log.h"

using namespace state_machines;
using namespace replicated_log;

#define MAX_CONCURRENT_MESSAGES 32
#define ID_SIZE 64

namespace slogger {

    class SLogger : public State_Machine {
        public:
            SLogger(){};
            SLogger(unordered_map<string, string> config);
            // ~SLogger() {ALERT("SLOG", "deleting slog");}
            bool FAA_Allocate_Log_Entry(Log_Entry &bs);
            bool CAS_Allocate_Log_Entry(Log_Entry &bs);

            bool (SLogger::*_allocate_log_entry)(Log_Entry &bs);
            void Write_Log_Entry(Log_Entry &bs, void* data);
            void Syncronize_Log(uint64_t offset);
            uint64_t local_to_remote_log_address(uint64_t local_address);

            void init_rdma_structures(rdma_info info);
            void fsm();
            void clear_statistics();
            const char * log_id();
            unordered_map<string, string> get_stats();

        protected:
            Client_Workload_Driver _workload_driver;
            char _log_identifier[ID_SIZE];
            uint32_t _id;


        private:
            bool test_insert_log_entry(int i, int size);

            void set_allocate_function(unordered_map<string, string> config);


            //RDMA Variables
            ibv_qp * _qp;
            ibv_pd *_protection_domain;
            struct ibv_cq * _completion_queue;
            slog_config *_slog_config;

            Replicated_Log _replicated_log;
            ibv_mr *_log_mr;
            ibv_mr *_tail_pointer_mr;

            struct ibv_wc *_wc;
            uint64_t _wr_id;

            //silly variables
            int _entry_size;

    };
}

#endif