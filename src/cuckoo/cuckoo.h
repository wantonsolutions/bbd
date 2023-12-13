#ifndef CUCKOO_H
#define CUCKOO_H

#include <unordered_map>
#include <infiniband/verbs.h>
#include <atomic>

#include "tables.h"
#include "search.h"
#include "../slib/config.h"
#include "../slib/state_machines.h"
#include "../rdma/rdma_common.h"


using namespace state_machines;
using namespace cuckoo_search;

#define MAX_CONCURRENT_CUCKOO_MESSAGES 32
#define ID_SIZE 64

#define MAX_INSERT_CRC_AND_UNLOCK_MESSAGE_COUNT 16
#define FAULT_CASE_0 0
#define FAULT_CASE_1 1
#define FAULT_CASE_2 2
#define FAULT_CASE_3 3
#define FAULT_CASE_4 4

namespace cuckoo_rcuckoo {



    class RCuckoo : public Client_State_Machine {
        public:
            RCuckoo();
            RCuckoo(unordered_map<string, string> config);
            ~RCuckoo() {printf("Killing RCuckoo!!!n");}

            const char * log_id();

            void set_search_function(unordered_map<string, string> config);
            void set_location_function(unordered_map<string, string> config);


            void clear_statistics();
            string get_state_machine_name();

            void receive_successful_unlocking_message(unsigned int message_index);
            void receive_successful_locking_message(unsigned int message_index);

            void complete_insert_stats(bool success);
            void complete_insert();

            //Table and lock table functions
            Entry ** get_table_pointer();
            unsigned int get_table_size_bytes();
            void print_table();
            Entry * get_entry_pointer(unsigned int bucket_id, unsigned int offset);
            void * get_lock_table_pointer();
            unsigned int get_lock_table_size_bytes();
            void * get_lock_pointer(unsigned int lock_index);

            void fill_current_unlock_list();

            bool path_valid();
            bool insert_cuckoo_path_local(Table &table, vector<path_element> &path);

            /* RDMA specific functions */
            uint64_t local_to_remote_table_address(uint64_t local_address);
            void send_virtual_read_message(VRReadData message, uint64_t wr_id);
            void send_virtual_cas_message(VRCasData message, uint64_t wr_id);
            void send_virtual_masked_cas_message(VRMaskedCasData message, uint64_t wr_id);

            void set_unlock_message(VRMaskedCasData &unlock_message, struct ibv_sge *sg, struct ibv_exp_send_wr *wr, uint64_t *wr_id);
            void set_insert(VRCasData &insert_message, struct ibv_sge *sg, struct ibv_exp_send_wr *wr, uint64_t *wr_id);
            void send_lock_and_cover_message(VRMaskedCasData lock_message, VRReadData read_message);
            void send_insert_and_unlock_messages(vector<VRCasData> &insert_messages, vector<VRMaskedCasData> & unlock_messages, uint64_t wr_id);

            void send_insert_and_crc(VRCasData insert_message, ibv_sge *sg, ibv_exp_send_wr *wr, uint64_t *wr_id);
            void send_insert_crc_and_unlock_messages(vector<VRCasData> &insert_messages, vector<VRMaskedCasData> & unlock_messages);
            void send_insert_crc_and_unlock_messages_with_fault(vector<VRCasData> &insert_messages, vector<VRMaskedCasData> & unlock_messages, unsigned int fault);
            void send_read(vector <VRReadData> reads);

            void rdma_fsm(void);
            void init_rdma_structures(rdma_info info);
            bool top_level_aquire_locks();
            void put_direct();
            void get_direct(void);
            void insert_direct();
            void update_direct(void);

            void set_hash_factor(unordered_map<string, string> config);


            void complete_read_stats(bool success);
            void complete_read(bool success);
            void complete_update(bool success);
            void complete_update_stats(bool success);


            int aquire_repair_lease(unsigned int lock);
            int release_repair_lease(unsigned int lease_id);

            void repair_table(Entry broken_entry, unsigned int error_state);
            void reclaim_lock(unsigned int lock);
            int get_broken_row(Entry broken_entry);



            vector<VRMaskedCasData> get_current_unlock_list();


        private:

            char _log_identifier[ID_SIZE];
            unsigned int _read_threshold_bytes;
            unsigned int _buckets_per_lock;
            unsigned int _locks_per_message;

            bool _local_prime_flag;
            bool _use_mask;
            float _hash_factor;

            uint64_t _sleep_counter = 1;



            Table _table;
            // Key _current_insert_key;
            int _search_path_index;
            vector<unsigned int> _locks_held;


            int _locking_message_index;

            /*rdma specific variables*/
            ibv_qp * _qp;
            ibv_mr *_table_mr;
            ibv_mr *_lock_table_mr;
            ibv_pd *_protection_domain;
            struct ibv_cq * _completion_queue;
            table_config * _table_config;
            struct ibv_wc *_wc;
            uint64_t _wr_id;

            //Cached structures to prevent reinitalizations
            vector<vector<unsigned int>> _fast_lock_chunks;
            vector<unsigned int> _buckets;

            vector<VRCasData> _insert_messages;
            vector<VRReadData> _covering_reads;


            LockingContext _locking_context;
            search_context _search_context;

            vector<VRReadData> _reads;


            // hash_locations  (*_location_function)(string, unsigned int);
            hash_locations  (*_location_function)(Key&, unsigned int);

            bool (RCuckoo::*_table_search_function)();
            bool a_star_insert_self();
            bool random_insert_self();
            bool bfs_insert_self();


            bool read_complete();
            bool all_locks_aquired();
            bool all_locks_released();
    };
}

#endif
