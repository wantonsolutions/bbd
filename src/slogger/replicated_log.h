#ifndef REPLICATED_LOG_H
#define REPLICATED_LOG_H

#include <stdint.h>
#include "../slib/log.h"
#include <string>
#include <bits/stdc++.h>

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
            Replicated_Log(unsigned int memory_size, unsigned int entry_size, unsigned int total_clients, unsigned int client_id);
            // ~Replicated_Log() {ALERT("REPLICATED_LOG", "deleting replicated log");}
            void Append_Log_Entry(void * data, size_t size);
            bool Can_Append();
            void Print_All_Entries();
            bool Will_Fit_In_Entry(size_t size);

            void Reset_Tail_Pointer();
            void Chase_Tail_Pointer();
            void Chase_Locally_Synced_Tail_Pointer();
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
            // unsigned int get_epoch() {return this->_epoch;}
            unsigned int get_epoch(uint64_t tail_pointer) {return (tail_pointer / this->_number_of_entries) + 1;}
            unsigned int get_entry(uint64_t tail_pointer) {return tail_pointer % this->_number_of_entries;}

            unsigned int epoch() {return get_epoch(this->_tail_pointer);}
            unsigned int entry() {return get_entry(this->_tail_pointer);}
            // void set_epoch(unsigned int epoch) {this->_epoch = epoch;}


            int client_position_byte() {return this->_client_id * this->_bits_per_client_position / 8;}
            int client_position_bit() {return this->_client_id * this->_bits_per_client_position % 8;}

            unsigned int read_client_position_epoch(int client_id) {
                int byte = client_position_byte();
                int bit = client_position_bit();
                return (_client_positions[byte] & (1 << bit)) >> bit;
            }

            void set_client_position_epoch(int client_id, unsigned int epoch) {
                int byte = client_position_byte();
                int bit = client_position_bit();
                if (epoch == 0) {
                    _client_positions[byte] &= ~(1 << bit);
                } else {
                    _client_positions[byte] |= (1 << bit);
                }
            }

            unsigned int tail_pointer_to_client_position(uint64_t tail_pointer) {
                unsigned int position = get_entry(tail_pointer);
                unsigned int bits_in_entries = log2(get_number_of_entries());
                unsigned int right_shift = ((bits_in_entries - this->_bits_per_client_position));
                position = position >> right_shift;
                return position;
            }

            void set_client_position(int client_id, uint64_t tail_pointer) {
                unsigned int position = tail_pointer_to_client_position(tail_pointer);
                int byte = client_position_byte();
                int bit = client_position_bit();
                bit = bit + 1;

                for (int i =0; i < this->_bits_per_client_position; i++) {
                    if (bit == 8) {
                        bit = 0;
                        byte++;
                    }
                    _client_positions[byte] &= ~(1 << bit);
                    _client_positions[byte] |= ((position & (1 << i)) >> i) << bit;
                    bit++;
                }
            }

            uint64_t get_client_position(int client_id) {
                unsigned int position = 0;
                int byte = client_position_byte();
                int bit = client_position_bit();
                bit = bit + 1;
                // printf("bits per client position %d\n", this->_bits_per_client_position);
                for (int j =0; j < this->_bits_per_client_position; j++) {
                    // printf("bit: %d\n", bit);
                    if (bit == 8) {
                        bit = 0;
                        byte++;
                    }
                    position |= ((_client_positions[byte] & (1 << bit)) >> bit) << j;
                    bit++;
                }
                // printf("position: %d\n", position);
                return position;
            }

        private:
            void * Next(uint64_t *tail_pointer);
            void Chase(uint64_t * tail_pointer);
            void allocate_client_positions(unsigned int total_clients, unsigned int bits_per_entry);
            void update_client_position(uint64_t tail_pointer);


            void print_client_positions();
            unsigned int _memory_size;
            unsigned int _entry_size;
            unsigned int _number_of_entries;
            unsigned int _total_clients;
            unsigned int _client_id;
            unsigned int _bits_per_client_position;
            // unsigned int _epoch;
            uint8_t* _log;
            //Tail pointer references the remote tail pointer. This value is DMA's to and from directly
            uint64_t _tail_pointer;
            //Local tail pointer is used for tracking complete local updates. This defines the maximum local entries.
            uint64_t _locally_synced_tail_pointer;
            //Operation tail pointer is used by the application to pop off operations from the log seperate from how the log is managed
            uint64_t _operation_tail_pointer;

            uint8_t* _client_positions;
    };

}

#endif