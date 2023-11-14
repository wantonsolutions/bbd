#ifndef REPLICATED_LOG_H
#define REPLICATED_LOG_H

#include <stdint.h>
#include "../slib/log.h"
#include <string>

using namespace std;

namespace replicated_log {


    typedef struct Log_Entry {
        uint16_t size;
        uint8_t type;
        string ToString();
        bool is_vaild_entry() {return this->size > 0;}
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
            Log_Entry * Next_Locally_Synced_Tail_Pointer();
            Log_Entry * Next_Operation();





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
            unsigned int get_memory_size() {return this->_memory_size;}

        private:
            Log_Entry * Next(uint64_t *tail_pointer);
            void Chase(uint64_t * tail_pointer);
            unsigned int _memory_size;
            uint8_t* _log;
            //Tail pointer references the remote tail pointer. This value is DMA's to and from directly
            uint64_t _tail_pointer;
            //Local tail pointer is used for tracking complete local updates. This defines the maximum local entries.
            uint64_t _locally_synced_tail_pointer;
            //Operation tail pointer is used by the application to pop off operations from the log seperate from how the log is managed
            uint64_t _operation_tail_pointer;
    };

}

#endif