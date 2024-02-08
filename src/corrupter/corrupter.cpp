#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"
#include "corrupter.h"
#include "mem_chunks.h"

#include <stdint.h>
using namespace mem_chunks;
using namespace rdma_helper;

namespace corrupter {

    Corrupter::Corrupter(unordered_map<string, string> config) : State_Machine(config){

        try {
            unsigned int memory_size = stoi(config["memory_size"]);
            if (memory_size < 1) {
                ALERT("CORRUPTER", "Error: memory size must be greater than 0");
                exit(1);
            }
            if (!IsPowerOfTwo(memory_size)) {
                ALERT("CORRUPTER", "Error: memory size must be a power of 2, input was %d", memory_size);
                exit(1);
            }

            unsigned int chunk_size = stoi(config["chunk_size"]);
            _mem_chunks = Mem_Chunks(memory_size, chunk_size);

            ALERT("Corrupter", "Creating Corrupter with id %s", config["id"].c_str());
            _id = stoi(config["id"]);
            sprintf(_log_identifier, "Client: %3d", stoi(config["id"]));
            _local_prime_flag = false;
            INFO(log_id(), "Done creating SLogger");
        } catch (exception& e) {
            ALERT("Corrupter", "Error in SLogger constructor: %s", e.what());
            exit(1);
        }   

        ALERT("Corrupter", "Done creating SLogger");
    }


    unordered_map<string, string> Corrupter::get_stats() {
        unordered_map<string, string> stats = State_Machine::get_stats();
        // unordered_map<string, string> workload_stats = _workload_driver.get_stats();
        // stats.insert(workload_stats.begin(), workload_stats.end());
        return stats;

    }

    const char * Corrupter::log_id() {
        return _log_identifier;
    }

    void Corrupter::clear_statistics() {
        State_Machine::clear_statistics();
        SUCCESS(log_id(), "Clearing statistics");
    }

    void Corrupter::fsm(){
        // ALERT(log_id(), "SLogger Starting FSM");
        while(!*_global_start_flag){
            INFO(log_id(), "not globally started");
        };

        int i=0;
        while(!*_global_end_flag){

            //Pause goes here rather than anywhere else because I don't want
            //To have locks, or any outstanding requests
            if (*_global_prime_flag && !_local_prime_flag){
                _local_prime_flag = true;
                clear_statistics();
            }

            _operation_start_time = get_current_ns();
            // INFO(log_id(), "SLogger FSM iteration %d\n", i);
            // sleep(1);
            i = (i+1)%50;
            printf("do the thing here");
            // bool res = test_insert_log_entry(i,_entry_size);
            // if (!res) {
            //     break;
            // }
            _operation_end_time = get_current_ns();

            #ifdef MEASURE_ESSENTIAL
                // uint64_t latency = (_operation_end_time - _operation_start_time).count();
                #ifdef MEASURE_MOST
                #endif
            #endif
        }


        // ALERT(log_id(), "SLogger Ending FSM");
    }

    // uint64_t Corrupter::local_to_remote_log_address(uint64_t local_address) {
    //     uint64_t base_address = (uint64_t) _replicated_log.get_log_pointer();
    //     uint64_t address_offset = local_address - base_address;
    //     return _slog_config->slog_address + address_offset;
    // }


    void Corrupter::init_rdma_structures(rdma_info info){ 

        assert(info.qp != NULL);
        assert(info.completion_queue != NULL);
        assert(info.pd != NULL);

        ALERT("SLOG", "SLogger Initializing RDMA Structures");


        _qp = info.qp;
        _completion_queue = info.completion_queue;
        _protection_domain = info.pd;

        _corrupter_config = memcached_get_corrupter_config();
        assert(_corrupter_config != NULL);
        assert((unsigned int)_corrupter_config->chunk_mem_size == _mem_chunks.get_memory_size());
        assert((unsigned int)_corrupter_config->chunk_size == _mem_chunks.get_chunk_size());
        INFO(log_id(),"got a slog config from the memcached server and it seems to line up\n");

        ALERT("SLOG", "SLogger Done Initializing RDMA Structures");
        ALERT("SLOG", "TODO - register local log with a mr");

        // INFO(log_id(), "Registering table with RDMA device size %d, location %p\n", get_table_size_bytes(), get_table_pointer()[0]);
        _log_mr = rdma_buffer_register(_protection_domain, _mem_chunks.get_chunk_pointer(0), _mem_chunks.get_memory_size(), MEMORY_PERMISSION);

        _wr_id = 10000000;
        _wc = (struct ibv_wc *) calloc (MAX_CONCURRENT_MESSAGES, sizeof(struct ibv_wc));
        ALERT("SLOG", "Done registering memory regions for SLogger");

    }

}