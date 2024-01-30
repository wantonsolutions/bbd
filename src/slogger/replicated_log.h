#ifndef REPLICATED_LOG_H
#define REPLICATED_LOG_H

#include <stdint.h>
#include "../slib/log.h"
#include <string>

using namespace std;

namespace replicated_log {


    typedef struct Entry_Metadata {
        unsigned type: 1;
        unsigned epoch: 1;
        bool is_vaild_entry(unsigned epoch) {return this->epoch == epoch % 2;}
    } __attribute__((packed)) Entry_Metadata;

    enum log_entry_types {
        control = 0,
        app = 1,
    };

    // typedef struct Log_Entry {
    //     Entry_Metadata metadata;
    //     uint8_t * data;
    //     string ToString();
    //     int Get_Total_Entry_Size();
    // } Log_Entry;

    class Replicated_Log {
        public:
            Replicated_Log();
            Replicated_Log(unsigned int memory_size, unsigned int entry_size);
            // ~Replicated_Log() {ALERT("REPLICATED_LOG", "deleting replicated log");}
            void Append_Log_Entry(void * data, size_t size);
            bool Can_Append();
            void Print_All_Entries();
            bool Will_Fit_In_Entry(size_t size);

            void Reset_Tail_Pointer();
            void Chase_Tail_Pointer();
            void Chase_Locally_Synced_Tail_Pointer();
            void Check_And_Roll_Over_Tail_Pointer(uint64_t *tail_pointer);
            void * Next_Locally_Synced_Tail_Pointer();
            void * Next_Operation();

            void * get_log_pointer() {return (void*) this->_log;}

            int get_log_size_bytes() {return this->_memory_size;}
            int get_entry_size_bytes() {return this->_entry_size;}
            int get_number_of_entries() {return this->_number_of_entries;}

            void * get_reference_to_tail_pointer_entry();//this one returns a pointer to the entry at the tail pointer
            void * get_reference_to_locally_synced_tail_pointer_entry();

            uint64_t get_tail_pointer() {return this->_tail_pointer;}
            uint64_t get_locally_synced_tail_pointer() {return this->_locally_synced_tail_pointer;}
            void set_tail_pointer(uint64_t tail_pointer) {this->_tail_pointer = tail_pointer;}
            void * get_tail_pointer_address() {return (void*) &this->_tail_pointer;}
            int get_tail_pointer_size_bytes() {return sizeof(this->_tail_pointer);}
            unsigned int get_memory_size() {return this->_memory_size;}
            unsigned int get_epoch() {return this->_epoch;}

        private:
            void * Next(uint64_t *tail_pointer);
            void Chase(uint64_t * tail_pointer);
            unsigned int _memory_size;
            unsigned int _entry_size;
            unsigned int _number_of_entries;
            unsigned int _epoch;
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