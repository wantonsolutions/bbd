#include "replicated_log.h"
#include "../slib/log.h"
#include "../slib/util.h"
#include "logger.h"
#include "local_stub_logger.h"

#include <stdint.h>

namespace slogger {
    Local_Stub_Logger::Local_Stub_Logger(unordered_map<string, string> config){
        ALERT("Local Logger", "You should probably be using the default constructor if you are calling this class");

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

            _id = 0;
            _total_clients=1;
            _replicated_log = Replicated_Log(memory_size, _entry_size, _total_clients, _id, _bits_per_client_position);

        } catch (exception& e) {
            ALERT("SLOG", "Error in SLogger constructor: %s", e.what());
            exit(1);
        }   
    }

    Local_Stub_Logger::Local_Stub_Logger(){
        _id = 0;
        _total_clients=1;
        unsigned int memory_size=1024*1024;
        _entry_size=256;
        _bits_per_client_position=64;
        _replicated_log = Replicated_Log(memory_size, _entry_size, _total_clients, _id, _bits_per_client_position);
    }

    const char * Local_Stub_Logger::log_id() {
        return "Local Logger";
    }

    // //Send out a request for client positions along with the fetch and add
    bool Local_Stub_Logger::alloc_log_entries(unsigned int entries) {
        INFO(log_id(), "Not Allocating %d log entries this is unnessisary for local", entries);

        // uint64_t current_tail_value = _replicated_log.get_tail_pointer();
        // current_tail_value += entries;
        // _replicated_log.set_tail_pointer(current_tail_value);
        return true;
    }


    void * Local_Stub_Logger::Next_Operation() {
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

    void * Local_Stub_Logger::Peek_Next_Operation() {
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

    bool Local_Stub_Logger::Write_Operation(void* op, int size) {
        const int single_entry = 1;

        bool success = alloc_log_entries(single_entry);
        if (!success) {
            ALERT("SLOG", "Failed to allocate log entry");
            // In the future we can deal with this failure, for now just die
            assert(false);
            return false;
        }
        Write_Log_Entry(op, size);
        return true;
    }

    bool Local_Stub_Logger::Write_Operations(void ** ops, unsigned int * sizes, unsigned int num_ops) {
        assert(num_ops <= _batch_size);
        assert(num_ops > 0);
        assert(num_ops <= MAX_BATCH_SIZE);

        bool success = alloc_log_entries(num_ops);
        if (!success) {
            ALERT("SLOG", "Failed to allocate %d log entries", num_ops);
            // In the future we can deal with this failure, for now just die
            assert(false);
            return false;
        }

        Write_Log_Entries(ops, sizes, num_ops);
        return true;
    }

    void Local_Stub_Logger::Write_Log_Entry(void* data, unsigned int size) {
        void * data_pointers[1];
        unsigned int sizes[1];
        data_pointers[0] = data;
        sizes[0] = size;
        Write_Log_Entries(data_pointers, sizes, 1);
    }


    void Local_Stub_Logger::Write_Log_Entries(void ** data, unsigned int * sizes, unsigned int num_entries) {

        //we have to have allocated the remote entry at this point.
        //TODO assert somehow that we have allocated
        uint64_t local_log_tail_address = (uint64_t) _replicated_log.get_reference_to_tail_pointer_entry();

        INFO(log_id(), "Writing log entry to tail address -> %lu", local_log_tail_address);
        for (int i=0; i<num_entries; i++) {
            unsigned int in_size = sizes[i];
            assert(_replicated_log.Will_Fit_In_Entry(in_size));
        }

        INFO(log_id(), "Current tail pointer is %lu", _replicated_log.get_tail_pointer());

        //Make the local change
        //TODO batch
        while(!_replicated_log.Can_Append(num_entries)){
            Update_Client_Position(_replicated_log.get_tail_pointer()); // clients will stall if I don't do this
        }
        // _replicated_log.Append_Log_Entries
        for (int i=0; i<num_entries; i++) {
            unsigned int size = sizes[i];
            void * d = data[i];
            _replicated_log.Append_Log_Entry(d, size);
        }
    }

    void Local_Stub_Logger::Sync_To_Last_Write() {
        Syncronize_Log(_replicated_log.get_tail_pointer()); 
    }

    void Local_Stub_Logger::Update_Client_Position(uint64_t new_tail) {
        // if (_replicated_log.tail_pointer_to_client_position(old_tail) == _replicated_log.tail_pointer_to_client_position(new_tail)) {
        if (_replicated_log.get_client_position(_id) == _replicated_log.tail_pointer_to_client_position(new_tail)) {
            INFO(log_id(), "No need to update tail pointer stayed the same");
            //We are up to date
            return;
        } else {
            INFO(log_id(), "Updating Remote Pointer old tail %ld, new tail %ld");
            _replicated_log.update_client_position(new_tail);
        }
    }

    void Local_Stub_Logger::Syncronize_Log(uint64_t offset){

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
            //Step Two we need to read the remote log and find out what the value of the tail pointer is
            uint64_t entries = offset - _replicated_log.get_locally_synced_tail_pointer();

            //Finally chase to the end of what we have read. If the entry is not vaild read again.
            // _replicated_log.Chase_Locally_Synced_Tail_Pointer(); // This will bring us to the last up to date entry
            _replicated_log.Chase_Locally_Synced_Tail_Pointer();
        }

        Update_Client_Position(_replicated_log.get_locally_synced_tail_pointer());

    }
}
