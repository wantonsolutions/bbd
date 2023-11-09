#ifndef REPLICATED_LOG_H
#define REPLICATED_LOG_H

#include <stdint.h>
#include "../slib/log.h"
#include <string>

using namespace std;

namespace replicated_log {

    typedef struct Log_Entry {
        uint16_t entry_size;
        uint8_t entry_type;
        string ToString();
        bool is_vaild_entry() {return this->entry_size > 0;}
        int Get_Total_Entry_Size();
    } Log_Entry;

    class Replicated_Log {
        public:
            Replicated_Log();
            Replicated_Log(unsigned int memory_size);
            // ~Replicated_Log() {ALERT("REPLICATED_LOG", "deleting replicated log");}
            void Append_Log_Entry(Log_Entry &bs, void * data);
            bool Can_Append(Log_Entry &bs);
            void Print_All_Entries();

            void Reset_Tail_Pointer();
            void Chase_Tail_Pointer();
            void Chase_Locally_Synced_Tail_Pointer();



            void * get_log_pointer() {return (void*) this->_log;}
            float get_fill_percentage();
            int get_size_bytes();

            void * get_reference_to_tail_pointer_entry();//this one returns a pointer to the entry at the tail pointer
            void * get_reference_to_locally_synced_tail_pointer_entry();

            uint64_t get_tail_pointer() {return this->_tail_pointer;}
            uint64_t get_locally_synced_tail_pointer() {return this->_locally_synced_tail_pointer;}
            void set_tail_pointer(uint64_t tail_pointer) {this->_tail_pointer = tail_pointer;}
            void * get_tail_pointer_address() {return (void*) &this->_tail_pointer;}
            int get_tail_pointer_size_bytes() {return sizeof(this->_tail_pointer);}

        private:
            void Chase(uint64_t * tail_pointer);
            unsigned int _memory_size;
            uint8_t* _log;
            uint64_t _tail_pointer;
            uint64_t _locally_synced_tail_pointer;
    };

}

#endif