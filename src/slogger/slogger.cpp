#include "slogger.h"
#include "replicated_log.h"
#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"

using namespace replicated_log;
using namespace rdma_helper;

namespace slogger {


    SLogger::SLogger(unordered_map<string, string> config) : State_Machine(config){

        try {
            unsigned int memory_size = stoi(config["memory_size"]);
            _workload_driver = Client_Workload_Driver(config);


            _replicated_log = Replicated_Log(memory_size);
            set_allocate_function(config);

            ALERT("SLOG", "Creating SLogger with id %s", config["id"].c_str());
            sprintf(_log_identifier, "Client: %3d", stoi(config["id"]));
            _local_prime_flag = false;
            ALERT(log_id(), "Done creating SLogger");
        } catch (exception& e) {
            ALERT("SLOG", "Error in SLogger constructor: %s", e.what());
            exit(1);
        }   

        ALERT("Slogger", "Done creating SLogger");
    }


    unordered_map<string, string> SLogger::get_stats() {
        unordered_map<string, string> stats = State_Machine::get_stats();
        unordered_map<string, string> workload_stats = _workload_driver.get_stats();
        stats.insert(workload_stats.begin(), workload_stats.end());
        return stats;

    }

    const char * SLogger::log_id() {
        return _log_identifier;
    }

    void SLogger::clear_statistics() {
        State_Machine::clear_statistics();
        SUCCESS(log_id(), "Clearing statistics");
    }

    void SLogger::fsm(){
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
            bool res = test_insert_log_entry(i);
            if (!res) {
                break;
            }
            _operation_end_time = get_current_ns();

            #ifdef MEASURE_ESSENTIAL
            uint64_t latency = (_operation_end_time - _operation_start_time).count();
            _completed_insert_count++;
            _current_insert_rtt = 0;
            _sum_insert_latency_ns += latency;
                #ifdef MEASURE_MOST
                _insert_rtt.push_back(_current_insert_rtt);
                _insert_latency_ns.push_back(latency);
                #endif
            #endif
        }


        // ALERT(log_id(), "SLogger Ending FSM");
    }

    uint64_t SLogger::local_to_remote_log_address(uint64_t local_address) {
        uint64_t base_address = (uint64_t) _replicated_log.get_log_pointer();
        uint64_t address_offset = local_address - base_address;
        return _slog_config->slog_address + address_offset;
    }

    bool SLogger::FAA_Allocate_Log_Entry(Basic_Entry &bs) {

        // printf("FETCH AND ADD\n");
        uint64_t local_tail_pointer_address = (uint64_t) _replicated_log.get_tail_pointer_address();
        uint64_t remote_tail_pointer_address = _slog_config->tail_pointer_address;
        uint64_t add  = bs.Get_Total_Entry_Size();
        uint64_t current_tail_value = _replicated_log.get_tail_pointer();

        rdmaFetchAndAddExp(
            _qp,
            local_tail_pointer_address,
            remote_tail_pointer_address,
            add,
            _tail_pointer_mr->lkey,
            _slog_config->tail_pointer_key,
            true,
            _wr_id);

        _wr_id++;
        int outstanding_messages = 1;
        int n = bulk_poll(_completion_queue, outstanding_messages, _wc);

        if (n < 0) {
            ALERT(log_id(), "Error polling completion queue");
            exit(1);
        }

        assert(current_tail_value <= _replicated_log.get_tail_pointer());
        #ifdef MEASURE_ESSENTIAL
        uint64_t request_size = RDMA_FAA_REQUEST_SIZE + RDMA_FAA_RESPONSE_SIZE;
        _faa_bytes += request_size;
        _total_bytes += request_size;
        _insert_operation_bytes += request_size;
        _current_insert_rtt++;
        _insert_rtt_count++;
        _total_cas++;
        #endif
        //At this point we should have a local tail that is at least as large as the remote tail
        //Also the remote tail is allocated

        return true;
    }

    bool SLogger::CAS_Allocate_Log_Entry(Basic_Entry &bs) {

        bool allocated = false;

        while(!allocated) {
            if (!_replicated_log.Can_Append(bs)) {
                ALERT(log_id(), "Unable to allocate. Try again later.  Current tail value is %lu", _replicated_log.get_tail_pointer());
                return false;
            }
            uint64_t local_tail_pointer_address = (uint64_t) _replicated_log.get_tail_pointer_address();
            uint64_t remote_tail_pointer_address = _slog_config->tail_pointer_address;
            uint64_t compare  = _replicated_log.get_tail_pointer();
            uint64_t new_tail_pointer = compare + bs.Get_Total_Entry_Size();
            uint64_t current_tail_value = _replicated_log.get_tail_pointer();

            rdmaCompareAndSwapExp(
                _qp,
                local_tail_pointer_address,
                remote_tail_pointer_address,
                compare,
                new_tail_pointer,
                _tail_pointer_mr->lkey,
                _slog_config->tail_pointer_key,
                true,
                _wr_id);

            _wr_id++;
            int outstanding_messages = 1;
            int n = bulk_poll(_completion_queue, outstanding_messages, _wc);

            if (n < 0) {
                ALERT(log_id(), "Error polling completion queue");
                exit(1);
            }

            allocated = (current_tail_value == _replicated_log.get_tail_pointer());

            #ifdef MEASURE_ESSENTIAL
            uint64_t request_size = RDMA_CAS_REQUEST_SIZE + RDMA_CAS_RESPONSE_SIZE;
            _cas_bytes += request_size;
            _total_bytes += request_size;
            _insert_operation_bytes += request_size;
            _current_insert_rtt++;
            _insert_rtt_count++;
            _total_cas++;
            if(!allocated) {
                _total_cas_failures++;
            }
            #endif
        }
        SUCCESS(log_id(), "Allocated log entry successfully %lu", _replicated_log.get_tail_pointer());
        return true;
    }

    void SLogger::Write_Log_Entry(Basic_Entry &bs){
        //we have to have allocated the remote entry at this point.
        //TODO assert somehow that we have allocated
        uint64_t local_log_tail_address = (uint64_t) _replicated_log.get_reference_to_tail_pointer_entry();
        uint64_t remote_log_tail_address = local_to_remote_log_address(local_log_tail_address);
        uint64_t size = bs.Get_Total_Entry_Size();

        // printf("writing to local addr %ul remote log %ul\n", local_log_tail_address, remote_log_tail_address);

        //Make the local change
        _replicated_log.Append_Basic_Entry(bs);

        rdmaWriteExp(
            _qp,
            local_log_tail_address,
            remote_log_tail_address,
            size,
            _log_mr->lkey,
            _slog_config->slog_key,
            -1,
            true,
            _wr_id);

        _wr_id++;
        int outstanding_messages = 1;
        int n = bulk_poll(_completion_queue, outstanding_messages, _wc);

        if (n < 0) {
            ALERT("SLOG", "Error polling completion queue");
            exit(1);
        }

        #ifdef MEASURE_ESSENTIAL
        uint64_t request_size = size + RDMA_WRITE_REQUEST_BASE_SIZE + RDMA_WRITE_RESPONSE_SIZE;
        _cas_bytes += request_size;
        _total_bytes += request_size;
        _insert_operation_bytes += request_size;
        _current_insert_rtt++;
        _insert_rtt_count++;
        _total_writes++;
        #endif




    }



    bool SLogger::test_insert_log_entry(int i) {


        //Step 1 we are going to allocate some memory for the remote log

        //The assumption is that the local log tail pointer is at the end of the log.
        //For the first step we are going to get the current value of the tail pointer

        Basic_Entry bs;
        const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        bs.entry_size = 10;
        bs.entry_type = i;
        bs.repeating_value = digits[i%36];

        //Now we must find out the size of the new entry that we want to commit
        // printf("This is where we are at currently\n");
        // _replicated_log.Print_All_Entries();

        // if (CAS_Allocate_Log_Entry(bs)) {
        // if (FAA_Allocate_Log_Entry(bs)) {
        if((this->*_allocate_log_entry)(bs)) {
            Write_Log_Entry(bs);
            // sleep(1);
            return true;
        }
        return false;

    }


    void SLogger::set_allocate_function(unordered_map<string, string> config){
        string allocate_function = config["allocate_function"];
        if(allocate_function == "CAS") {
            _allocate_log_entry = &SLogger::CAS_Allocate_Log_Entry;
        } else if (allocate_function == "FAA") {
            _allocate_log_entry = &SLogger::FAA_Allocate_Log_Entry;
        } else {
            ALERT("SLOG", "Unknown allocate function %s", allocate_function.c_str());
            exit(1);
        }
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
        _tail_pointer_mr = rdma_buffer_register(_protection_domain, _replicated_log.get_tail_pointer_address(), _replicated_log.get_tail_pointer_size_bytes(), MEMORY_PERMISSION);

        _wr_id = 10000000;
        _wc = (struct ibv_wc *) calloc (MAX_CONCURRENT_MESSAGES, sizeof(struct ibv_wc));
        ALERT("SLOG", "Done registering memory regions for SLogger");

    }

}