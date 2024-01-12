#pragma once
#ifndef STATE_MACHINES_H
#define STATE_MACHINES_H

#include "../cuckoo/tables.h"
#include "../cuckoo/virtual_rdma.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <any>
#include <chrono>

using namespace std;
using namespace cuckoo_virtual_rdma;


#define RDMA_READ_REQUSET_SIZE 74
#define RDMA_MASKED_CAS_REQUEST_SIZE 102
#define RDMA_CAS_REQUEST_SIZE 86
#define RDMA_FAA_REQUEST_SIZE 78
#define RDMA_WRITE_REQUEST_BASE_SIZE 82

#define RDMA_READ_RESPONSE_BASE_SIZE 62
#define RDMA_ATOMIC_RESPONSE_SIZE 70
#define RDMA_WRITE_RESPONSE_SIZE 54
#define RDMA_MASKED_CAS_RESPONSE_SIZE RDMA_ATOMIC_RESPONSE_SIZE
#define RDMA_CAS_RESPONSE_SIZE RDMA_ATOMIC_RESPONSE_SIZE
#define RDMA_FAA_RESPONSE_SIZE RDMA_ATOMIC_RESPONSE_SIZE

//Measurement DEFS
// #define MEASURE_ALL
// #define MEASURE_MOST
#define MEASURE_ESSENTIAL
// #define MEASURE_NONE

#ifdef MEASURE_ALL
    #define MEASURE_MOST
#endif
#ifdef MEASURE_MOST
    #define MEASURE_ESSENTIAL
#endif
#ifdef MEASURE_ESSENTIAL
    #define MEASURE_NONE
#endif


namespace state_machines {


    class State_Machine {
        public:
            State_Machine();
            State_Machine(unordered_map<string, string> config);
            virtual ~State_Machine() = default;
            void clear_statistics();

            string get_state_machine_name();
            bool is_complete();
            vector<Key> get_completed_inserts();
            void set_max_fill(float max_fill);

            void complete_read_stats(bool success, Key read_key);
            void complete_insert_stats(bool success);
            void complete_update_stats(bool success);
            unordered_map<string, string> get_stats();


            void set_global_start_flag(volatile bool * flag);
            void set_global_end_flag(volatile bool * flag);
            void set_global_prime_flag(volatile bool * flag);


        protected:
            unordered_map<string, string> _config;
            bool _complete;
            bool _inserting;
            bool _reading;
            bool _updating;

            bool _local_prime_flag;

            //State Machine Statistics
            uint64_t _total_bytes;
            uint64_t _read_bytes;
            uint64_t _write_bytes;
            uint64_t _cas_bytes;
            uint64_t _faa_bytes;
            uint64_t _masked_cas_bytes;

            uint32_t _total_reads;
            uint32_t _total_writes;
            uint32_t _total_cas;
            uint32_t _total_cas_failures;
            uint32_t _total_faa;
            uint32_t _total_masked_cas = 0;
            uint32_t _total_masked_cas_failures=0;

            Key _current_insert_key;
            Key _current_update_key;

            vector<int> _insert_path_lengths;
            vector<int> _index_range_per_insert;
            uint32_t _current_insert_messages;
            vector<int> _messages_per_insert;
            vector<Key> _completed_inserts;

            vector<int> _failed_inserts_second_search;
            uint32_t _failed_insert_second_search_count;
            uint32_t _failed_insert_second_search_this_insert;
            vector<int> _failed_lock_aquisitions;
            uint32_t _failed_lock_aquisition_count;
            uint32_t _failed_lock_aquisition_this_insert;

            uint32_t _failed_insert_first_search_this_insert;
            uint32_t _failed_insert_first_search_count;
            vector<int> _failed_inserts_first_search;


            uint32_t _completed_insert_count;
            uint64_t _insert_operation_bytes;
            uint64_t _insert_operation_messages;
            //track rtt
            uint32_t _current_insert_rtt;
            vector<int> _insert_rtt;
            uint64_t _insert_rtt_count;


            vector<Key>_completed_updates;
            vector<int> _messages_per_update;
            vector<int> _update_rtt;
            vector<int> _update_latency_ns;
            uint32_t _completed_update_count;
            uint32_t _current_update_messages;
            uint32_t _current_update_rtt;
            uint64_t _sum_update_latency_ns;

            //read stats
            uint32_t _current_read_messages;
            vector<int> _messages_per_read;
            vector<Key> _completed_reads;
            uint64_t _completed_read_count;
            vector<Key> _failed_reads;
            uint64_t _failed_read_count;
            uint64_t _read_operation_bytes;
            uint64_t _read_operation_messages;
            uint32_t _current_read_rtt;
            vector<int> _read_rtt;
            uint64_t _read_rtt_count;

            chrono::nanoseconds _operation_start_time;
            chrono::nanoseconds _operation_end_time;
            uint64_t _sum_insert_latency_ns;
            uint64_t _sum_read_latency_ns;
            vector<int> _insert_latency_ns;
            vector<int> _read_latency_ns;

            uint32_t _faults_injected;

            //////////////////////////////
            /*Control Variables*/
            volatile bool * _global_start_flag;
            volatile bool * _global_end_flag;
            volatile bool * _global_prime_flag;

        private:
    };

    enum ycsb_workload {
        A,
        B,
        C,
        W,
    };

    const char* get_ycsb_workload_name(ycsb_workload workload);

    class Client_Workload_Driver {
        public:
            Client_Workload_Driver();
            Client_Workload_Driver(unordered_map<string, string> config);
            ~Client_Workload_Driver() {}
            unordered_map<string,string> get_stats();
            void set_workload(ycsb_workload workload);
            void set_workload(string workload);
            Request next();

        private:
            int _total_requests;
            int _client_id;
            int _starting_id;
            int _global_clients;
            int _num_clients;
            bool _deterministic;
            int _random_factor;
            int _completed_requests;
            int _completed_puts;
            int _completed_gets;
            int _completed_updates;
            unsigned int _time_seed;
            ycsb_workload _workload;
            Request _last_request;

            void record_last_request();
            Key unique_insert(int insert_index, int client_id, int total_clients, int factor);
            Key unique_get(int get_index, int client_id, int total_clients, int factor);
            Key unique_update(int get_index, int client_id, int total_clients, int factor);
            Request next_put();
            Request next_get();
            Request next_update();
            operation gen_next_operation();

    };


    enum client_state {
        IDLE,
        READING,
        AQUIRE_LOCKS,
        RELEASE_LOCKS_TRY_AGAIN,
        INSERTING,
    };

    const char* get_client_state_name(client_state state);

    typedef struct read_status {
        bool complete;
        bool success;
    } read_status;

    class Client_State_Machine : public State_Machine {
        public:
            Client_State_Machine();
            Client_State_Machine(unordered_map<string, string> config);
            void set_max_fill(int max_fill);
            ~Client_State_Machine() {}
            void clear_statistics();
            void set_workload(ycsb_workload workload);
            bool read_complete();
            bool read_successful(Key key);
            string get_state_machine_name();
            unordered_map<string, string> get_stats();

        protected:
            uint32_t _total_inserts;
            uint32_t _id;
            client_state _state;
            uint32_t _max_fill;
            int _starting_id;
            int _global_clients; 

            //Read State Machine
            Key _current_read_key;
            // Key _current_insert_key;
            uint32_t _outstanding_read_requests;
            uint32_t _read_values_found;
            string _workload;
            vector<Key> _read_values;
            uint32_t _duplicates_found;
            Client_Workload_Driver _workload_driver;
    };


    class TableFullException : public std::exception {
    public:
    char * what () {
            char * message = new char[100];
            sprintf(message, "Table is full to the max fill rate -- :) this is the only good exception");
            return message;
        }
    };

    class Memory_State_Machine : public State_Machine {
        public:
            Memory_State_Machine();
            Memory_State_Machine(unordered_map<string, string> config);
            void set_max_fill(int max_fill);
            bool contains_duplicates();
            vector<Duplicate_Entry> get_duplicates();
            bool contains(Key key);
            float get_fill_percentage();
            float get_max_fill();

            void set_prime_fill(int prime_fill);
            int get_prime_fill();
            void print_table();
            void print_lock_table();
            int crc_table();



            unsigned int get_table_size();
            void set_table_pointer(Entry ** table);
            Entry ** get_table_pointer();
            Table * get_table();


            void * get_underlying_lock_table_address();
            unsigned int get_underlying_lock_table_size_bytes();
            void set_underlying_lock_table_address(void * address);

            void * get_underlying_repair_lease_table_address() { return _table.get_underlying_repair_lease_table_address();};
            unsigned int get_underlying_repair_lease_table_size_bytes() { return _table.get_underlying_repair_lease_table_size_bytes();};

        private:
            Table _table;
            uint32_t _max_fill;
            uint32_t _prime_fill;
    };
}

#endif