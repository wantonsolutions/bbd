#include "slogger.h"
#include "replicated_log.h"
#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"

#include <stdint.h>
using namespace replicated_log;
using namespace rdma_helper;

char no_op_buffer[NOOP_BUFFER_SIZE];


namespace slogger {


    SLogger::SLogger(unordered_map<string, string> config) : State_Machine(config){

        try {
            unsigned int memory_size = stoi(config["memory_size"]);
            if (memory_size < 1) {
                ALERT("SLOG", "Error: memory size must be greater than 0");
                exit(1);
            }
            if (!IsPowerOfTwo(memory_size)) {
                ALERT("SLOG", "Error: memory size must be a power of 2, input was %d", memory_size);
                exit(1);
            }
            _workload_driver = Client_Workload_Driver(config);


            _replicated_log = Replicated_Log(memory_size);
            set_allocate_function(config);
            set_workload(config["workload"]);

            ALERT("SLOG", "Creating SLogger with id %s", config["id"].c_str());
            _id = stoi(config["id"]);
            _entry_size = stoi(config["entry_size"]);
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
            bool res = test_insert_log_entry(i,_entry_size);
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

    bool SLogger::FAA_Allocate_Log_Entry(Log_Entry &bs) {

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

        // printf("FETCH AND ADD DONE tail value at the time of the faa is %lu\n", _replicated_log.get_tail_pointer());

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

    bool SLogger::MFAA_Allocate_Log_Entry(Log_Entry &le) {
        // printf("FETCH AND ADD\n");
        uint64_t local_tail_pointer_address = (uint64_t) _replicated_log.get_tail_pointer_address();
        uint64_t remote_tail_pointer_address = _slog_config->tail_pointer_address;
        uint64_t add  = le.Get_Total_Entry_Size();
        uint64_t current_tail_value = _replicated_log.get_tail_pointer();

        // ALERT("SLOG", "MFAA_Allocate_Log_Entry:
        // local_tail_pointer_address %lu,
        // remote_tail_pointer_address %lu, add %lu,
        // current_tail_value %lu",
        // local_tail_pointer_address,
        // remote_tail_pointer_address, add,
        // current_tail_value);



        rdmaMaskedFetchAndAddExp(
            _qp,
            local_tail_pointer_address,
            remote_tail_pointer_address,
            add,
            _tail_pointer_mr->lkey,
            _slog_config->tail_pointer_key,
            _replicated_log.get_memory_size()/2,
            true,
            _wr_id);

        _wr_id++;
        int outstanding_messages = 1;
        int n = bulk_poll(_completion_queue, outstanding_messages, _wc);

        if (n < 0) {
            ALERT(log_id(), "Error polling completion queue");
            exit(1);
        }

        // printf("FETCH AND ADD DONE tail value at the time of the faa is %lu\n", _replicated_log.get_tail_pointer());

        // ALERT("TODO", "Here is where we need to perform
        // some roll over calculations. If the size we
        // requested + the current tail pointer is greater
        // than the size of the log, then we need to roll
        // over the tail pointer. We also need to roll over
        // the tail pointer if the size we requested + the
        // current tail pointer is greater than the size of
        // the log minus the size of the log header. We also
        // need to roll over the tail pointer if the size we
        // requested + the current tail pointer is greater
        // than the size of the log minus the size of the
        // log header minus the size of the log footer. We
        // also need to roll over the tail pointer if the
        // size we requested + the current tail pointer is
        // greater than the size of the log minus the size
        // of the log header minus the size of the log
        // footer minus the size of the log control. We also
        // need to roll over the tail pointer if the size we
        // requested + the current tail pointer is greater
        // than the size of the log minus the size of the
        // log header minus the size of the log footer minus
        // the size of the log control minus the size of the
        // log control.");

        ALERT("SLOG", "Tail Pointer %lu", _replicated_log.get_tail_pointer());

        if (_replicated_log.get_tail_pointer() + add > _replicated_log.get_memory_size()) {
            ALERT("SLOG", "ALERT we have detected that we are on the boundry and need to do the roll over");
            fill_allocated_log_with_noops(add);
            MFAA_Allocate_Log_Entry(le);
            ALERT("SLOG", "ALERT we have rolled over and completed the allocation");
        }


        // assert(current_tail_value <= _replicated_log.get_tail_pointer());
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

    void SLogger::fill_allocated_log_with_noops(uint64_t size) {
        //The first thing we need to do is construct two no-ops one at the beginning and one at the end of the log
        //This function should only be called when we have a region of the log allocated that spans the log and must be 
        //broken into two pieces.
        // log [xxx______xxxx]
        // imagine that we called allocate and got size x = 7 but it rolled over
        // now we are going to allocate two entries of size 4 at the end, and size 3 at the beginning

        assert(_replicated_log.get_tail_pointer() + size > _replicated_log.get_memory_size());
        uint64_t first_entry_size = _replicated_log.get_memory_size() - _replicated_log.get_tail_pointer();
        uint64_t second_entry_size = size - first_entry_size;
        //we have to make sure that the log entries can fit.
        //If they cant we are in trouble.
        //One fix is to make sure that the log entries can always fit in a byte.
        //That is not the case for now
        ALERT("SLOG", "Filling log with noops. First entry size %lu, second entry size %lu", first_entry_size, second_entry_size);

        assert(first_entry_size >= sizeof(Log_Entry) || first_entry_size == 0);
        assert(second_entry_size >= sizeof(Log_Entry) || second_entry_size == 0);

        if (first_entry_size > 0) {
            Write_NoOp(first_entry_size - sizeof(Log_Entry));
            ALERT("SLOG", "Wrote first noop");
        }
        if (second_entry_size > 0) {
            Write_NoOp(second_entry_size - sizeof(Log_Entry));
            ALERT("SLOG", "Wrote second noop");
        }
        Sync_To_Last_Write();
    }

    void * SLogger::Next_Operation() {
        Log_Entry * le = _replicated_log.Next_Operation();
        if (le == NULL) {
            return NULL;
        }
        //TODO here is where we would put controls in the log.
        //For now we just return the data.
        //For instance, if we wanted to have contorl log entries with different types we could embed them here.
        if (le->type == log_entry_types::control) {
            ALERT("SLOG", "Got a control log entry");
        }

        //return a pointer to the data of the log entry. Leave it to the application to figure out what's next;
        return (void *) le + sizeof(Log_Entry);
    }

    void SLogger::Write_Operation(void* op, int size) {
        Log_Entry le;
        le.type = log_entry_types::app;
        le.size = size;

        if((this->*_allocate_log_entry)(le)) {
            Write_Log_Entry(le, op);
        }
    }

    //Assumes that we have memory allready allocated
    void SLogger::Write_NoOp(int size) {
        Log_Entry le;
        le.type = log_entry_types::control;
        le.size = size;
        //No Op buffer is only here so we have something to put in the NoOp
        memset(no_op_buffer, 0, size);
        Write_Log_Entry(le, no_op_buffer);
    }

    bool SLogger::CAS_Allocate_Log_Entry(Log_Entry &bs) {

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

    void SLogger::Read_Remote_Tail_Pointer() {
            uint64_t local_tail_pointer_address = (uint64_t) _replicated_log.get_tail_pointer_address();
            uint64_t remote_tail_pointer_address = _slog_config->tail_pointer_address;
            uint64_t size = sizeof(uint64_t);
            rdmaReadExp(
                _qp,
                local_tail_pointer_address,
                remote_tail_pointer_address,
                size,
                _log_mr->lkey,
                _slog_config->slog_key,
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
            uint64_t request_size = size + RDMA_READ_REQUSET_SIZE + RDMA_READ_RESPONSE_BASE_SIZE;
            _read_bytes += request_size;
            _total_bytes += request_size;
            _read_operation_bytes += request_size;
            _current_read_rtt++;
            _read_rtt_count++;
            _total_reads++;
            #endif
    }

    void SLogger::Write_Log_Entry(Log_Entry &bs, void* data){
        //we have to have allocated the remote entry at this point.
        //TODO assert somehow that we have allocated
        uint64_t local_log_tail_address = (uint64_t) _replicated_log.get_reference_to_tail_pointer_entry();
        uint64_t remote_log_tail_address = local_to_remote_log_address(local_log_tail_address);
        uint64_t size = bs.Get_Total_Entry_Size();

        // printf("writing to local addr %ul remote log %ul\n", local_log_tail_address, remote_log_tail_address);

        //Make the local change
        _replicated_log.Append_Log_Entry(bs, data);

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

    void SLogger::Sync_To_Last_Write() {
        Syncronize_Log(_replicated_log.get_tail_pointer()); 
    }

    void SLogger::Sync_To_Remote_Log() {
        Read_Remote_Tail_Pointer();
        Syncronize_Log(_replicated_log.get_tail_pointer()); 
    }


    void SLogger::Syncronize_Log(uint64_t offset){
        //Step One reset our local tail pointer and chase to the end of vaild entries
        _replicated_log.Chase_Locally_Synced_Tail_Pointer(); // This will bring us to the last up to date entry

        if (_replicated_log.get_locally_synced_tail_pointer() >= offset) {
            SUCCESS(log_id(), "Log is already up to date");
            return;
        }

        //Locally we have a tail pointer that is behind the adderss we want to sync to.
        //Here we need to read the remote log and find out what the value of the tail pointer is     
        if (_replicated_log.get_tail_pointer() < offset) {
            ALERT(log_id(), "Local tail pointer is behind the offset we want to sync to not yet supported");
            exit(1);
        }

        while (_replicated_log.get_locally_synced_tail_pointer() < offset) {
            //Step Two we need to read the remote log and find out what the value of the tail pointer is
            uint64_t local_log_tail_address = (uint64_t) _replicated_log.get_reference_to_locally_synced_tail_pointer_entry();
            uint64_t remote_log_tail_address = local_to_remote_log_address(local_log_tail_address);
            uint64_t size = offset - _replicated_log.get_locally_synced_tail_pointer();

            // if (_id == 0){
            //     ALERT(log_id(), "Syncing log from %lu to %lu", _replicated_log.get_locally_synced_tail_pointer(), offset);
            // }

            rdmaReadExp(
                _qp,
                local_log_tail_address,
                remote_log_tail_address,
                size,
                _log_mr->lkey,
                _slog_config->slog_key,
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
            uint64_t request_size = size + RDMA_READ_REQUSET_SIZE + RDMA_READ_RESPONSE_BASE_SIZE;
            _read_bytes += request_size;
            _total_bytes += request_size;
            _read_operation_bytes += request_size;
            _current_read_rtt++;
            _read_rtt_count++;
            _total_reads++;
            #endif

            //Finally chase to the end of what we have read. If the entry is not vaild read again.
            _replicated_log.Chase_Locally_Synced_Tail_Pointer(); // This will bring us to the last up to date entry
        }
    }



    bool SLogger::test_insert_log_entry(int i, int size) {


        //Step 1 we are going to allocate some memory for the remote log

        //The assumption is that the local log tail pointer is at the end of the log.
        //For the first step we are going to get the current value of the tail pointer
        #define MAX_LOG_ENTRY_SIZE 4096
        assert(size < MAX_LOG_ENTRY_SIZE);
        char data[MAX_LOG_ENTRY_SIZE];
        const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

        //TODO uncomment for debugging
        for (int j = 0; j < size; j++) {
            data[j] = digits[i%36];
        }
        Log_Entry bs;
        bs.size = size;
        bs.type = _id;

        // bs.repeating_value = digits[i%36];

        //Now we must find out the size of the new entry that we want to commit
        // printf("This is where we are at currently\n");
        // _replicated_log.Print_All_Entries();

        // if (CAS_Allocate_Log_Entry(bs)) {
        // if (FAA_Allocate_Log_Entry(bs)) {
        if((this->*_allocate_log_entry)(bs)) {
            Write_Log_Entry(bs,data);
            Sync_To_Last_Write();
            // if (_id == 0){
            //     _replicated_log.Print_All_Entries();
            // }
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
        } else if (allocate_function == "MFAA") {
            _allocate_log_entry = &SLogger::MFAA_Allocate_Log_Entry;
        } else {
            ALERT("SLOG", "Unknown allocate function %s", allocate_function.c_str());
            exit(1);
        }
    }

    void SLogger::set_workload(string workload) {
        ALERT("setting workload to %s\n", workload.c_str());
        if (workload == "ycsb-a"){
            _workload = A;
        } else if (workload == "ycsb-b"){
            _workload = B;
        } else if (workload == "ycsb-c"){
            _workload = C;
        } else if (workload == "ycsb-w"){
            _workload = W;
        } else {
            ALERT("ERROR", "unknown workload\n");
            throw logic_error("ERROR: unknown workload");
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