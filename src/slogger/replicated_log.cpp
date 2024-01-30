#include "replicated_log.h"
#include "string.h"
#include "assert.h"
#include "../slib/util.h"

namespace replicated_log {


    Replicated_Log::Replicated_Log(unsigned int memory_size, unsigned int entry_size) {
        assert(memory_size > 0);
        assert(entry_size > 0);
        //make sure memory is a multiple of entry size
        assert(memory_size % entry_size == 0);
        assert(IsPowerOfTwo(memory_size));

        this->_entry_size = entry_size;
        this->_memory_size = memory_size;
        this->_number_of_entries = memory_size / entry_size;

        this->_log = new uint8_t[memory_size];
        ALERT("REPLICATED_LOG", " TOOD mesetting memory, figure out how to avoid this by validating logs");
        memset(this->_log, 0, memory_size);

        this->_tail_pointer = 0;
        this->_locally_synced_tail_pointer = 0;
        this->_operation_tail_pointer = 0;
    }

    Replicated_Log::Replicated_Log(){
        this->_memory_size = 0;
        this->_entry_size = 0;
        this->_log = NULL;
        this->_tail_pointer = 0;
        this->_locally_synced_tail_pointer = 0;
        this->_operation_tail_pointer = 0;
    }

    void * Replicated_Log::get_reference_to_tail_pointer_entry() {
        return (void*) ((uint64_t) this->_log) + (get_entry(this->_tail_pointer) * this->_entry_size);
    }

    void * Replicated_Log::get_reference_to_locally_synced_tail_pointer_entry() {
        return (void*) ((uint64_t) this->_log) + (get_entry(this->_locally_synced_tail_pointer) * this->_entry_size);
    }

    bool Replicated_Log::Can_Append() {
        return this->_number_of_entries - this->_tail_pointer > 1;
    }

    bool Replicated_Log::Will_Fit_In_Entry(size_t size) {
        if (size + sizeof(Entry_Metadata) > this->_entry_size) {
            return false;
        }
        return true;
    }


    void Replicated_Log::Append_Log_Entry(void * data, size_t size) {
        ALERT("Append Entry", "[%5d] epoch[%d]: size: %d and value %d", get_entry(this->_tail_pointer), get_epoch(this->_tail_pointer), size, *(int *)data);
        assert(sizeof(Entry_Metadata) + size <= this->_entry_size); // we must be able to fit the entry in the log
        void* old_tail_pointer = get_reference_to_tail_pointer_entry();
        Entry_Metadata em;
        em.type = app;
        em.epoch = get_epoch(this->_tail_pointer) % 2;

        this->_tail_pointer++;
        // Check_And_Roll_Over_Tail_Pointer(&this->_tail_pointer);

        bzero((void*) old_tail_pointer, this->_entry_size);
        memcpy((void*) old_tail_pointer, (void*) &em, sizeof(Entry_Metadata));
        memcpy((void*) (old_tail_pointer + sizeof(Entry_Metadata)), data, size);

    }

    void Replicated_Log::Print_All_Entries() {
        ALERT("REPLICATED_LOG", "printing all entries -- I think this function is broken");
        uint64_t current_pointer = (uint64_t) this->_log;
        // while (current_pointer < (uint64_t) this->get_reference_to_tail_pointer_entry()) {
        char *buf = new char[this->_entry_size + 1];
        while (current_pointer < (uint64_t) this->_log + (uint64_t) this->_number_of_entries * this->_entry_size){
            // Log_Entry* bs = (Log_Entry*) current_pointer;
            Entry_Metadata* em = (Entry_Metadata*) current_pointer;

            // memcpy((void*) buf, (void*) (current_pointer + sizeof(Entry_Metadata)), this->_entry_size - sizeof(Entry_Metadata));
            memcpy((void*) buf, (void*) (current_pointer), this->_entry_size);
            int value = *(int*) (buf + sizeof(Entry_Metadata));
            ALERT("REPLICATED_LOG (BROKEN)", "entry type %d, epoch %d, value %d", em->type, em->epoch, value);
            //Copy repeating values to buffer and print as a string
            // buf[this->_entry_size] = '\0';
            // ALERT("REPLICATED_LOG", "[%s]", buf);
            current_pointer += this->_entry_size;
        }
        delete[] buf;
    }

    void Replicated_Log::Reset_Tail_Pointer() {
        this->_tail_pointer = 0;
    }

    void Replicated_Log::Chase_Tail_Pointer() {
        ALERT("Chasing Tail Pointer", "Start Index %d", this->_tail_pointer);
        Chase(&this->_tail_pointer);
    }

    void Replicated_Log::Chase_Locally_Synced_Tail_Pointer() {
        ALERT("Chasing Locally Synced Tail Pointer", "Start Index %d", this->_locally_synced_tail_pointer);
        Chase(&this->_locally_synced_tail_pointer);
    }

    void Replicated_Log::Chase(uint64_t * tail_pointer) {
        Entry_Metadata * em = (Entry_Metadata*) ((uint64_t) this->_log + (get_entry(*tail_pointer) * this->_entry_size));
        //Print the epoch of the entry metadata and the epoch of the tail pointer
        ALERT("Chase", "[%d] epoch %d em-epoch[%d]", get_entry(*tail_pointer), get_epoch(*tail_pointer), em->epoch);
        //Print the entry we are chasing
        while(em->is_vaild_entry(get_epoch(*tail_pointer))) {
            ALERT("Chase", "[%d] epoch %d", get_entry(*tail_pointer), get_epoch(*tail_pointer));
            (*tail_pointer)++;
            em = (Entry_Metadata*) ((uint64_t) this->_log + (get_entry(*tail_pointer) * this->_entry_size));
        }
    }

    void * Replicated_Log::Next_Locally_Synced_Tail_Pointer(){
        return Next(&this->_locally_synced_tail_pointer);
    }

    void * Replicated_Log::Next_Operation(){
        return Next(&this->_operation_tail_pointer);
    }

    void * Replicated_Log::Next(uint64_t *tail_pointer) {
        Entry_Metadata * em = (Entry_Metadata*) ((uint64_t) this->_log + (*tail_pointer * this->_entry_size));
        if (em->is_vaild_entry(get_epoch(*tail_pointer))) {
            (*tail_pointer)++;
            return (void*) ((uint64_t) this->_log + (*tail_pointer * this->_entry_size));
        }
        return NULL;
    }
}