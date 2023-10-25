#include "replicated_log.h"
#include "string.h"

namespace replicated_log {


    string Basic_Entry::ToString(){
        string s = "[entry_size: " + to_string(entry_size) + ", entry_type: " + to_string(entry_type) + ", repeating_value: " + (char)repeating_value + "]";
        return s;
    }

    Replicated_Log::Replicated_Log(unsigned int memory_size) {
        this->_memory_size = memory_size;
        this->_log = new uint8_t[memory_size];
        this->_tail_pointer = (uint64_t) this->_log;
    }

    Replicated_Log::Replicated_Log(){
        this->_memory_size = 0;
        this->_log = NULL;
        this->_tail_pointer = (uint64_t) this->_log;
    }

    int Replicated_Log::get_size_bytes(){
        return this->_memory_size;
    }

    void Replicated_Log::Append_Basic_Entry(Basic_Entry &bs) {
        int total_entry_size = bs.entry_size + sizeof(Basic_Entry);
        int fill_size = this->_tail_pointer - (uint64_t) this->_log;
        if (this->_memory_size - fill_size < total_entry_size) {
            ALERT("REPLICATED_LOG", "not enough space in log");
            return;
        }
        uint64_t old_tail_pointer = this->_tail_pointer;
        this->_tail_pointer += total_entry_size;
        memcpy((void*) old_tail_pointer, (void*) &bs, sizeof(Basic_Entry));
        memset((void*) (old_tail_pointer + sizeof(Basic_Entry)), bs.repeating_value, bs.entry_size);
    }

    void Replicated_Log::Print_All_Entries() {
        uint64_t current_pointer = (uint64_t) this->_log;
        while (current_pointer < this->_tail_pointer) {
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
}