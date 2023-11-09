#include "replicated_log.h"
#include "string.h"

namespace replicated_log {


    string Basic_Entry::ToString(){
        string s = "[entry_size: " + to_string(entry_size) + ", entry_type: " + to_string(entry_type) + ", repeating_value: " + (char)repeating_value + "]";
        return s;
    }

    int Basic_Entry::Get_Total_Entry_Size(){
        return this->entry_size + sizeof(Basic_Entry);
    }

    Replicated_Log::Replicated_Log(unsigned int memory_size) {
        this->_memory_size = memory_size;
        this->_log = new uint8_t[memory_size];
        this->_tail_pointer = 0;
        this->_locally_synced_tail_pointer = 0;
    }

    Replicated_Log::Replicated_Log(){
        this->_memory_size = 0;
        this->_log = NULL;
        this->_tail_pointer = 0;
        this->_locally_synced_tail_pointer = 0;
    }

    float Replicated_Log::get_fill_percentage() {
        // ALERT("Replicated LOG", "TODO calculate the fill percentage");
        this->Chase_Tail_Pointer();
        if (this->_memory_size > 0) {
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

    bool Replicated_Log::Can_Append(Basic_Entry &bs) {
        int total_entry_size = bs.entry_size + sizeof(Basic_Entry);
        int remaining_size = this->_memory_size - this->_tail_pointer;
        if (remaining_size < total_entry_size) {
            ALERT("REPLICATED_LOG", "not enough space in log. Total size %d, remaining size %d, log size %d", total_entry_size, remaining_size, this->_memory_size);
            return false;
        }
        return true;
    }


    void Replicated_Log::Append_Basic_Entry(Basic_Entry &bs) {

        int total_entry_size = bs.entry_size + sizeof(Basic_Entry);
        if (!this->Can_Append(bs)) {
            return;
        }
        uint64_t old_tail_pointer = (uint64_t) this->_log + this->_tail_pointer;
        this->_tail_pointer += total_entry_size;
        memcpy((void*) old_tail_pointer, (void*) &bs, sizeof(Basic_Entry));
        memset((void*) (old_tail_pointer + sizeof(Basic_Entry)), bs.repeating_value, bs.entry_size);
    }

    void Replicated_Log::Print_All_Entries() {
        uint64_t current_pointer = (uint64_t) this->_log;
        // while (current_pointer < (uint64_t) this->get_reference_to_tail_pointer_entry()) {
        while (current_pointer < (uint64_t) this->_log + (uint64_t) this->get_locally_synced_tail_pointer()) {
            Basic_Entry* bs = (Basic_Entry*) current_pointer;
            //Copy repeating values to buffer and print as a string
            char* repeating_values = new char[bs->entry_size + 1];
            repeating_values[bs->entry_size] = '\0';
            memcpy((void*) repeating_values, (void*) (current_pointer + sizeof(Basic_Entry)), bs->entry_size);
            ALERT("REPLICATED_LOG", "%s -> [%s]", bs->ToString().c_str(), repeating_values);

            delete[] repeating_values;
            current_pointer += bs->entry_size + sizeof(Basic_Entry);
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
        Basic_Entry * bs = (Basic_Entry*) ((uint64_t) this->_log + *tail_pointer);
        while(bs->is_vaild_entry()) {
            *tail_pointer += bs->entry_size + sizeof(Basic_Entry);
            bs = (Basic_Entry*) ((uint64_t) this->_log + *tail_pointer);
        }
    }
}