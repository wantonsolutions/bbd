#include "replicated_log.h"
#include "string.h"
#include "assert.h"
#include "../slib/util.h"

namespace slogger {


    void Replicated_Log::allocate_client_positions(unsigned int total_clients, unsigned int bits_per_entry) {
        assert(bits_per_entry > 0);
        assert(total_clients > 0);
        assert(bits_per_entry < 64);
        assert(IsPowerOfTwo(bits_per_entry) + 1);
        int bits = (bits_per_entry + 1);
        int total_bits = bits * total_clients;
        int total_bytes = 0;
        if (total_bits % 8 == 0) {
            total_bytes = total_bits / 8;
        } else {
            total_bytes = (total_bits / 8) + 1;
        }
        //Round up total bytes to the nearest 8 byte boundary
        if (total_bytes % 8 != 0) {
            total_bytes = total_bytes + (8 - (total_bytes % 8));
        }
        _client_positions_size_bytes = total_bytes;
        INFO("Allocate Client Positions", "total clients %d, bits per entry %d, total bits %d, total bytes %d", total_clients, bits_per_entry, total_bits, total_bytes);
        _client_positions = new uint8_t[total_bytes];
        //Zero out client positions
        memset(_client_positions, 0, total_bytes);

        //Set the epoch of each entry to 1
        for (int i=0;i<total_clients;i++) {
            set_client_position_epoch(i, 1);
        }
        // ALERT("Allocate Client Positions", "client positions size bytes %d", _client_positions_size_bytes);
        // print_client_position_raw_hex();
    }


    void Replicated_Log::print_client_positions() {
        for (int i=0;i<this->_total_clients;i++) {
            // ALERT("Client Positions", "client %d, position %d", i, this->_client_positions[i]);

            // epoch is the first bit in the client position
            // int epoch = (_client_positions[starting_byte] & (1 << starting_bit)) >> starting_bit;
            int epoch = get_client_position_epoch(i);
            uint64_t position = get_client_position(i);
            uint64_t entry = get_client_entry(i);

            ALERT("Client Positions", "client %d, position %ld, entry %ld epoch %d", i, position, entry, epoch);
        }
    }

    void Replicated_Log::print_client_position_raw_hex() {
        for (int i=0;i<this->_client_positions_size_bytes;i++) {
            ALERT("Client Positions", "client %d, position %x", i, this->_client_positions[i]);
        }
    }

    uint64_t Replicated_Log::get_min_client_index() {
        if (this->_total_clients == 1) {
            return 0;
        }
        int min_client = 0;
        uint64_t min_position = get_client_position(0);
        uint64_t min_epoch = get_client_position_epoch(0);

        for (int i=1;i<this->_total_clients;i++) {
            uint64_t position = get_client_position(i);
            uint64_t epoch = get_client_position_epoch(i);

            if (min_epoch == epoch && position < min_position) {
                min_position = position;
                min_epoch = epoch;
                min_client = i;
            }

            if (epoch != min_epoch && position > min_position) {
                min_position = position;
                min_epoch = epoch;
                min_client = i;
            }
        }
        return min_client;
    }

    uint64_t Replicated_Log::get_min_client_position() {
        uint64_t min_client_index = get_min_client_index();
        uint64_t position =  get_client_position(min_client_index);
        return position_to_entry(position);
        VERBOSE("Min Client Position", "client %d, position %ld", min_client_index, position);
    }

    void Replicated_Log::update_client_position(uint64_t tail_pointer) {

        int epoch = get_epoch(tail_pointer) % 2;
        set_client_position_epoch(this->_client_id, epoch);
        set_client_position(this->_client_id, tail_pointer);

    }

    Replicated_Log::Replicated_Log(unsigned int memory_size, unsigned int entry_size, unsigned int total_clients, unsigned int client_id, unsigned int bits_per_client_position) {
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


        //This part does not really matter but it makes my life easy
        assert((1 << bits_per_client_position) < _number_of_entries);
        this->_bits_per_client_position = bits_per_client_position;
        this->_client_id = client_id;
        this->_total_clients = total_clients;
        allocate_client_positions(total_clients, bits_per_client_position);
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

    bool Replicated_Log::Can_Append(int num_entries) {
        int min_entry = get_min_client_position();
        int current_entry = get_entry(this->_tail_pointer);
        VERBOSE("Can Append", "min entry %d, current entry %d", min_entry, current_entry);

        for (int i=0;i<num_entries;i++) {
            if ((current_entry + i + 1) %get_number_of_entries() == min_entry) {
                return false;
            }
        }


        return true;

    }

    bool Replicated_Log::Will_Fit_In_Entry(size_t size) {
        if (size + sizeof(Entry_Metadata) > this->_entry_size) {
            ALERT("WARNING", "Entry size is too big for the log. Log entry size is %d and the entry size is %d", this->_entry_size, size + sizeof(Entry_Metadata));
            return false;
        }
        return true;
    }


    void Replicated_Log::Append_Log_Entry(void * data, size_t size) {
        INFO("Append Entry", "[id %d] [%5d] epoch[%d]: size: %d and value %d",this->_client_id, get_entry(this->_tail_pointer), get_epoch(this->_tail_pointer), size, *(int *)data);
        assert(sizeof(Entry_Metadata) + size <= this->_entry_size); // we must be able to fit the entry in the log
        void* old_tail_pointer = get_reference_to_tail_pointer_entry();
        Entry_Metadata em;
        em.type = app;
        em.epoch = get_epoch(this->_tail_pointer) % 2;

        // if (!Can_Append()){
        //     ALERT("Append Entry", "Cannot append entry, min client position is %d", get_min_client_position());
        //     ALERT("Append Entry", "This is where we wil");
        // }

        this->_tail_pointer++;
        //Dont update the client position here. We have only written, not consumed.
        //This tail pointer is the remote tail pointer we want to sync the local one.
        // update_client_position(this->_tail_pointer);
        // print_client_positions();

        bzero((void*) old_tail_pointer, this->_entry_size);
        memcpy((void*) old_tail_pointer, (void*) &em, sizeof(Entry_Metadata));
        memcpy((void*) (old_tail_pointer + sizeof(Entry_Metadata)), data, size);

        //These two lines can replace the 3 above for a bit more speed but it's not really required.
        // *((Entry_Metadata*) old_tail_pointer) = *((Entry_Metadata*) &em);
        // memcpy((void*) (old_tail_pointer + sizeof(Entry_Metadata)), data, size);

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
            buf[this->_entry_size] = '\0';
            int value = *(int*) (buf + sizeof(Entry_Metadata));
            ALERT("REPLICATED_LOG (BROKEN)", "entry type %d, epoch %d, value %s", em->type, em->epoch, value,buf);
            //Copy repeating values to buffer and print as a string
            // buf[this->_entry_size] = '\0';
            // ALERT("REPLICATED_LOG", "[%s]", buf);
            current_pointer += this->_entry_size;

            if (current_pointer > (uint64_t)this->_log + this->_entry_size * 20) {
                ALERT("REPLICATED_LOG", "HARD LIMIT ON PRINT ALL ENTRIES BECAUSE WE SHOULD NOT BE DEBUUING 20+ entries");
                break;
            }
        }
        delete[] buf;
    }

    void Replicated_Log::Reset_Tail_Pointer() {
        this->_tail_pointer = 0;
    }

    void Replicated_Log::Chase_Tail_Pointer() {
        VERBOSE("Chasing Tail Pointer", "Start Index %d", this->_tail_pointer);
        Chase(&this->_tail_pointer);
    }

    void Replicated_Log::Chase_Locally_Synced_Tail_Pointer() {
        VERBOSE("Chasing Locally Synced Tail Pointer", "Start Index %d", this->_locally_synced_tail_pointer);
        Chase(&this->_locally_synced_tail_pointer);
    }

    void Replicated_Log::Chase(uint64_t * tail_pointer) {
        Entry_Metadata * em = (Entry_Metadata*) ((uint64_t) this->_log + (get_entry(*tail_pointer) * this->_entry_size));
        //Print the epoch of the entry metadata and the epoch of the tail pointer
        // INFO("Chase", "[%d] epoch %d em-epoch[%d]", get_entry(*tail_pointer), get_epoch(*tail_pointer), em->epoch);
        //Print the entry we are chasing
        while(em->is_vaild_entry(get_epoch(*tail_pointer))) {
            // INFO("Chase", "[%d] epoch %d", get_entry(*tail_pointer), get_epoch(*tail_pointer));
            (*tail_pointer)++;
            // em = (Entry_Metadata *) ((uint64_t) em) + this->_entry_size;
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
        void * ret = Peek_Next(tail_pointer);
        if (ret != NULL) {
            (*tail_pointer)++;
        }
        return ret;
    }

    void * Replicated_Log::Peek_Next(uint64_t *tail_pointer){
        Entry_Metadata * em = (Entry_Metadata*) ((uint64_t) this->_log + (*tail_pointer * this->_entry_size));
        void * ret = NULL;
        if (em->is_vaild_entry(get_epoch(*tail_pointer))) {
            ret = (void*) ((uint64_t) this->_log + (*tail_pointer * this->_entry_size));
        }
        return ret;
    }

    void * Replicated_Log::Peek_Next_Operation(){
        VERBOSE("Peek Next Operation", "Peeking at next operation %lu", this->_operation_tail_pointer);
        return Peek_Next(&this->_operation_tail_pointer);
    }
}