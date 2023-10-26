#include "slogger.h"
#include "replicated_log.h"
#include "../slib/log.h"
#include "../slib/memcached.h"
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
        for (int i=0;i<5;i++){
            INFO(log_id(), "SLogger FSM iteration %d\n", i);
            sleep(1);
            test_insert_log_entry(i);
        }
    }

    void SLogger::test_insert_log_entry(int i) {


        //Step 1 we are going to allocate some memory for the remote log

        //The assumption is that the local log tail pointer is at the end of the log.

    }


    void SLogger::init_rdma_structures(rdma_info info){ 

        assert(info.qp != NULL);
        assert(info.completion_queue != NULL);
        assert(info.pd != NULL);

        ALERT("SLOG", "SLogger Initializing RDMA Structures");


        _qp = info.qp;
        _completion_queue = info.completion_queue;
        _protection_domain = info.pd;

        _slog_config = memcached_get_slog_config();
        assert(_slog_config != NULL);
        assert(_slog_config->slog_size_bytes == _replicated_log.get_size_bytes());
        INFO(log_id(),"got a slog config from the memcached server and it seems to line up\n");

        ALERT("SLOG", "SLogger Done Initializing RDMA Structures");
        ALERT("SLOG", "TODO - register local log with a mr");

        // INFO(log_id(), "Registering table with RDMA device size %d, location %p\n", get_table_size_bytes(), get_table_pointer()[0]);
        _log_mr = rdma_buffer_register(_protection_domain, _replicated_log.get_log_pointer(), _replicated_log.get_size_bytes(), MEMORY_PERMISSION);
        // INFO(log_id(), "Registering lock table with RDMA device size %d, location %p\n", get_lock_table_size_bytes(), get_lock_table_pointer());
        _tail_pointer_mr = rdma_buffer_register(_protection_domain, _replicated_log.get_tail_pointer(), _replicated_log.get_tail_pointer_size_bytes(), MEMORY_PERMISSION);

        _wr_id = 10000000;
        _wc = (struct ibv_wc *) calloc (MAX_CONCURRENT_MESSAGES, sizeof(struct ibv_wc));
        ALERT("SLOG", "Done registering memory regions for SLogger");

    }

}