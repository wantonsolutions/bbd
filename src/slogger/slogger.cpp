#include "slogger.h"
#include "rslog.h"
#include "replicated_log.h"
#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"

#include <stdint.h>
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
            _total_clients = stoi(config["num_client_machines"]) * stoi(config["num_clients"]);

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
        stats["sync_calls"]=to_string(_sync_calls);
        stats["sync_calls_retry"]=to_string(_sync_calls_retry);
        stats["stall_count"]=to_string(_stall_count);
        return stats;
    }

    const char * SLogger::log_id() {
        return _log_identifier;
    }

    void SLogger::Clear_Statistics() {
        State_Machine::Clear_Statistics();
        _sync_calls = 0;
        _sync_calls_retry = 0;
        _stall_count = 0;
        SUCCESS(log_id(), "Clearing statistics");
    }

    void SLogger::fsm(){
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
                Clear_Statistics();
            }

            _operation_start_time = get_current_ns();
            bool res = insert_n_sequential_ints(i, _batch_size);
            _operation_end_time = get_current_ns();
            if (!res) {
                break;
            }
            i = (i+_batch_size);

            uint64_t latency = (_operation_end_time - _operation_start_time).count();
            insert_stats(latency, _batch_size);
        }
    }

    bool SLogger::insert_n_sequential_ints(int starting_int, int batch_size) {
        //Step 1 we are going to allocate some memory for the remote log
        //The assumption is that the local log tail pointer is at the end of the log.
        //For the first step we are going to get the current value of the tail pointer
        #define MAX_BATCH_SIZE 256

        int ints[MAX_BATCH_SIZE];
        unsigned int sizes[MAX_BATCH_SIZE];
        void * data_pointers[MAX_BATCH_SIZE];

        for (int i=0; i<batch_size; i++) {
            ints[i] = starting_int + i;
            sizes[i] = sizeof(int);
            data_pointers[i] = (void*)&ints[i];
        }


        //Allocate, write and then sync
        if((this->*_allocate_log_entry)(batch_size)) {

            INFO("SLOG", "Allocated log entry successfully %lu", _replicated_log.get_tail_pointer());
            // Write_Log_Entry(&i, sizeof(int));
            Write_Log_Entries((void**)data_pointers, (unsigned int *)&sizes, batch_size);
            Sync_To_Last_Write();
            return true;
        }
        return false;

    }

    //Send out a request for client positions along with the fetch and add
    bool SLogger::faa_allocate_log_entry(unsigned int entries) {

        uint64_t current_tail_value = _replicated_log.get_tail_pointer();
        _rslog.FAA_Alocate(entries);
        
        _rslog.poll_one();
        assert(current_tail_value <= _replicated_log.get_tail_pointer());

        //At this point we should have a local tail that is at least as large as the remote tail
        //Also the remote tail is allocated
        faa_alloc_stats();
        SUCCESS(log_id(), "FAA Allocated log entry successfully %lu", _replicated_log.get_tail_pointer());
        return true;
    }

    bool SLogger::cas_allocate_log_entry(unsigned int entries) {
        bool allocated = false;
        while(!allocated) {
            uint64_t current_tail_value  = _replicated_log.get_tail_pointer();
            _rslog.CAS_Allocate(entries);
            _rslog.poll_one();
            allocated = (current_tail_value == _replicated_log.get_tail_pointer());
            cas_alloc_stats(allocated);
        }
        SUCCESS(log_id(), "CAS Allocated log entry successfully %lu", _replicated_log.get_tail_pointer());
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

    void * SLogger::Peek_Next_Operation() {
        Entry_Metadata * em = (Entry_Metadata*) _replicated_log.Peek_Next_Operation();
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

    bool SLogger::Write_Operation(void* op, int size) {
        const int single_entry = 1;

        bool success = (this->*_allocate_log_entry)(single_entry);
        if (!success) {
            ALERT("SLOG", "Failed to allocate log entry");
            // In the future we can deal with this failure, for now just die
            assert(false);
            return false;
        }
        Write_Log_Entry(op, size);
        return true;
    }

    bool SLogger::Write_Operations(void ** ops, unsigned int * sizes, unsigned int num_ops) {
        assert(num_ops <= _batch_size);
        assert(num_ops > 0);
        assert(num_ops <= MAX_BATCH_SIZE);

        bool success = (this->*_allocate_log_entry)(num_ops);
        if (!success) {
            ALERT("SLOG", "Failed to allocate %d log entries", num_ops);
            // In the future we can deal with this failure, for now just die
            assert(false);
            return false;
        }

        Write_Log_Entries(ops, sizes, num_ops);
        return true;
    }

    bool SLogger::Update_Remote_Client_Position(uint64_t new_tail){
        uint64_t old_position = _replicated_log.get_client_position(_id);
        uint64_t new_position = _replicated_log.tail_pointer_to_client_position(new_tail);
        uint64_t old_epoch = _replicated_log.get_client_position_epoch(_id);
        uint64_t new_epoch = _replicated_log.get_epoch(new_tail) % 2;
        uint64_t byte_pos = _replicated_log.client_position_byte(_id);
        uint64_t start_pos = byte_pos - (byte_pos % 8);

        INFO(log_id(), "old_position %lu", old_position);
        INFO(log_id(), "new_position %lu", new_position);
        INFO(log_id(), "byte_pos %lu", byte_pos);
        INFO(log_id(), "start_pos %lu", start_pos);
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

        _rslogs.RCAS_Position(old_val, new_val, mask, start_pos);
        _rslogs.poll_one();
        //Get the address of the lock
        // for (int i=0;i<_rslogs.size();i++){
        //     _rslogs[i].RCAS_Position(old_val, new_val, mask, start_pos);
        // }
        // for (int i=0;i<_rslogs.size();i++){
        //     _rslogs[i].poll_one();
        // }


        //Assert that we got the old value back. This prevents errors
        uint64_t returned_position = _replicated_log.get_client_position(_id);
        assert(returned_position == old_position);

        //The write will have overwritten the local copy so we need to update it
        // ALERT(log_id(), "Updating local client position to %lu on id %d", new_position,_id);
        _replicated_log.set_client_position(_id, new_tail);

        return true;
    }

    void SLogger::Read_Remote_Tail_Pointer() {
            _rslog.Read_Tail_Pointer();
            _rslog.poll_one();
            read_tail_stats();
    }

    //Block will wait for the request to terminate before returning.
    //We could batch this with other requests but for now we are going to keep it simple
    void SLogger::Read_Client_Positions(bool block) {
        _rslog.Read_Client_Positions(block);
        if (block) {
            _rslog.poll_one();
        }
        read_position_stats(_replicated_log.get_client_positions_size_bytes());
    }

    void SLogger::Write_Log_Entry(void* data, unsigned int size) {
        void * data_pointers[1];
        unsigned int sizes[1];
        data_pointers[0] = data;
        sizes[0] = size;
        Write_Log_Entries(data_pointers, sizes, 1);
    }


    void SLogger::Write_Log_Entries(void ** data, unsigned int * sizes, unsigned int num_entries) {

        //we have to have allocated the remote entry at this point.
        //TODO assert somehow that we have allocated
        uint64_t local_log_tail_address = (uint64_t) _replicated_log.get_reference_to_tail_pointer_entry();

        if (_id == 1) {
            INFO(log_id(), "Writing log entry to %lu on 1", local_log_tail_address);
        }

        for (int i=0; i<num_entries; i++) {
            unsigned int in_size = sizes[i];
            assert(_replicated_log.Will_Fit_In_Entry(in_size));
        }

        //Make the local change
        //TODO batch
        while(!_replicated_log.Can_Append(num_entries) && !experiment_ended()){
            if (_stall_count % 1000 == 0){
                ALERT(log_id(), "Stalling on slow client! [slow client = %d][stall count %d]",_replicated_log.get_min_client_index(), _stall_count);
            }
            Read_Client_Positions(true);
            Update_Client_Position(_replicated_log.get_tail_pointer()); // clients will stall if I don't do this
            _stall_count++;
            usleep(100);
        }
        // _replicated_log.Append_Log_Entries
        for (int i=0; i<num_entries; i++) {
            unsigned int size = sizes[i];
            void * d = data[i];
            _replicated_log.Append_Log_Entry(d, size);
        }

        _rslogs.Write_Log_Entries(local_log_tail_address, num_entries);
        _rslogs.poll_one();
        write_log_entries_stats(_entry_size*num_entries);


    }

    void SLogger::Sync_To_Last_Write() {
        Syncronize_Log(_replicated_log.get_tail_pointer()); 
    }

    void SLogger::Sync_To_Remote_Log() {
        Read_Remote_Tail_Pointer();
        Syncronize_Log(_replicated_log.get_tail_pointer()); 
    }

    void SLogger::Update_Client_Position(uint64_t new_tail) {
        // if (_replicated_log.tail_pointer_to_client_position(old_tail) == _replicated_log.tail_pointer_to_client_position(new_tail)) {
        if (_replicated_log.get_client_position(_id) == _replicated_log.tail_pointer_to_client_position(new_tail)) {
            INFO(log_id(), "No need to update tail pointer stayed the same");
            //We are up to date
            return;
        } else {
            INFO(log_id(), "Updating Remote Pointer old tail %ld, new tail %ld");
            //Do a read here to keep ourselves up to date
            Read_Client_Positions(false);
            Update_Remote_Client_Position(new_tail);
            _replicated_log.update_client_position(new_tail);
        }
    }

    void SLogger::Batch_Read_Next_N_Entries(int entries){
            //TODO we could batch this and the next read
            uint64_t local_log_tail_address = (uint64_t) _replicated_log.get_reference_to_locally_synced_tail_pointer_entry();
            uint64_t local_entry = _replicated_log.get_locally_synced_tail_pointer() % _replicated_log.get_number_of_entries();
            if (local_entry + entries > _replicated_log.get_number_of_entries()) {
                entries = _replicated_log.get_number_of_entries() - local_entry;
            } 
            uint64_t total_entries = entries;
            _rslogs.Batch_Read_Log(local_log_tail_address, entries);
    }


    void SLogger::Syncronize_Log(uint64_t offset){

        //Stats
        _sync_calls++;

        INFO(log_id(), "Syncronizing log from %lu to %lu", _replicated_log.get_locally_synced_tail_pointer(), offset);
        //Step One reset our local tail pointer and chase to the end of vaild entries
        // _replicated_log.Chase_Locally_Synced_Tail_Pointer(); // This will bring us to the last up to date entry

        //Now we can chase our tail untill we are up to date.
        _replicated_log.Chase_Locally_Synced_Tail_Pointer();

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
            //Stats
            _sync_calls_retry++;

            //Return if experiment is over
            if (experiment_ended()) {return;}
            //Step Two we need to read the remote log and find out what the value of the tail pointer is
            uint64_t entries = offset - _replicated_log.get_locally_synced_tail_pointer();

            //Make sure that we only read up to the end of the log
            //Rond off to the end of the log if we are there
            Batch_Read_Next_N_Entries(entries);
            _rslogs.poll_batch_read();

            //Finally chase to the end of what we have read. If the entry is not vaild read again.
            // _replicated_log.Chase_Locally_Synced_Tail_Pointer(); // This will bring us to the last up to date entry
            _replicated_log.Chase_Locally_Synced_Tail_Pointer();
        }

        Update_Client_Position(_replicated_log.get_locally_synced_tail_pointer());

    }



    void SLogger::set_allocate_function(unordered_map<string, string> config){
        string allocate_function = config["allocate_function"];
        if(allocate_function == "CAS") {
            _allocate_log_entry = &SLogger::cas_allocate_log_entry;
        } else if (allocate_function == "FAA") {
            _allocate_log_entry = &SLogger::faa_allocate_log_entry;
        } else {
            ALERT("SLOG", "Unknown allocate function %s", allocate_function.c_str());
            exit(1);
        }
    }

    void SLogger::set_workload(string workload) {
        INFO(__func__, "%s", workload.c_str());
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

    void SLogger::add_remote(rdma_info info, string name, int memory_server_index){ 
        assert(memory_server_index == (_rslogs.remote_server_count()));
        RSlog nslog = RSlog(info, &_replicated_log, name, memory_server_index);
        _rslogs.Add_Slog(nslog);
        _rslog = _rslogs.get_slog(0);
    }
}
