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
            _entry_size = stoi(config["entry_size"]);
            if (_entry_size < 1) {
                ALERT("SLOG", "Error: entry size must be greater than 0");
                exit(1);
            }
            if (!IsPowerOfTwo(_entry_size)) {
                ALERT("SLOG", "Error: entry size must be a power of 2, input was %d", _entry_size);
                exit(1);
            }
            if (memory_size % _entry_size != 0) {
                ALERT("SLOG", "Error: memory size must be a multiple of entry size");
                exit(1);
            }

            _batch_size = stoi(config["batch_size"]);            
            _bits_per_client_position = stoi(config["bits_per_client_position"]);

            ALERT("SLOG", "Creating SLogger with id %s", config["id"].c_str());
            _id = stoi(config["id"]);
            sprintf(_log_identifier, "Client: %3d", stoi(config["id"]));

            ALERT("SLOG", "TODO ensure that we don't have duplicate client ID's");
            _total_clients = stoi(config["global_clients"]);

            //This is merely a safty concern I don't want to be allocating over the end of the log
            assert(_total_clients * _batch_size < 2 * (memory_size / _entry_size));

            _replicated_log = Replicated_Log(memory_size, _entry_size, _total_clients, _id, _bits_per_client_position);
            _workload_driver = Client_Workload_Driver(config);

            set_allocate_function(config);
            set_workload(config["workload"]);

            _local_prime_flag = false;
        } catch (exception& e) {
            ALERT("SLOG", "Error in SLogger constructor: %s", e.what());
            exit(1);
        }   

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

        int adjusted_entry_size = _entry_size - sizeof(Entry_Metadata);
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
            i = (i+1);

            bool res = test_insert_log_entry(i, adjusted_entry_size);
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

    bool SLogger::FAA_Allocate_Log_Entry(unsigned int entries) {

        // printf("FETCH AND ADD\n");
        uint64_t local_tail_pointer_address = (uint64_t) _replicated_log.get_tail_pointer_address();
        uint64_t remote_tail_pointer_address = _slog_config->tail_pointer_address;
        uint64_t add  = entries;
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

    bool SLogger::MFAA_Allocate_Log_Entry(unsigned int entries) {
        // printf("FETCH AND ADD\n");
        uint64_t local_tail_pointer_address = (uint64_t) _replicated_log.get_tail_pointer_address();
        uint64_t remote_tail_pointer_address = _slog_config->tail_pointer_address;
        uint64_t add  = entries;
        uint64_t current_tail_value = _replicated_log.get_tail_pointer();

        //Print out all ther relevant information
        ALERT("SLOG", "MFAA_Allocate_Log_Entry");
        ALERT("SLOG", "local_tail_pointer_address %lu", local_tail_pointer_address);
        ALERT("SLOG", "remote_tail_pointer_address %lu", remote_tail_pointer_address);
        ALERT("SLOG", "add %lu", add);
        ALERT("SLOG", "current_tail_value %lu", current_tail_value);
        ALERT("SLOG", "tail_pointer_mr->lkey %lu", _tail_pointer_mr->lkey);

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

        printf("FETCH AND ADD DONE tail value at the time of the faa is %lu\n", _replicated_log.get_tail_pointer());


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


    void * SLogger::Next_Operation() {
        Entry_Metadata * em = (Entry_Metadata*) _replicated_log.Next_Operation();
        if (em == NULL) {
            return NULL;
        }
        //TODO here is where we would put controls in the log.
        //For now we just return the data.
        //For instance, if we wanted to have contorl log entries with different types we could embed them here.
        if (em->type == log_entry_types::control) {
            ALERT("SLOG", "Got a control log entry");
        }

        //return a pointer to the data of the log entry. Leave it to the application to figure out what's next;
        return (void *) em + sizeof(Entry_Metadata);
    }

    void SLogger::Write_Operation(void* op, int size) {

        assert(_replicated_log.Will_Fit_In_Entry(size));
        unsigned int entries_per_insert = 1;

        if((this->*_allocate_log_entry)(1)) {
            Write_Log_Entry(op, size);
        }
    }

    bool SLogger::Send_Client_Tail_Update(uint64_t old_tail, uint64_t new_tail){
        struct ibv_sge sg;
        struct ibv_exp_send_wr wr;

        uint64_t old_position = _replicated_log.tail_pointer_to_client_position(old_tail);
        uint64_t new_position = _replicated_log.tail_pointer_to_client_position(new_tail);

        uint64_t old_epoch = _replicated_log.get_epoch(old_tail) % 2;
        uint64_t new_epoch = _replicated_log.get_epoch(new_tail) % 2;

        uint64_t byte_pos = _replicated_log.client_position_byte(_id);

        //Round old_byte to the nearest 8 byte boundary
        uint64_t start_pos = byte_pos - (byte_pos % 8);

        INFO(log_id(), "Setting client tail update from %lu to %lu", old_tail, new_tail);
        INFO(log_id(), "old_position %lu", old_position);
        INFO(log_id(), "new_position %lu", new_position);
        // ALERT(log_id(), "byte_pos %lu", byte_pos);
        // ALERT(log_id(), "start_pos %lu", start_pos);
        INFO(log_id(), "old_epoch %lu", old_epoch);
        INFO(log_id(), "new_epoch %lu", new_epoch);

        
        //Set two uin64 values with old position and new position
        int starting_bit_offset = _replicated_log.client_position_bit(_id) + (_replicated_log.client_position_byte(_id) * 8);
        INFO(log_id(), "starting_bit_offset %d", starting_bit_offset);
        INFO(log_id(), "client position byte %d", _replicated_log.client_position_byte(_id));
        uint64_t new_val = 0;
        uint64_t old_val = 0;
        uint64_t mask = 0;
        uint64_t one = 1;

        //Shift in epoch
        uint64_t epoch_shift = starting_bit_offset;
        mask    |= one << epoch_shift;
        old_val |= (old_epoch & one) << epoch_shift;
        new_val |= (new_epoch & one) << epoch_shift;

        starting_bit_offset++;
        for (int i=0;i<_replicated_log.bits_per_position(); i++) {
            mask    |= one << (starting_bit_offset + i);
            old_val |= (old_position & (one << i)) << (starting_bit_offset);
            new_val |= (new_position & (one << i)) << (starting_bit_offset);
        }
        //Print out the hex of each value
        INFO(log_id(), "old_val %16lx", old_val);
        INFO(log_id(), "new_val %16lx", new_val);
        INFO(log_id(), "mask    %16lx", mask);

        //Get the address of the lock
        uint64_t local_address = (uint64_t) _replicated_log.get_client_positions_pointer() + start_pos;
        uint64_t remote_address = _slog_config->client_position_table_address + start_pos;

        bool success = rdmaCompareAndSwapMask(
            _qp,
            local_address,
            remote_address,
            old_val,
            new_val,
            _client_position_table_mr->lkey,
            _slog_config->client_position_table_key,
            mask,
            true,
            _wr_id);

        _wr_id++;
        int outstanding_messages = 1;
        int n = bulk_poll(_completion_queue, outstanding_messages, _wc);

        //Assert that we got the old value back. This prevents errors
        uint64_t read_position = _replicated_log.get_client_position(_id);

        if (read_position != old_position) {
            ALERT(log_id(), "Read position %lu does not match old position %lu", read_position, old_position);
            ALERT(log_id(), "HEX TABLE");
            _replicated_log.print_client_position_raw_hex();
            ALERT(log_id(), "FORMAT TABLE");
            _replicated_log.print_client_positions();
            assert(read_position == old_position);
        }

        //The write will have overwritten the local copy
        _replicated_log.set_client_position(_id, new_tail);

        return success;
    }

    bool SLogger::CAS_Allocate_Log_Entry(unsigned int entries) {

        bool allocated = false;

        while(!allocated) {
            uint64_t local_tail_pointer_address = (uint64_t) _replicated_log.get_tail_pointer_address();
            uint64_t remote_tail_pointer_address = _slog_config->tail_pointer_address;
            uint64_t compare  = _replicated_log.get_tail_pointer();
            uint64_t new_tail_pointer = compare + entries;
            uint64_t current_tail_value = compare;

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

    void SLogger::Read_Client_Positions() {
        uint64_t local_address = (uint64_t) _replicated_log.get_client_positions_pointer();
        uint64_t remote_address = _slog_config->client_position_table_address;
        uint64_t size = _replicated_log.get_client_positions_size_bytes();
        rdmaReadExp(
            _qp,
            local_address,
            remote_address,
            size,
            _client_position_table_mr->lkey,
            _slog_config->client_position_table_key,
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


    void SLogger::Write_Log_Entry(void* data, unsigned int in_size){
        //we have to have allocated the remote entry at this point.
        //TODO assert somehow that we have allocated
        assert(_replicated_log.Will_Fit_In_Entry(in_size));
        uint64_t local_log_tail_address = (uint64_t) _replicated_log.get_reference_to_tail_pointer_entry();
        uint64_t remote_log_tail_address = local_to_remote_log_address(local_log_tail_address);
        uint64_t size = in_size;

        // printf("writing to local addr %ul remote log %ul\n", local_log_tail_address, remote_log_tail_address);

        //Make the local change
        while(!_replicated_log.Can_Append()){
            Read_Client_Positions();
        }
        _replicated_log.Append_Log_Entry(data, size);

        rdmaWriteExp(
            _qp,
            local_log_tail_address,
            remote_log_tail_address,
            this->_entry_size,
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

    void SLogger::Chase_Tail_And_Update_Remote() {
        uint64_t old_tail = _replicated_log.get_locally_synced_tail_pointer();
        _replicated_log.Chase_Locally_Synced_Tail_Pointer(); // This will bring us to the last up to date entry
        uint64_t new_tail = _replicated_log.get_locally_synced_tail_pointer();
        if (_replicated_log.tail_pointer_to_client_position(old_tail) == _replicated_log.tail_pointer_to_client_position(new_tail)) {
            INFO(log_id(), "No need to update tail pointer stayed the same");
            //We are up to date
            return;
        } else {
            INFO(log_id(), "Updating Remote Pointer old tail %ld, new tail %ld");
            Send_Client_Tail_Update(old_tail, new_tail);
            _replicated_log.update_client_position(new_tail);
        }
    }


    void SLogger::Syncronize_Log(uint64_t offset){

        // ALERT(log_id(), "Syncronizing log from %lu to %lu", _replicated_log.get_locally_synced_tail_pointer(), offset);
        //Step One reset our local tail pointer and chase to the end of vaild entries
        // _replicated_log.Chase_Locally_Synced_Tail_Pointer(); // This will bring us to the last up to date entry
        Chase_Tail_And_Update_Remote();

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
            uint64_t entries = offset - _replicated_log.get_locally_synced_tail_pointer();

            //Make sure that we only read up to the end of the log
            //Rond off to the end of the log if we are there
            //TODO we could batch this and the next read
            uint64_t local_entry = _replicated_log.get_locally_synced_tail_pointer() % _replicated_log.get_number_of_entries();
            if (local_entry + entries > _replicated_log.get_number_of_entries()) {
                entries = _replicated_log.get_number_of_entries() - local_entry;
            } 
            uint64_t size = entries * _entry_size;

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
            // _replicated_log.Chase_Locally_Synced_Tail_Pointer(); // This will bring us to the last up to date entry
            Chase_Tail_And_Update_Remote();
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
        unsigned int entries_per_insert = 1;

        // bs.repeating_value = digits[i%36];

        //Now we must find out the size of the new entry that we want to commit
        // printf("This is where we are at currently\n");
        // _replicated_log.Print_All_Entries();

        // if (CAS_Allocate_Log_Entry(bs)) {
        // if (FAA_Allocate_Log_Entry(bs)) {
        if((this->*_allocate_log_entry)(entries_per_insert)) {
            INFO("SLOG", "Allocated log entry successfully %lu", _replicated_log.get_tail_pointer());
            // Write_Log_Entry(data, size);
            Write_Log_Entry(&i, sizeof(int));
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
        INFO(__func__: "%s", workload.c_str());
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

        INFO("SLOG", "SLogger Initializing RDMA Structures");
        assert(info.qp != NULL);
        assert(info.completion_queue != NULL);
        assert(info.pd != NULL);

        _qp = info.qp;
        _completion_queue = info.completion_queue;
        _protection_domain = info.pd;

        _slog_config = memcached_get_slog_config();
        assert(_slog_config != NULL);
        assert(_slog_config->slog_size_bytes == _replicated_log.get_log_size_bytes());
        INFO(log_id(),"got a slog config from the memcached server and it seems to line up\n");

        SUCCESS("SLOG", "Set RDMA Structs from memcached server");

        // INFO(log_id(), "Registering table with RDMA device size %d, location %p\n", get_table_size_bytes(), get_table_pointer()[0]);
        _log_mr = rdma_buffer_register(_protection_domain, _replicated_log.get_log_pointer(), _replicated_log.get_log_size_bytes(), MEMORY_PERMISSION);
        // INFO(log_id(), "Registering lock table with RDMA device size %d, location %p\n", get_lock_table_size_bytes(), get_lock_table_pointer());
        _tail_pointer_mr = rdma_buffer_register(_protection_domain, _replicated_log.get_tail_pointer_address(), _replicated_log.get_tail_pointer_size_bytes(), MEMORY_PERMISSION);
        _client_position_table_mr = rdma_buffer_register(_protection_domain, _replicated_log.get_client_positions_pointer(), _replicated_log.get_client_positions_size_bytes(), MEMORY_PERMISSION);

        _wr_id = 10000000;
        _wc = (struct ibv_wc *) calloc (MAX_CONCURRENT_MESSAGES, sizeof(struct ibv_wc));
        SUCCESS("SLOG", "Done registering memory regions for SLogger");

    }

}