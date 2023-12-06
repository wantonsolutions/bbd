// #include "state_machines.h"
#include <string>
#include <unordered_map>
#include <algorithm>
#include <iterator>
#include <vector>
#include <set>
#include "cuckoo.h"
#include "tables.h"
#include "search.h"
#include "hash.h"
#include "../slib/log.h"
#include "../slib/memcached.h"
#include "../slib/util.h"
#include "../rdma/rdma_helper.h"
#include "../rdma/rdma_common.h"
#include <cassert>
#include <chrono>
#include <inttypes.h>

#define DEBUG

using namespace std;
using namespace cuckoo_search;
using namespace state_machines;
using namespace rdma_helper;


#define MAX_INSERT_AND_UNLOCK_MESSAGE_COUNT 64

#define INJECT_FAULT FAULT_CASE_0


namespace cuckoo_rcuckoo {

    Entry ** RCuckoo::get_table_pointer() {
        return _table.get_underlying_table();
    }

    const char * RCuckoo::log_id() {
        return _log_identifier;
    }

    void * RCuckoo::get_lock_table_pointer(){
        return _table.get_underlying_lock_table_address();
    }

    void * RCuckoo::get_lock_pointer(unsigned int lock_index){
        return _table.get_lock_pointer(lock_index);
    }

    unsigned int RCuckoo::get_lock_table_size_bytes(){
        return _table.get_underlying_lock_table_size_bytes();
    }

    void RCuckoo::print_table() {
        _table.print_table();
    }

    bool RCuckoo::path_valid() {
        //Check that all the entries in the path are the same as in the table
        for (size_t i=0;i<_search_context.path.size()-1;i++) {
            Entry current_table_entry = _table.get_entry(_search_context.path[i].bucket_index, _search_context.path[i].offset);
            if (!(current_table_entry.key == _search_context.path[i].key)) {
                return false;
            }
        }
        return true;
    }

    void RCuckoo::insert_cuckoo_path_local(Table &table, vector<path_element> &path) {
        assert(path.size() >= 2);
        for (int i=path.size()-2; i >=0; i--){
            Entry e;
            e.key = path[i+1].key;
            table.set_entry(path[i].bucket_index, path[i].offset, e);
        }
    }

    unsigned int RCuckoo::get_table_size_bytes() {
        return _table.get_table_size_bytes();
    }

    Entry * RCuckoo::get_entry_pointer(unsigned int bucket_id, unsigned int offset){
        return _table.get_entry_pointer(bucket_id, offset);
    }

    RCuckoo::RCuckoo() : Client_State_Machine() {}

    RCuckoo::RCuckoo(unordered_map<string, string> config) : Client_State_Machine(config) {

        //Try to init the table
        try {
            unsigned int memory_size = stoi(config["memory_size"]);
            unsigned int bucket_size = stoi(config["bucket_size"]);
            unsigned int buckets_per_lock = stoi(config["buckets_per_lock"]);
            INFO(log_id(), "Creating Table : Table_size: %d, bucket_size %d, buckets_per_lock %d\n", memory_size, bucket_size, buckets_per_lock);
            _table = Table(memory_size, bucket_size, buckets_per_lock);
        } catch (exception& e) {
            ALERT(log_id(), "ERROR: Memory_State_Machine config missing required field\n");
            throw logic_error("ERROR: Memory_State_Machine config missing required field");
        }

        clear_statistics();
        //try to parse state machine config
        bool use_virtual_lock_table;
        unsigned virtual_lock_scale_factor;
        try{
            _read_threshold_bytes = stoi(config["read_threshold_bytes"]);
            _buckets_per_lock = stoi(config["buckets_per_lock"]);
            _locks_per_message = stoi(config["locks_per_message"]);
            // assert(_locks_per_message == 64);
            _starting_id = stoi(config["starting_id"]);
            _global_clients = stoi(config["global_clients"]);
            _id = stoi(config["id"]) + _starting_id;
            _use_mask = (config["use_mask"] == "true");
            use_virtual_lock_table = (config["use_virtual_lock_table"] == "true");
            virtual_lock_scale_factor = stoi(config["virtual_lock_scale_factor"]);

        } catch (exception& e) {
            printf("ERROR: RCuckoo config missing required field\n");
            throw logic_error("ERROR: RCuckoo config missing required field");
        }
        assert(_read_threshold_bytes > 0);
        assert(_read_threshold_bytes >= _table.row_size_bytes());

        set_search_function(config);
        set_location_function(config);

        _search_context.table = &_table;
        _search_context.location_func = _location_function;

        const int max_in_flight_locks = 32;
        for (int i=0;i<max_in_flight_locks;i++) {
            _locking_context.fast_lock_chunks.push_back(vector<unsigned int>());
        }
        _locking_context.buckets_per_lock = _buckets_per_lock;
        _locking_context.locks_per_message = _locks_per_message;
        _locking_context.virtual_lock_table = use_virtual_lock_table;

        if (_locking_context.virtual_lock_table) {
            _locking_context.scale_factor = virtual_lock_scale_factor;
            _locking_context.total_physical_locks = _table.get_total_locks() / _locking_context.scale_factor;
            for (int i=0;i<50;i++){
                ALERT("virutal lock table", "we are using an artifically sized virtual lock table\n");
            }
        } else {
            _locking_context.scale_factor = 1;
            _locking_context.total_physical_locks = _table.get_total_locks();
        }
        sprintf(_log_identifier, "Client: %3d", _id);

        _local_prime_flag = false; //we have yet to prime

        set_hash_factor(config);
    }

    string RCuckoo::get_state_machine_name() {
        return "RCuckoo";
    }

    void RCuckoo::clear_statistics(){
        Client_State_Machine::clear_statistics();
        SUCCESS("RCuckoo", "clearing statistics\n");
        _current_insert_key = Key();
        // _search_path = vector<path_element>();
        _search_path_index = 0;
        _locks_held = vector<unsigned int>();
        _locking_message_index = 0;
        return;
    }

    void RCuckoo::set_search_function(unordered_map<string, string> config) {
        string search_function_name = config["search_function"];
        // printf("setting search function: %s\n",search_function_name);
        if (search_function_name == "a_star") {
            _table_search_function = &RCuckoo::a_star_insert_self;
        } else if (search_function_name == "random") {
            _table_search_function = &RCuckoo::random_insert_self;
        } else if (search_function_name == "bfs") {
            _table_search_function = &RCuckoo::bfs_insert_self;
        } else {
            printf("ERROR: Invalid search function name\n");
            throw logic_error("ERROR: Invalid search function name");
        }
    }

    void RCuckoo::set_location_function(unordered_map<string, string> config) {
        string location_function_name = config["location_function"];
        if (location_function_name == "dependent") {
            _location_function = &rcuckoo_hash_locations;
        } else if (location_function_name == "independent") {
            _location_function = &rcuckoo_hash_locations_independent;
        } else {
            printf("ERROR: Invalid location function name\n");
            throw logic_error("ERROR: Invalid location function name");
        }
    }

    void RCuckoo::set_hash_factor(unordered_map<string, string> config) {
        try {
            string factor = config["hash_factor"];
            float hash_factor = stof(factor);
            _hash_factor = hash_factor;
            set_factor(hash_factor);
        } catch(exception &e) {
            printf("unable to parse hash factor");
        }
    }

    bool RCuckoo::a_star_insert_self() {
        _search_context.key = _current_insert_key;
        bucket_cuckoo_a_star_insert_fast_context(_search_context);
        return (_search_context.path.size() > 0);
    }

    bool RCuckoo::random_insert_self() {
        _search_context.key = _current_insert_key;
        bucket_cuckoo_random_insert_fast_context(_search_context);
        return (_search_context.path.size() > 0);
    }

    bool RCuckoo::bfs_insert_self() {
        _search_context.key = _current_insert_key;
        bucket_cuckoo_bfs_insert_fast_context(_search_context);
        return (_search_context.path.size() > 0);
    }

    void RCuckoo::complete_insert_stats(bool success){
        #ifdef MEASURE_MOST
        _insert_path_lengths.push_back(_search_context.path.size());
        _index_range_per_insert.push_back(path_index_range(_search_context.path));
        #endif
        Client_State_Machine::complete_insert_stats(success);
        return;
    }

    void RCuckoo::complete_update_stats(bool success) {
        Client_State_Machine::complete_update_stats(success);
        return;
    }

    void RCuckoo::complete_insert(){
        SUCCESS(log_id(), "[complete_insert] key %s\n", _current_insert_key.to_string().c_str());
        _state = IDLE;
        _inserting = false;
        _operation_end_time = get_current_ns();
        complete_insert_stats(true);
        return;
    }

    void RCuckoo::complete_read_stats(bool success){
        Client_State_Machine::complete_read_stats(success, _current_read_key);
        return;
    }

    void RCuckoo::complete_read(bool success){
        INFO(log_id(), "[complete_read] key %s\n", _current_read_key.to_string().c_str());
        _state = IDLE;
        _reading = false;
        _operation_end_time = get_current_ns();
        complete_read_stats(success);
        return;
    }

    void RCuckoo::complete_update(bool success) {
        INFO(log_id(), "[complete_update] key %s\n", _current_read_key.to_string().c_str());
        _state = IDLE;
        _operation_end_time = get_current_ns();
        complete_update_stats(success);
        return;
    }

    void RCuckoo::receive_successful_locking_message(unsigned int message_index){
        for (unsigned int i =0; i< _locking_context.fast_lock_chunks[message_index].size(); i++){
            // printf("locking chunk %d\n", _locking_context.fast_lock_chunks[message_index][i]);
            _locks_held.push_back(_locking_context.fast_lock_chunks[message_index][i]);
        }
        _locking_message_index++;
    }

    void RCuckoo::receive_successful_unlocking_message(unsigned int message_index) {
        for (unsigned int i =0; i< _locking_context.fast_lock_chunks[message_index].size(); i++){
            _locks_held.erase(remove(_locks_held.begin(), _locks_held.end(), _locking_context.fast_lock_chunks[message_index][i]), _locks_held.end());
        }
        assert(_locks_held.size() == set<unsigned int>(_locks_held.begin(), _locks_held.end()).size());
        _locking_message_index++;
    }

    bool RCuckoo::all_locks_released() {
        return (_locks_held.size() == 0);
    }

    bool RCuckoo::read_complete() {
        return (_outstanding_read_requests == 0);
    }

    void RCuckoo::fill_current_unlock_list() {
        get_unlock_list_from_lock_indexes(_locking_context);
    }


    /******* DIRECT RDMA CALLS ********/

    void RCuckoo::init_rdma_structures(rdma_info info){ 

        assert(info.qp != NULL);
        assert(info.completion_queue != NULL);
        assert(info.pd != NULL);

        _qp = info.qp;
        _completion_queue = info.completion_queue;
        _protection_domain = info.pd;

        _table_config = memcached_get_table_config();
        assert(_table_config != NULL);
        assert((unsigned int)_table_config->table_size_bytes == get_table_size_bytes());
        INFO(log_id(),"got a table config from the memcached server and it seems to line up\n");

        INFO(log_id(), "Registering table with RDMA device size %d, location %p\n", get_table_size_bytes(), get_table_pointer()[0]);
        _table_mr = rdma_buffer_register(_protection_domain, get_table_pointer()[0], _table.get_table_size_bytes(), MEMORY_PERMISSION);
        INFO(log_id(), "Registering lock table with RDMA device size %d, location %p\n", get_lock_table_size_bytes(), get_lock_table_pointer());
        _lock_table_mr = rdma_buffer_register(_protection_domain, get_lock_table_pointer(), get_lock_table_size_bytes(), MEMORY_PERMISSION);

        _wr_id = 10000000;
        _wc = (struct ibv_wc *) calloc (MAX_CONCURRENT_CUCKOO_MESSAGES, sizeof(struct ibv_wc));

    }


    uint64_t RCuckoo::local_to_remote_table_address(uint64_t local_address){
        uint64_t base_address = (uint64_t) get_table_pointer()[0];
        uint64_t address_offset = local_address - base_address;
        uint64_t remote_address = (uint64_t) _table_config->table_address + address_offset;
        // remote_address += 64 + (sizeof(Entry) * 2);
        return remote_address;
    }


    void RCuckoo::send_virtual_read_message(VRReadData message, uint64_t wr_id) {
        //translate address locally for bucket id
        unsigned int bucket_offset = message.offset;
        unsigned int bucket_id = message.row;
        unsigned int size = message.size;

        uint64_t local_address = (uint64_t) get_entry_pointer(bucket_id, bucket_offset);
        uint64_t remote_server_address = local_to_remote_table_address(local_address);

        // printf("table size       %d\n", _table_config->table_size_bytes);
        // printf("table address =  %p\n", _rcuckoo->get_table_pointer());
        // printf("offset pointer = %p\n", _rcuckoo->get_entry_pointer(bucket_id, bucket_offset));
        // printf("offset =         %p\n", (void *) (local_address - (uint64_t) _rcuckoo->get_table_pointer()));

        // bool success = rdmaRead(
        bool success = rdmaReadExp(
            _qp,
            local_address,
            remote_server_address,
            size,
            _table_mr->lkey,
            _table_config->remote_key,
            true,
            wr_id
        );
        if (!success) {
            printf("rdma read failed\n");
            exit(1);
        }
        VERBOSE("State Machine Wrapper", "sent virtual read message (do do do 2)\n");
        // VERBOSE("State Machine Wrapper", "sent virtual read message\n");

    }

    void RCuckoo::send_virtual_cas_message(VRCasData message, uint64_t wr_id){

        // ALERT("sending cas data", "data %s\n", message.to_string().c_str());
        uint64_t local_address = (uint64_t) get_entry_pointer(message.row, message.offset);
        uint64_t remote_server_address = local_to_remote_table_address(local_address);

        bool success = rdmaCompareAndSwap(
            _qp, 
            local_address, 
            remote_server_address,
            message.old, 
            message.new_value, 
            _table_mr->lkey,
            _table_config->remote_key, 
            true, 
            wr_id);

        if (!success) {
            printf("rdma cas failed\n");
            exit(1);
        }

    }


    void RCuckoo::send_virtual_masked_cas_message(VRMaskedCasData message, uint64_t wr_id) {
        uint64_t local_lock_address = (uint64_t) get_lock_pointer(message.min_lock_index);
        // uint64_t remote_lock_address = local_to_remote_lock_table_address(local_lock_address);
        uint64_t remote_lock_address = (uint64_t) _table_config->lock_table_address + message.min_lock_index;

        uint64_t compare = __builtin_bswap64(message.old);
        uint64_t swap = __builtin_bswap64(message.new_value);
        uint64_t mask = __builtin_bswap64(message.mask);

        VERBOSE(log_id(), "local_lock_address %lu\n", local_lock_address);
        VERBOSE(log_id(), "remote_lock_address %lu\n", remote_lock_address);
        VERBOSE(log_id(), "compare %lu\n", compare);
        VERBOSE(log_id(), "swap %lu\n", swap);
        VERBOSE(log_id(), "mask %lu\n", mask);
        VERBOSE(log_id(), "_lock_table_mr->lkey %u\n", _lock_table_mr->lkey);
        VERBOSE(log_id(), "_table_config->lock_table_key %u\n", _table_config->lock_table_key);

        bool success = rdmaCompareAndSwapMask(
            _qp,
            local_lock_address,
            remote_lock_address,
            compare,
            swap,
            _lock_table_mr->lkey,
            _table_config->lock_table_key,
            mask,
            true,
            wr_id);

        if (!success) {
            printf("rdma masked cas failed failed\n");
            exit(1);
        }
    }

    void RCuckoo::send_lock_and_cover_message(VRMaskedCasData lock_message, VRReadData read_message, uint64_t wr_id) {
        #define READ_AND_COVER_MESSAGE_COUNT 2
        struct ibv_sge sg [READ_AND_COVER_MESSAGE_COUNT];
        struct ibv_exp_send_wr wr [READ_AND_COVER_MESSAGE_COUNT];


        //Lock
        uint64_t local_lock_address = (uint64_t) get_lock_pointer(lock_message.min_lock_index);
        uint64_t remote_lock_address = (uint64_t) _table_config->lock_table_address + lock_message.min_lock_index;
        uint64_t compare = __builtin_bswap64(lock_message.old);
        uint64_t swap = __builtin_bswap64(lock_message.new_value);
        uint64_t mask = __builtin_bswap64(lock_message.mask);

        assert(mask != 0);
        assert(swap != 0);

        // ALERT(log_id(),"[lock] %5d, m%" PRIx64 ", c%016X, s%016X", remote_lock_address, mask, compare, swap);
        // printf("%2d [lock] %5d, m%016" PRIx64 ", c%016" PRIx64 ", s%016" PRIx64 "\n",_id, remote_lock_address, mask, compare, swap);
        // ALERT(log_id(),"[lock] %5d, m%lld, c%lld, s%lld", remote_lock_address, mask, compare, swap);
        // printf("%lld\n", remote_lock_address);

        setRdmaCompareAndSwapMask(
            &sg[0],
            &wr[0],
            local_lock_address,
            remote_lock_address,
            compare,
            swap,
            _lock_table_mr->lkey,
            _table_config->lock_table_key,
            mask,
            false,
            wr_id);


        bool inside_rows = read_message.row < _table.get_row_count();
        bool inside_buckets = read_message.offset < _table.get_buckets_per_row();
        if (!inside_rows) {
            ALERT(log_id(),"row %d is not inside rows %d\n", read_message.row, _table.get_row_count());
        }
        if (!inside_buckets) {
            printf(log_id(),"offset %d is not inside buckets %d\n", read_message.offset, _table.get_buckets_per_row());
        }
        assert(inside_rows);
        assert(inside_buckets);



        uint64_t local_address = (uint64_t) get_entry_pointer(read_message.row, read_message.offset);
        uint64_t remote_server_address = local_to_remote_table_address(local_address);

        //Covering Read
        setRdmaReadExp(
            &sg[1],
            &wr[1],
            local_address,
            remote_server_address,
            read_message.size,
            _table_mr->lkey,
            _table_config->remote_key,
            true,
            wr_id + 1
        );
        
        send_bulk(READ_AND_COVER_MESSAGE_COUNT, _qp, wr);

        //Increment Measurement Counters
        #ifdef MEASURE_ESSENTIAL
        uint64_t read_bytes = RDMA_READ_REQUSET_SIZE;
        read_bytes += RDMA_READ_RESPONSE_BASE_SIZE + read_message.size;
        uint64_t masked_cas_bytes = RDMA_MASKED_CAS_REQUEST_SIZE + RDMA_MASKED_CAS_RESPONSE_SIZE;

        _insert_operation_messages+=READ_AND_COVER_MESSAGE_COUNT;
        _total_bytes += read_bytes + masked_cas_bytes;
        _insert_operation_bytes += read_bytes + masked_cas_bytes;

        _read_bytes += read_bytes;
        _masked_cas_bytes +=masked_cas_bytes;
        _total_reads++;
        _total_masked_cas++;

        _current_insert_rtt++;
        _insert_rtt_count++;
        #endif
    }

    void RCuckoo::send_read(vector <VRReadData> reads, uint64_t wr_id) {

        #define MAX_READ_PACKETS 2
        struct ibv_sge sg [MAX_READ_PACKETS];
        struct ibv_exp_send_wr wr [MAX_READ_PACKETS];

        for(size_t i=0;i<reads.size();i++) {
            uint64_t local_address = (uint64_t) get_entry_pointer(reads[i].row, reads[i].offset);
            uint64_t remote_server_address = local_to_remote_table_address(local_address);

            bool signal = (i == reads.size() - 1);            

            setRdmaReadExp(
                &sg[i],
                &wr[i],
                local_address,
                remote_server_address,
                reads[i].size,
                _table_mr->lkey,
                _table_config->remote_key,
                signal,
                wr_id + i
            );
        }
        send_bulk(reads.size(), _qp, wr);
        #ifdef MEASURE_ESSENTIAL
        uint64_t read_bytes = RDMA_READ_REQUSET_SIZE * reads.size();
        for(size_t i=0;i<reads.size();i++) {
            read_bytes += RDMA_READ_RESPONSE_BASE_SIZE + reads[i].size;
        }
        _read_operation_messages+=reads.size();
        _total_bytes += read_bytes;
        _read_operation_bytes += read_bytes;
        _read_bytes += read_bytes;
        _total_reads++;
        _current_read_rtt++;
        #endif

    }

    void RCuckoo::send_insert_and_crc(VRCasData insert_message, ibv_sge *sg, ibv_exp_send_wr *wr, uint64_t *wr_id) {
        #ifndef ROW_CRC
        ALERT("ROW CRC", "dont run this code unless we are running row CRC");
        assert(false);
        #endif

        uint64_t local_address = (uint64_t) get_entry_pointer(insert_message.row, insert_message.offset);
        uint64_t remote_server_address = local_to_remote_table_address(local_address);


        // printf("local_address %lX remote_server_address %lX\n", local_address, remote_server_address);
        int32_t imm = -1;
        setRdmaWriteExp(
            &sg[0],
            &wr[0],
            local_address,
            remote_server_address,
            sizeof(Entry),
            _table_mr->lkey,
            _table_config->remote_key,
            imm,
            false,
            *wr_id
        );
        (*wr_id)++;

        uint64_t crc = _table.crc64_row(insert_message.row);
        Entry crc_entry;
        crc_entry.set_as_uint64_t(crc);
        _table.set_entry(insert_message.row, _table.get_entries_per_row(), crc_entry);
        uint64_t crc_address = (uint64_t) get_entry_pointer(insert_message.row, _table.get_entries_per_row());

        uint64_t remote_server_crc_address = local_to_remote_table_address(crc_address);
        // printf("crc_address %lX remote_server_crc_address %lX\n", crc_address, remote_server_crc_address);
        setRdmaWriteExp(
            &sg[1],
            &wr[1],
            crc_address,
            remote_server_crc_address,
            sizeof(uint64_t),
            _table_mr->lkey,
            _table_config->remote_key,
            imm,
            false,
            *wr_id
        );
        (*wr_id)++;


    }

    void RCuckoo::send_insert_crc_and_unlock_messages_with_fault(vector<VRCasData> &insert_messages, vector<VRMaskedCasData> & unlock_messages, unsigned int fault) {
        struct ibv_sge sg [MAX_INSERT_CRC_AND_UNLOCK_MESSAGE_COUNT];
        struct ibv_exp_send_wr wr [MAX_INSERT_CRC_AND_UNLOCK_MESSAGE_COUNT];

        #ifndef ROW_CRC
        ALERT("ROW CRC", "dont run this code unless we are running row CRC (send_insert_crc_and_unlock_messages)");
        assert(false);
        #endif

        //There are 5 distinct fault scenarios
        assert(fault >=0 && fault <= 4);
        ALERT(log_id(), "Executing insert fault %d\n", fault);

        //Make sure that we are actually doing something here.
        assert(insert_messages.size() >= 1 && unlock_messages.size() >= 1);

        int insert_messages_with_crc = insert_messages.size()*2;
        int total_messages = (insert_messages_with_crc) + unlock_messages.size();

        if (total_messages > MAX_INSERT_AND_UNLOCK_MESSAGE_COUNT) {
            ALERT(log_id(),"Too many messages to send\n");
            ALERT(log_id(), "insert_messages.size() %lu\n", insert_messages.size());
            ALERT(log_id(), "unlock_messages.size() %lu\n", unlock_messages.size());
            for (auto message : insert_messages) {
                ALERT(log_id(), "insert_messages %d %d %lu\n", message.row, message.offset, message.new_value);
            }
            for (auto message : unlock_messages) {
                ALERT(log_id(), "unlock_messages %d %lX %lX\n", message.min_lock_index, message.old, message.new_value);
            }
            exit(1);
        }
        assert(total_messages <= MAX_INSERT_AND_UNLOCK_MESSAGE_COUNT);

        //Fault case 0 is that no insert, cas or unlock messages are sent.
        //We leave the table in a locked state
        if (fault == FAULT_CASE_0) {
            ALERT(log_id(), "Fault Case 0: %d locks are left set\n", _locking_context.lock_indexes_size);
            for (int i=0;i<_locking_context.lock_indexes_size;i++) {
                ALERT(log_id(), "Lock %d left in set state\n", _locking_context.lock_indexes[i]);
            }
            vector<unsigned int> locked_buckets;
            lock_indexes_to_buckets_context(locked_buckets, _locks_held, _locking_context);
            for (int i=0;i<locked_buckets.size(); i++){
                ALERT(log_id(), "Bucket %d Locked\n",locked_buckets[i]);
            }
            ALERT(log_id(), "Spinning Forever to simulate a failure... (todo report statistics some other time)\n");
            while(true){}
        }

    }


    void RCuckoo::send_insert_crc_and_unlock_messages(vector<VRCasData> &insert_messages, vector<VRMaskedCasData> & unlock_messages, uint64_t wr_id) {

        struct ibv_sge sg [MAX_INSERT_CRC_AND_UNLOCK_MESSAGE_COUNT];
        struct ibv_exp_send_wr wr [MAX_INSERT_CRC_AND_UNLOCK_MESSAGE_COUNT];

        #ifndef ROW_CRC
        ALERT("ROW CRC", "dont run this code unless we are running row CRC (send_insert_crc_and_unlock_messages)");
        assert(false);
        #endif

        #ifdef INJECT_FAULT
        if (_id == 0){
            send_insert_crc_and_unlock_messages_with_fault(insert_messages,unlock_messages,INJECT_FAULT);
        }
        #endif

        int insert_messages_with_crc = insert_messages.size()*2;
        int total_messages = (insert_messages_with_crc) + unlock_messages.size();

        if (total_messages > MAX_INSERT_AND_UNLOCK_MESSAGE_COUNT) {
            ALERT(log_id(),"Too many messages to send\n");
            ALERT(log_id(), "insert_messages.size() %lu\n", insert_messages.size());
            ALERT(log_id(), "unlock_messages.size() %lu\n", unlock_messages.size());
            for (auto message : insert_messages) {
                ALERT(log_id(), "insert_messages %d %d %lu\n", message.row, message.offset, message.new_value);
            }
            for (auto message : unlock_messages) {
                ALERT(log_id(), "unlock_messages %d %lX %lX\n", message.min_lock_index, message.old, message.new_value);
            }
            exit(1);
        }
        assert(total_messages <= MAX_INSERT_AND_UNLOCK_MESSAGE_COUNT);

        for ( unsigned int i=0; i < insert_messages.size(); i++) {
            send_insert_and_crc(insert_messages[i], &sg[i*2], &wr[i*2], &wr_id);
        }
        for ( unsigned int i=0; i < unlock_messages.size(); i++) {
            set_unlock_message(unlock_messages[i], &sg[i + insert_messages_with_crc], &wr[i + insert_messages_with_crc], &wr_id);
        }
        set_signal(&wr[total_messages-1]);

        send_bulk(total_messages, _qp, wr);
        #ifdef MEASURE_ESSENTIAL
        //TODO WRITE SIZES ARE WRONG
        uint64_t cas_bytes = (RDMA_CAS_REQUEST_SIZE + RDMA_CAS_RESPONSE_SIZE) * insert_messages.size();
        uint64_t masked_cas_bytes = (RDMA_MASKED_CAS_REQUEST_SIZE + RDMA_MASKED_CAS_RESPONSE_SIZE) * unlock_messages.size();

        _total_bytes += cas_bytes + masked_cas_bytes;
        _cas_bytes += cas_bytes;
        _masked_cas_bytes +=masked_cas_bytes;

        _total_cas += insert_messages.size();
        _total_masked_cas += unlock_messages.size();

        _current_insert_messages += total_messages;
        _insert_operation_messages += total_messages;
        _current_insert_rtt++;
        _insert_rtt_count++;
        #endif


    }
    

    void RCuckoo::set_insert(VRCasData &insert_message, struct ibv_sge *sg, struct ibv_exp_send_wr *wr, uint64_t *wr_id) {

            uint64_t local_address = (uint64_t) get_entry_pointer(insert_message.row, insert_message.offset);
            uint64_t remote_server_address = local_to_remote_table_address(local_address);

            int32_t imm = -1;
            setRdmaWriteExp(
                sg,
                wr,
                local_address,
                remote_server_address,
                sizeof(Entry),
                _table_mr->lkey,
                _table_config->remote_key,
                imm,
                false,
                (*wr_id)
            );
            (*wr_id)++;
    }

    void RCuckoo::set_unlock_message(VRMaskedCasData &unlock_message, struct ibv_sge *sg, struct ibv_exp_send_wr *wr, uint64_t *wr_id) {
        uint64_t local_lock_address = (uint64_t) get_lock_pointer(unlock_message.min_lock_index);
        uint64_t remote_lock_address = (uint64_t) _table_config->lock_table_address + unlock_message.min_lock_index;
        uint64_t compare = __builtin_bswap64(unlock_message.old);
        uint64_t swap = __builtin_bswap64(unlock_message.new_value);
        uint64_t mask = __builtin_bswap64(unlock_message.mask);

        setRdmaCompareAndSwapMask(
            sg,
            wr,
            local_lock_address,
            remote_lock_address,
            compare,
            swap,
            _lock_table_mr->lkey,
            _table_config->lock_table_key,
            mask,
            false,
            *wr_id);
        (*wr_id)++;
    }

    void RCuckoo::send_insert_and_unlock_messages(vector<VRCasData> &insert_messages, vector<VRMaskedCasData> & unlock_messages, uint64_t wr_id) {
        struct ibv_sge sg [MAX_INSERT_AND_UNLOCK_MESSAGE_COUNT];
        struct ibv_exp_send_wr wr [MAX_INSERT_AND_UNLOCK_MESSAGE_COUNT];


        if (insert_messages.size() + unlock_messages.size() > MAX_INSERT_AND_UNLOCK_MESSAGE_COUNT) {
            ALERT(log_id(),"Too many messages to send\n");
            ALERT(log_id(), "insert_messages.size() %lu\n", insert_messages.size());
            ALERT(log_id(), "unlock_messages.size() %lu\n", unlock_messages.size());
            for (auto message : insert_messages) {
                ALERT(log_id(), "insert_messages %d %d %lu\n", message.row, message.offset, message.new_value);
            }
            for (auto message : unlock_messages) {
                ALERT(log_id(), "unlock_messages %d %lX %lX\n", message.min_lock_index, message.old, message.new_value);
            }
            exit(1);
        }
        assert(insert_messages.size() + unlock_messages.size() <= MAX_INSERT_AND_UNLOCK_MESSAGE_COUNT);


        int total_messages = insert_messages.size() + unlock_messages.size();
        for ( unsigned int i=0; i < insert_messages.size(); i++) {
            set_insert(insert_messages[i], &sg[i], &wr[i], &wr_id);
        }
        for ( unsigned int i=0; i < unlock_messages.size(); i++) {
            set_unlock_message(unlock_messages[i], &sg[i + insert_messages.size()], &wr[i + insert_messages.size()], &wr_id);
        }
        set_signal(&wr[total_messages - 1]);

        send_bulk(total_messages, _qp, wr);
        #ifdef MEASURE_ESSENTIAL
        uint64_t cas_bytes = (RDMA_CAS_REQUEST_SIZE + RDMA_CAS_RESPONSE_SIZE) * insert_messages.size();
        uint64_t masked_cas_bytes = (RDMA_MASKED_CAS_REQUEST_SIZE + RDMA_MASKED_CAS_RESPONSE_SIZE) * unlock_messages.size();

        _total_bytes += cas_bytes + masked_cas_bytes;
        _cas_bytes += cas_bytes;
        _masked_cas_bytes +=masked_cas_bytes;

        _total_cas += insert_messages.size();
        _total_masked_cas += unlock_messages.size();

        _current_insert_messages += total_messages;
        _insert_operation_messages += total_messages;
        _current_insert_rtt++;
        _insert_rtt_count++;
        #endif
        
    }

    //Precondition _locking_context.buckets contains the locks we want to get
    void RCuckoo::top_level_aquire_locks() {
        get_lock_list_fast_context(_locking_context);
        INFO(log_id(), "[aquire_locks] gathering locks for buckets %s\n", vector_to_string(_locking_context.buckets).c_str());
        //TODO make this one thing
        // get_covering_reads_from_lock_list(_locking_context.lock_list, _covering_reads ,_buckets_per_lock, _table.row_size_bytes());
        get_covering_reads_context(_locking_context, _covering_reads, _table, _buckets_per_lock);

        for (unsigned int i = 0; i < _locking_context.lock_list.size(); i++) {
            INFO(log_id(), "[aquire_locks] lock %d -> [lock %s] [read %s]\n", i, _locking_context.lock_list[i].to_string().c_str(), _covering_reads[i].to_string().c_str());
        }

        if(_locking_context.lock_list.size() != _covering_reads.size()) {
            ALERT(log_id(), "[aquire_locks] lock_list.size() != covering_reads.size() %lu != %lu\n", _locking_context.lock_list.size(), _covering_reads.size());
            exit(1);
        }
        assert(_locking_context.lock_list.size() == _covering_reads.size());

        bool locking_complete = false;
        bool failed_last_request = false;

        unsigned int message_index = 0;
        while (!locking_complete) {

            assert(message_index < _locking_context.lock_list.size());

            VRMaskedCasData lock = _locking_context.lock_list[message_index];
            VRReadData read = _covering_reads[message_index];

            //This is for testing the benifit of locks only
            if (!_use_mask) {
                lock.mask = 0xFFFFFFFFFFFFFFFF;
            }

            _wr_id++;
            uint64_t outstanding_cas_wr_id = _wr_id;
            _wr_id++;

            send_lock_and_cover_message(lock, read, outstanding_cas_wr_id);
            // auto t1 = high_resolution_clock::now();
            int outstanding_messages = 1; //It's two because we send the read and CAS
            int n = bulk_poll(_completion_queue, outstanding_messages, _wc);

            while (n < outstanding_messages) {
                VERBOSE(log_id(), "first poll missed the read, polling again\n");
                assert(_wc);
                assert(_completion_queue);
                assert(_wc + n);
                assert(outstanding_messages - n > 0);
                n += bulk_poll(_completion_queue, outstanding_messages - n, _wc + n);
            }

            _outstanding_read_requests--;
            assert(n == outstanding_messages); //This is just a safty for now.
            if (_wc[0].status != IBV_WC_SUCCESS) {
                ALERT("lock aquire", " masked cas failed somehow\n");
                ALERT("lock aquire", " masked cas %s\n", lock.to_string().c_str());
                exit(1);
            }

            if (_wc[1].status != IBV_WC_SUCCESS) {
                ALERT(log_id(), " [lock aquire] spanning read failed somehow\n");

                ALERT(log_id(), " errno: %d \n", -errno);
                ALERT(log_id(), " [lock aquire] read %s\n", read.to_string().c_str());
                ALERT(log_id(), " [lock aquire] table size %d\n", _table.get_row_count());
                exit(1);
            }

            uint64_t old_value = lock.old;
            uint64_t mask = lock.mask;
            int lock_index = lock.min_lock_index;

            if (!(lock_index >= 0 && lock_index < (int)_table.get_underlying_lock_table_size_bytes())) {
                WARNING(log_id(), "assert about to fail, lock_index = %d, lock_table_size = %d\n", lock_index, _table.get_underlying_lock_table_size_bytes());
            }
            assert(lock_index >= 0 && lock_index < (int)_table.get_underlying_lock_table_size_bytes());
            assert(get_lock_pointer(lock_index));

            uint64_t received_locks = *((uint64_t*) get_lock_pointer(lock_index));

            //?? bswap the DMA'd value?
            received_locks = __builtin_bswap64(received_locks);
            *((uint64_t*) get_lock_pointer(lock_index)) = received_locks;


            if ((received_locks & mask) == old_value) {
                // ALERT(log_id(), "we got the lock!\n");
                // receive_successful_locking_message(lock);
                receive_successful_locking_message(message_index);
                message_index++;
                if (failed_last_request) {
                    failed_last_request = false;
                }
            } else {
                // printf("%2d [fail lock]   r%016" PRIx64 ", m%016" PRIx64 ", o%016" PRIx64 "\n", _id, received_locks, mask, old_value);
                #ifdef MEASURE_ESSENTIAL
                _total_masked_cas_failures++;
                _failed_lock_aquisition_count++;
                _failed_lock_aquisition_this_insert++;
                #endif
                failed_last_request = true;



                //Here we are going to try to triage which lock is the one that is set for too long
                // ALERT(log_id(),"Failed to grab the lock, this is where we will start to do fault recovery\n");
                ALERT(log_id(),"Failed to get the lock %s\n", lock.to_string().c_str(), mask);
                ALERT(log_id(),"unable to set %lX\n", mask & received_locks);
                unsigned int locks[64];
                unsigned int total_locks = lock_message_to_lock_indexes(lock,locks);
                for (int i=0;i<total_locks;i++) {
                    ALERT(log_id(), "Unable to aquire lock %d\n",locks[i]);
                }
            }

            if (_locking_context.lock_list.size() == message_index) {
                locking_complete = true;
                INFO(log_id(), " [put-direct] we got all the locks!\n");
                break;
            }
        }

    }


    void RCuckoo::insert_direct() {

        _state = INSERTING;

        // assert(_buckets_per_lock == 1);
        //Search path is now set
        if(!path_valid()) {
            _failed_insert_first_search_this_insert++;
            _failed_insert_first_search_count++;
            INFO(log_id(), "Path is not valid\n");
            INFO(log_id(), "first path %s\n", path_to_string(_search_context.path).c_str());
            // lock_indexes_to_buckets(_search_context.open_buckets, _locks_held, _buckets_per_lock);
            lock_indexes_to_buckets_context(_search_context.open_buckets, _locks_held, _locking_context);

            bool successful_search = (this->*_table_search_function)();
            _search_path_index = _search_context.path.size() -1;
            if (!successful_search) {
                _failed_insert_second_search_this_insert++;
                _failed_insert_second_search_count++;
                // printf("[%d] failed search current locks held: %s\n", _id, vector_to_string(_locks_held).c_str());
                INFO(log_id(), "Insert Direct -- Second Search Failed for key %s \n", _current_insert_key.to_string().c_str());
                INFO(log_id(), "Unable to find path within buckets %s\n", vector_to_string(_search_context.open_buckets).c_str());

                INFO(log_id(), "Hi Stew, I\'m hoping you have a great day", _current_insert_key.to_string().c_str(), _id);
                _state = RELEASE_LOCKS_TRY_AGAIN;
            } else {
                INFO(log_id(), "second search succeeded\n");
                INFO(log_id(), "new path %s", path_to_string(_search_context.path).c_str());
                if(!path_valid()){
                    ALERT(log_id(), "Path is not valid second search is broken\n");
                    exit(1);
                }
            }
        }


        _locking_message_index = 0;
        fill_current_unlock_list();
        // _lock_list is now full

        INFO("insert direct", "about to unlock a total of %d lock messages\n", _locking_context.lock_list.size());
        unsigned int total_messages = _locking_context.lock_list.size();
        if (_state == INSERTING) {
            gen_cas_data(_search_context.path, _insert_messages);
            total_messages += _insert_messages.size();
        } else {
            _insert_messages.clear();
        }


        if (_state == INSERTING) {
            insert_cuckoo_path_local(_table, _search_context.path);
        }
        #ifdef ROW_CRC
        send_insert_crc_and_unlock_messages(_insert_messages, _locking_context.lock_list, _wr_id);
        #else
        send_insert_and_unlock_messages(_insert_messages, _locking_context.lock_list, _wr_id);
        #endif
        _wr_id += total_messages;


        //Bulk poll to receive all messages
        bulk_poll(_completion_queue, 1, _wc + 1);
        _locks_held.clear();

        if (_state == INSERTING) {
            // #define VALIDATE_INSERT
            insert_cuckoo_path_local(_table, _search_context.path);
            //finish the insert by inserting the key into the local table.
            complete_insert();
        } else if (_state == RELEASE_LOCKS_TRY_AGAIN) {
            return;
        } else {
            printf("invalid state\n");
            exit(1);
        }
        return;

    }


    void RCuckoo::put_direct() {

        /* copied from the search function */ 
        _search_context.open_buckets.clear();
        bool successful_search = (this->*_table_search_function)();
        //Search failed
        //Search path is now set

        if (!successful_search)  {
            ALERT(log_id(), "Search Failed for key %s unable to continue client %d is done\n", _current_insert_key.to_string().c_str(), _id);
            // _table.print_table();
            _complete=true;
            _state = IDLE;
            return;
        }
        // VERBOSE("search", "Successful local search for [key %s] -> [path %s]\n", _current_insert_key.to_string().c_str(), path_to_string(_search_path).c_str());
        _state = AQUIRE_LOCKS;


        /* copied from aquire locks function */ 
        _locking_message_index = 0;

        

        search_path_to_buckets_fast(_search_context.path, _locking_context.buckets);
        // assert(_locking_context.buckets.size() < 64);
        top_level_aquire_locks();

        return insert_direct();
    }

    void RCuckoo::get_direct(void) {
        read_theshold_message(_reads, _location_function, _current_read_key, _read_threshold_bytes, _table.get_row_count(), _table.get_buckets_per_row());
        send_read(_reads, _wr_id);

        int outstanding_messages = _reads.size();
        int n =0;
        while (n < 1) {
            //We only signal a single read, so we should only get a single completion
            n = bulk_poll(_completion_queue, outstanding_messages - n, _wc + n);
        }

        //At this point we should have the values
        hash_locations buckets = _location_function(_current_read_key, _table.get_row_count());

        bool success = (_table.bucket_contains(buckets.primary, _current_read_key) || _table.bucket_contains(buckets.secondary, _current_read_key));
        if (success) {
            SUCCESS(log_id(), "%2d found key %s \n", _id, _current_read_key.to_string().c_str());
        } else {
            ALERT("[get-direct]", "%2d did not find key %s \n", _id, _current_read_key.to_string().c_str());
            complete_read(success);
            return;
        }

        success |= _table.crc_valid_row(buckets.primary);
        success |= _table.crc_valid_row(buckets.secondary);

        if (success) {
            SUCCESS(log_id(), "%2d found key %s in vaild row\n", _id, _current_read_key.to_string().c_str());
        } else {
            ALERT("[get-direct]", "%2d did not find key %s in vaild row\n", _id, _current_read_key.to_string().c_str());
            complete_read(success);
            return;
        }

        SUCCESS("[get-direct]", "%2d found key %s \n", _id, _current_read_key.to_string().c_str());
        complete_read(success);
        return;
    }

    void RCuckoo::update_direct(void) {
        // assert(_locking_context.buckets.size() < 64);
        hash_locations buckets = _location_function(_current_update_key, _table.get_row_count());
        _locking_context.buckets.clear();
        _locking_context.buckets.push_back(buckets.primary);
        _locking_context.buckets.push_back(buckets.secondary);
        top_level_aquire_locks();

        bool b1 = _table.bucket_contains(buckets.primary, _current_update_key);
        bool b2 = _table.bucket_contains(buckets.secondary, _current_update_key);
        bool success = b1 || b2;

        unsigned int bucket;
        if (b1) {
            SUCCESS("[update-direct]", "%2d found key %s in primay bucket\n", _id, _current_update_key.to_string().c_str());
            bucket = buckets.primary;
        } else if (b2) {
            SUCCESS("[update-direct]", "%2d found key %s in secondary bucket\n", _id, _current_update_key.to_string().c_str());
            bucket = buckets.secondary;
        } else {
            ALERT("[update-direct]", "%2d did not find key %s \n", _id, _current_update_key.to_string().c_str());
            ALERT("[update-direct]", "primany,seconday [%d,%d]\n", buckets.primary, buckets.secondary);
            return;
        }

        VRCasData insert;
        insert.row = bucket;
        insert.offset = _table.get_keys_offset_in_row(bucket, _current_update_key);
        insert.new_value = 1337;
        _insert_messages.clear();
        _insert_messages.push_back(insert);
        //get offset

        _locking_message_index = 0;
        fill_current_unlock_list();

        INFO("update direct", "about to unlock a total of %d lock messages\n", _locking_context.lock_list.size());
        unsigned int total_messages = _locking_context.lock_list.size();

        #ifdef ROW_CRC
        send_insert_crc_and_unlock_messages(_insert_messages, _locking_context.lock_list, _wr_id);
        #else
        send_insert_and_unlock_messages(_insert_messages, _locking_context.lock_list, _wr_id);
        #endif
        _wr_id += total_messages;

        //Bulk poll to receive all messages
        bulk_poll(_completion_queue, 1, _wc + 1);
        _locks_held.clear();
        complete_update(success);

        return;
    }


    void RCuckoo::rdma_fsm(void) {

        //Hold here until the global start flag is set
        while(!*_global_start_flag){
            VERBOSE(log_id(), "not globally started");
        };
        INFO(log_id(),"Starting RDMA FSM Start Flag Set\n");

        //Prior to starting we are going to always set the workload driver to fill
        _workload_driver.set_workload(W);

        while(!*_global_end_flag) {


            //The client is done
            if (_complete && _state == IDLE) {
                return;
            }

            Request next_request;
            switch(_state) {
                case IDLE:

                    //Pause goes here rather than anywhere else because I don't want
                    //To have locks, or any outstanding requests
                    if (*_global_prime_flag && !_local_prime_flag){
                        _local_prime_flag = true;
                        clear_statistics();
                        //Set the workload to what we actually want to be working with
                        _workload_driver.set_workload(_workload);
                    }
                    next_request = _workload_driver.next();
                    VERBOSE("DEBUG: general idle fsm","Generated New Request: %s\n", next_request.to_string().c_str());

                    if (next_request.op == NO_OP) {
                            break;
                    } else if (next_request.op == PUT) {
                        _operation_start_time = get_current_ns();
                        _current_insert_key = next_request.key;
                        // ALERT("[gen key]","trying to put %s\n", _current_insert_key.to_string().c_str());
                        put_direct();
                    } else if (next_request.op == GET) {
                        _operation_start_time = get_current_ns();
                        _current_read_key = next_request.key;
                        // ALERT("[gen key]","trying to read %s\n", _current_read_key.to_string().c_str());
                        // throw logic_error("ERROR: GET not implemented");
                        get_direct();
                    } else if (next_request.op == UPDATE) {
                        _operation_start_time = get_current_ns();
                        _current_update_key = next_request.key;
                        // ALERT("[gen key]","trying to read %s\n", _current_read_key.to_string().c_str());
                        // throw logic_error("ERROR: GET not implemented");
                        update_direct();
                    } else {
                        printf("ERROR: unknown operation\n");
                        throw logic_error("ERROR: unknown operation");
                    }
                    break;


                case READING:
                    // read_fsm(message);
                    throw logic_error("ERROR: reading not implemented");
                    break;
                case RELEASE_LOCKS_TRY_AGAIN:

                    put_direct();
                    break;
                default:
                    throw logic_error("ERROR: Invalid state");
            }


        }
        INFO(log_id(), "BREAKING EXIT FLAG!!\n");

        if(*_global_end_flag == true) {
            _complete = true;
        }

        return;
    }
}
