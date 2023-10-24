#include "slogger.h"
#include "replicated_log.h"
#include "../slib/log.h"
#include "../rdma/rdma_common.h"

using namespace replicated_log;

namespace slogger {

    SLogger::SLogger(unordered_map<string, string> config) : State_Machine(config){

        try {
            unsigned int memory_size = stoi(config["memory_size"]);
            _replicated_log = Replicated_Log(memory_size);
        } catch (exception& e) {
            ALERT("SLOG", "Error in SLogger constructor: %s", e.what());
            exit(1);
        }   

        ALERT("Slogger", "Done creating SLogger");
    }

    void SLogger::fsm(){
        ALERT("SLOG", "SLogger Starting FSM");
        ALERT("SLOG", "SLogger Ending FSM");
    }


    void SLogger::init_rdma_structures(rdma_info info){ 

        assert(info.qp != NULL);
        assert(info.completion_queue != NULL);
        assert(info.pd != NULL);

        ALERT("SLOG", "SLogger Initializing RDMA Structures");


        _qp = info.qp;
        _completion_queue = info.completion_queue;
        _protection_domain = info.pd;

        ALERT("SLOG", "TODO init rdma structures");
        // _table_config = memcached_get_table_config();
        // assert(_table_config != NULL);
        // assert(_table_config->table_size_bytes == get_table_size_bytes());
        // INFO(log_id(),"got a table config from the memcached server and it seems to line up\n");

        // INFO(log_id(), "Registering table with RDMA device size %d, location %p\n", get_table_size_bytes(), get_table_pointer()[0]);
        // _table_mr = rdma_buffer_register(_protection_domain, get_table_pointer()[0], _table.get_table_size_bytes(), MEMORY_PERMISSION);
        // INFO(log_id(), "Registering lock table with RDMA device size %d, location %p\n", get_lock_table_size_bytes(), get_lock_table_pointer());
        // _lock_table_mr = rdma_buffer_register(_protection_domain, get_lock_table_pointer(), get_lock_table_size_bytes(), MEMORY_PERMISSION);

        // _wr_id = 10000000;
        // _wc = (struct ibv_wc *) calloc (MAX_CONCURRENT_CUCKOO_MESSAGES, sizeof(struct ibv_wc));

    }

}