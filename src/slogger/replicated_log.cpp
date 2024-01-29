#include "replicated_log.h"
#include "string.h"

namespace replicated_log {

    string Log_Entry::ToString(){
        string s = "[entry_size: " + to_string(size) + ", entry_type: " + to_string(type) + "]";
        return s;
    }

    int Log_Entry::Get_Total_Entry_Size(){
        return this->size + sizeof(Log_Entry);
    }

    Replicated_Log::Replicated_Log(unsigned int memory_size) {
        this->_memory_size = memory_size;
        this->_log = new uint8_t[memory_size];
        ALERT("REPLICATED_LOG", " TOOD mesetting memory, figure out how to avoid this by validating logs");
        memset(this->_log, 0, memory_size);

        this->_tail_pointer = 0;
        this->_locally_synced_tail_pointer = 0;
        this->_operation_tail_pointer = 0;
    }

    Replicated_Log::Replicated_Log(){
        this->_memory_size = 0;
        this->_log = NULL;
        this->_tail_pointer = 0;
        this->_locally_synced_tail_pointer = 0;
        this->_operation_tail_pointer = 0;
    }

    float Replicated_Log::get_fill_percentage() {
        // ALERT("Replicated LOG", "TODO calculate the fill percentage");
        this->Chase_Tail_Pointer();
        if (this->_memory_size > 0) {
            // ALERT("Replicated LOG", "tail pointer %d, memory size %d", this->_tail_pointer, this->_memory_size);
            return (float) this->_tail_pointer / (float) this->_memory_size;
        }
    }

    int Replicated_Log::get_size_bytes(){
        return this->_memory_size;
    }

    void * Replicated_Log::get_reference_to_tail_pointer_entry() {
        return (void*) ((uint64_t) this->_log) + this->_tail_pointer;
    }

    void * Replicated_Log::get_reference_to_locally_synced_tail_pointer_entry() {
        return (void*) ((uint64_t) this->_log) + this->_locally_synced_tail_pointer;
    }

    bool Replicated_Log::Can_Append(Log_Entry &bs) {
        int total_entry_size = bs.size + sizeof(Log_Entry);
        int remaining_size = this->_memory_size - this->_tail_pointer;
        if (remaining_size < total_entry_size) {
            ALERT("REPLICATED_LOG", "not enough space in log. Total size %d, remaining size %d, log size %d", total_entry_size, remaining_size, this->_memory_size);
            return false;
        }
        return true;
    }

    void Replicated_Log::Check_And_Roll_Over_Tail_Pointer(uint64_t *tail_pointer) {
        if (*tail_pointer == this->_memory_size) {
            ALERT("REPLICATED_LOG", "we are perfectly at the end of the log rolling over\n");
            *tail_pointer = 0;
        }
    }


    void Replicated_Log::Append_Log_Entry(Log_Entry &bs, void * data) {

        int total_entry_size = bs.size + sizeof(Log_Entry);
        if (!this->Can_Append(bs)) {
            ALERT("REPLICATED_LOG", "not enough space in log. Total size %d, remaining size %d, log size %d", total_entry_size, this->_memory_size - this->_tail_pointer, this->_memory_size);
            ALERT("REPLICATED_LOG", "we are not at the end of the log, something is wrong\n");
            exit(0);
        } 
        uint64_t old_tail_pointer = (uint64_t) this->_log + this->_tail_pointer;
        this->_tail_pointer += total_entry_size;
        memcpy((void*) old_tail_pointer, (void*) &bs, sizeof(Log_Entry));
        memcpy((void*) (old_tail_pointer + sizeof(Log_Entry)), data, bs.size);

        Check_And_Roll_Over_Tail_Pointer(&this->_tail_pointer);
    }

    void Replicated_Log::Print_All_Entries() {
        uint64_t current_pointer = (uint64_t) this->_log;
        // while (current_pointer < (uint64_t) this->get_reference_to_tail_pointer_entry()) {
        while (current_pointer < (uint64_t) this->_log + (uint64_t) this->get_locally_synced_tail_pointer()) {
            Log_Entry* bs = (Log_Entry*) current_pointer;
            //Copy repeating values to buffer and print as a string
            char* data = new char[bs->size + 1];
            data[bs->size] = '\0';
            memcpy((void*) data, (void*) (current_pointer + sizeof(Log_Entry)), bs->size);
            ALERT("REPLICATED_LOG", "%s -> [%s]", bs->ToString().c_str(), data);

            delete[] data;
            current_pointer += bs->size + sizeof(Log_Entry);
        }
    }

    void Replicated_Log::Reset_Tail_Pointer() {
        this->_tail_pointer = 0;
    }

    void Replicated_Log::Chase_Tail_Pointer() {
        Chase(&this->_tail_pointer);
    }

    void Replicated_Log::Chase_Locally_Synced_Tail_Pointer() {
        Chase(&this->_locally_synced_tail_pointer);
    }

    void Replicated_Log::Chase(uint64_t * tail_pointer) {
        Log_Entry * bs = (Log_Entry*) ((uint64_t) this->_log + *tail_pointer);
        while(bs->is_vaild_entry()) {
            *tail_pointer += bs->size + sizeof(Log_Entry);
            //Perform local roll over
            Check_And_Roll_Over_Tail_Pointer(tail_pointer);
            bs = (Log_Entry*) ((uint64_t) this->_log + *tail_pointer);
        }
    }

    Log_Entry * Replicated_Log::Next_Locally_Synced_Tail_Pointer(){
        return Next(&this->_locally_synced_tail_pointer);
    }

    Log_Entry * Replicated_Log::Next_Operation(){
        return Next(&this->_operation_tail_pointer);
    }

    Log_Entry * Replicated_Log::Next(uint64_t *tail_pointer) {
        Log_Entry * le = (Log_Entry*) ((uint64_t) this->_log + *tail_pointer);
        if (le->is_vaild_entry()) {
            *tail_pointer += le->size + sizeof(Log_Entry);
            return le;
        }
        return NULL;
    }
}