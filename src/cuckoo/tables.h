#pragma once
#ifndef TABLES_H
#define TABLES_H

#include <stdint.h>
#include <string>
#include <vector>
#include <assert.h>
#include "../slib/util.h"

using namespace std;

#define ROW_CRC
#define TOTAL_REPAIR_LEASES 1

namespace cuckoo_tables {

    #ifndef KEY_SIZE
    #define KEY_SIZE 4
    #endif

    #ifndef VALUE_SIZE
    #define VALUE_SIZE 4
    #endif


    typedef struct Key { 
        uint8_t bytes[KEY_SIZE];
        string to_string();
        uint64_t to_uint64_t();
        bool is_empty();
        bool operator==(const Key& rhs) const {
            if(KEY_SIZE == 4){
                uint32_t lhs_int = *(uint32_t*)bytes;
                uint32_t rhs_int = *(uint32_t*)rhs.bytes;
                return lhs_int == rhs_int;
            } else {

                for (int i = KEY_SIZE; i >= 0; i++){
                    if (bytes[i] != rhs.bytes[i]){
                        return false;
                    }
                }
                return true;
            }
        }

        template <typename T>
        void set(T val) {
            for (long unsigned int i = 0; i < KEY_SIZE && i < sizeof(val); i++){
                bytes[i] = (val >> (8 * i)) & 0xFF;
            }
        }

        Key(string key) {
            for (long unsigned int i = 0; i < KEY_SIZE*2 && i < key.size(); i+=2){
                std::string byteString = key.substr(i, 2);
                uint8_t byte = (uint8_t) strtol(byteString.c_str(), NULL, 16);
                bytes[i/2] = byte;
            }
        }
        Key(){
            for (auto i = 0; i < KEY_SIZE; i++){
                bytes[i] = 0;
            }
        }
    } Key;

    typedef struct Value { 
        uint8_t bytes[VALUE_SIZE];
        string to_string();
        bool is_empty();
        bool operator==(const Value& rhs) const {
            for (int i = 0; i < VALUE_SIZE; i++){
                if (bytes[i] != rhs.bytes[i]){
                    return false;
                }
            }
            return true;
        }
        // bool operator=(const Value& rhs) {
        //     for (int i = 0; i < VALUE_SIZE; i++){
        //         bytes[i] = rhs.bytes[i];
        //     }
        //     return true;
        // }
        template <typename T>
        void set(T val) {
            for (long unsigned int i = 0; i < VALUE_SIZE && i < sizeof(val); i++){
                bytes[i] = (val >> (8 * i)) & 0xFF;
            }
        }
        Value(string value){
            for (long unsigned int i = 0; i < VALUE_SIZE && i < value.size(); i++){
                bytes[i] = value[i] - '0';
            }
        }
        Value(){
            for (int i = 0; i < VALUE_SIZE; i++){
                bytes[i] = 0;
            }
        }
    } Value;

    typedef struct Entry {
        //todo add some entry functions
        Key key;
        Value value;
        string to_string();
        bool is_empty();
        Entry() {
            this->key = Key();
            this->value = Value();
        }
        Entry(string str_key, string str_value) {
            this->key = Key(str_key);
            this->value = Value(str_value);
        }
        Entry(Key key, Value value) {
            this->key = key;
            this->value = value;
        }

        bool operator==(const Entry& rhs) const {
            return this->key == rhs.key && this->value == rhs.value;
        }
        bool operator!=(const Entry& rhs) const {
            return !(this->key == rhs.key && this->value == rhs.value);
        }

        int copy(Entry &e) {
            for (int i=0; i < KEY_SIZE; i++){
                this->key.bytes[i] = e.key.bytes[i];
            }
            for(int i=0; i < VALUE_SIZE;i++){
                this->value.bytes[i] = e.value.bytes[i];
            }
            return VALUE_SIZE + KEY_SIZE;
        }

        bool equals(Entry &e) {
            for (int i=0; i < KEY_SIZE; i++){
                if(key.bytes[i] != e.key.bytes[i]) {
                    return false;
                }
            }
            for(int i=0; i < VALUE_SIZE;i++){
                if(value.bytes[i] != e.value.bytes[i]) {
                    return false;
                }
            }
            return true;
        }

        uint64_t get_as_uint64_t() {
            assert(sizeof(Entry) == 8);
            uint64_t entry64 = 0;
            int i=0;
            for (; i < KEY_SIZE; i++){
                entry64 |= (uint64_t) this->key.bytes[i] << (8 * i);
            }
            for(; i < VALUE_SIZE;i++){
                entry64 |= (uint64_t) this->value.bytes[i - KEY_SIZE] << (8 * i);
            }
            return entry64;
        }
        void set_as_uint64_t(uint64_t entry64) {
            assert(sizeof(Entry) == 8);
            int i=0;
            for (int i=0 ; i < KEY_SIZE; i++){
                this->key.bytes[i] = (entry64 >> (8 * i)) & 0xFF;
            }
            for(; i < VALUE_SIZE;i++){
                this->value.bytes[i - KEY_SIZE] = (entry64 >> (8 * i)) & 0xFF;
            }
        }
        void zero_out() {
            key.set(0);
            value.set(0);
        }
    } Entry;

    typedef struct Duplicate_Entry {
        Entry first_entry;
        int first_entry_row;
        int first_entry_column;
        Entry second_entry;
        int second_entry_row;
        int second_entry_column;
    } Duplicate_Entry;


    typedef struct Repair_Lease {
        uint8_t lock;
        uint8_t meta;
        uint16_t id;
        uint32_t counter; //Max failures during recovery are 1 second
        bool Lock(void) {
            if (lock == 0) {
                lock=1;
                return true;
            }
            return false;
        }
        bool Unlock(void) {
            if(lock == 1) {
                lock=0;
                return true;
            }
            return false;
        }
        bool islocked(void) {
            return lock == 1;
        }

        bool isunlocked(void) {
            return !islocked();
        }
        string to_string();
    } Repair_Lease;

    class Repair_Lease_Table {

        public:
            Repair_Lease_Table();
            Repair_Lease_Table(unsigned int leases);
            void * get_lease_table_address();
            unsigned int get_lease_table_size_bytes();
            string to_string();
            void * get_repair_lease_pointer(unsigned int repair_lease_index);
            unsigned int get_total_leases() { return _total_leases; };

        private:
            unsigned int _total_leases;
            Repair_Lease *_leases;
    };

    class Lock_Table {
        public:
            Lock_Table();
            Lock_Table(unsigned int memory_size, unsigned int bucket_size, unsigned int buckets_per_lock);
            // ~Lock_Table();
            void unlock_all();
            void * get_lock_table_address();
            unsigned int get_total_locks();
            unsigned int get_lock_table_size_bytes();
            void set_lock_table_address(void * address);
            void * get_lock_pointer(unsigned int lock_index);
            string to_string();

        private:
            unsigned int _total_locks;
            unsigned int _total_lock_entries;
            uint8_t *_locks;
    };
    
    class Table {
        public:
            Table();
            Table(unsigned int memory_size, unsigned int bucket_size, unsigned int buckets_per_lock);


            bool operator==(const Table &rhs) const;
            // ~Table();
            void unlock_all();
            string to_string();
            string row_to_string(unsigned int row);
            void print_table();
            void print_row(unsigned int row);
            void print_lock_table();
            Entry ** get_underlying_table();
            void set_underlying_table(Entry ** table);
            unsigned int get_table_size_bytes() const;
            unsigned int get_buckets_per_row() const;
            unsigned int get_row_count() const;
            unsigned int get_bucket_size();
            unsigned int row_size_bytes();
            unsigned int get_entry_size_bytes();
            unsigned int n_buckets_size(unsigned int n_buckets);
            Entry get_entry(unsigned int bucket_index, unsigned int offset) const;
            void set_entry_with_crc(unsigned int bucket_index, unsigned int offset, Entry &entry);
            void set_entry(unsigned int bucket_index, unsigned int offset, Entry entry);
            Entry * get_entry_pointer(unsigned int bucket_index, unsigned int offset);
            bool bucket_has_empty(unsigned int bucket_index);

            bool bucket_is_empty(unsigned int bucket_index);
            unsigned int get_first_empty_index(unsigned int bucket_index);

            uint64_t crc64_row(unsigned int row_index);
            bool crc_valid_row(unsigned int row);
            int crc_valid();

            bool contains(Key key);
            bool bucket_contains(unsigned int bucket_index, Key &key);
            int get_keys_offset_in_row(unsigned int row, Key &key);

            bool get_index_and_offset(Key &key, unsigned int row1, unsigned int row2, unsigned int *index, unsigned int *offset);
            bool key_is_at(unsigned int bucket_index, unsigned int offset, Key &key);

            float get_fill_percentage_fast();
            float get_fill_percentage();
            bool full();

            unsigned int get_entries_per_row() const;
            Entry ** generate_bucket_cuckoo_hash_index(unsigned int memory_size, unsigned int bucket_size);
            unsigned int absolute_index_to_bucket_index(unsigned int absolute_index);
            unsigned int absolute_index_to_bucket_offset(unsigned int absolute_index);
            void assert_operation_in_table_bound(unsigned int bucket_index, unsigned int offset, unsigned int read_size);
            bool contains_duplicates();
            vector<Duplicate_Entry> get_duplicates();


            unsigned int get_total_locks();
            void * get_underlying_lock_table_address();
            unsigned int get_underlying_lock_table_size_bytes();
            void set_underlying_lock_table_address(void * address);
            void * get_lock_pointer(unsigned int lock_index);

            void * get_underlying_repair_lease_table_address();
            unsigned int get_underlying_repair_lease_table_size_bytes();

            void * get_repair_lease_pointer(unsigned int repair_lease_index);


        private:
            unsigned int _memory_size;
            unsigned int _bucket_size;
            unsigned int _table_size;
            unsigned int _buckets_per_lock;
            unsigned int _entries_per_row;
            Entry **_table;
            Lock_Table _lock_table;
            Repair_Lease_Table _repair_lease_table;
            unsigned int _fill;
    };
}

namespace std {

    using namespace cuckoo_tables;
    template <>
    struct hash<Key>
    {
        std::size_t operator()(const Key& k) const
        {
        using std::size_t;
        using std::hash;
        using std::string;

        // Compute individual hash values for first,
        // second and third and combine them using XOR
        // and bit shifting:

        std::size_t s = hash<string>()("salt");
        for (int i = 0; i < KEY_SIZE; i++){
            s ^= (hash<int>()(k.bytes[i]) << i);
        }
        return s;
        }
        };
}

#endif