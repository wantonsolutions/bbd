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
#include "../slib/config.h"
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
#define MEAN_RTT_US 2
#define FAULT_HOST "yeti-00.sysnet.ucsd.edu"

// #define INJECT_FAULT FAULT_CASE_0


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

    void * RCuckoo::get_repair_lease_table_pointer() {
        return _table.get_underlying_repair_lease_table_address();
    }

    void * RCuckoo::get_lock_pointer(unsigned int lock_index){
        return _table.get_lock_pointer(lock_index);
    }

    unsigned int RCuckoo::get_lock_table_size_bytes(){
        return _table.get_underlying_lock_table_size_bytes();
    }

    unsigned int RCuckoo::get_repair_lease_table_size_bytes(){
        return _table.get_underlying_repair_lease_table_size_bytes();
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

    bool RCuckoo::insert_cuckoo_path_local(Table &table, vector<path_element> &path) {
        assert(path.size() >= 2);
        Entry e;
        for (int i=path.size()-2; i >=0; i--){
            e.key = path[i+1].key;
            // ALERT("insert local","Inserting key %s into bucket %d offset %d", e.key.to_string().c_str(), path[i].bucket_index, path[i].offset);
            #ifdef ROW_CRC
            table.set_entry_with_crc(path[i].bucket_index, path[i].offset, e);
            #else
            table.set_entry(path[i].bucket_index, path[i].offset, e);
            #endif
        }
        return true;
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
            unsigned int memory_size = get_config_int(config,"memory_size");
            unsigned int bucket_size = get_config_int(config,"bucket_size");
            unsigned int buckets_per_lock = get_config_int(config,"buckets_per_lock");
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
            _read_threshold_bytes = get_config_int(config,"read_threshold_bytes");
            _buckets_per_lock = get_config_int(config,"buckets_per_lock");
            _locks_per_message = get_config_int(config,"locks_per_message");
            // assert(_locks_per_message == 64);
            _starting_id = get_config_int(config,"starting_id");
            _global_clients = get_config_int(config,"global_clients");
            _id = get_config_int(config,"id") + _starting_id;
            _use_mask = get_config_bool(config,"use_mask");
            use_virtual_lock_table = get_config_bool(config,"use_virtual_lock_table");
            virtual_lock_scale_factor = get_config_int(config,"virtual_lock_scale_factor");
            _simulate_failures = get_config_bool(config,"simulate_failures");
            _lock_timeout_us = get_config_int(config,"lock_timeout_us");
            _lease_timeout_us = get_config_int(config,"lease_timeout_us");
            _delay_between_failures_us = get_config_int(config,"delay_between_failures_us");
            

        } catch (exception& e) {
            printf("%s\n", e.what());
            throw logic_error("ERROR: RCuckoo config missing required field");
        }
        gethostname(hostname, HOST_NAME_MAX);
        if (_simulate_failures && strcmp(hostname,FAULT_HOST)==0 && _id == 0 ) {
            ALERT("Generating Faults on client","%s %d",hostname,_id);
            _generate_failures_on_this_client = true;
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
        // _search_path = vector<path_element>();
        // _current_insert_key = Key();
        // _search_path_index = 0;
        // _locks_held = vector<unsigned int>();
        // _locking_message_index = 0;
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
        // ALERT(log_id(), "got table config %s",_table_config->to_string().c_str());
        assert(_table_config != NULL);
        assert((unsigned int)_table_config->table_size_bytes == get_table_size_bytes());
        INFO(log_id(),"got a table config from the memcached server and it seems to line up\n");

        INFO(log_id(), "Registering table with RDMA device size %d, location %p\n", get_table_size_bytes(), get_table_pointer()[0]);
        _table_mr = rdma_buffer_register(_protection_domain, get_table_pointer()[0], _table.get_table_size_bytes(), MEMORY_PERMISSION);
        INFO(log_id(), "Registering lock table with RDMA device size %d, location %p\n", get_lock_table_size_bytes(), get_lock_table_pointer());
        _lock_table_mr = rdma_buffer_register(_protection_domain, get_lock_table_pointer(), get_lock_table_size_bytes(), MEMORY_PERMISSION);
        INFO(log_id(), "Registering repair lease table with RDMA device size %d, location %p\n", get_repair_lease_table_size_bytes(), get_repair_lease_table_pointer());
        _repair_lease_mr = rdma_buffer_register(_protection_domain, _table.get_repair_lease_pointer(0), get_repair_lease_table_size_bytes(), MEMORY_PERMISSION);

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

    uint64_t RCuckoo::local_to_remote_repair_lease_address(uint64_t local_address) {
        uint64_t base_address = (uint64_t) _table.get_repair_lease_pointer(0);
        uint64_t address_offset = local_address - base_address;
        assert(address_offset == 0);
        uint64_t remote_address = (uint64_t) _table_config->lease_table_address + address_offset;
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

    void RCuckoo::send_lock_and_cover_message(VRMaskedCasData lock_message, vector<VRReadData> read_messages) {
        #define READ_AND_COVER_MESSAGE_COUNT 64
        struct ibv_sge sg [READ_AND_COVER_MESSAGE_COUNT];
        struct ibv_exp_send_wr wr [READ_AND_COVER_MESSAGE_COUNT];

        int total_messages = read_messages.size() + 1;
        assert(total_messages< READ_AND_COVER_MESSAGE_COUNT);



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
            _wr_id);
        _wr_id++;
        wr[0].exp_send_flags |= IBV_EXP_SEND_FENCE;

        for (int i=0;i<read_messages.size();i++){

            VRReadData read_message = read_messages[0];
            assert(read_message.row < _table.get_row_count());
            assert(read_message.offset < _table.get_buckets_per_row());

            uint64_t local_address = (uint64_t) get_entry_pointer(read_message.row, read_message.offset);
            uint64_t remote_server_address = local_to_remote_table_address(local_address);

            //Covering Read
            setRdmaReadExp(
                &sg[i+1],
                &wr[i+1],
                local_address,
                remote_server_address,
                read_message.size,
                _table_mr->lkey,
                _table_config->remote_key,
                false,
                _wr_id
            );
            _wr_id++;
            INFO("send lock and cover", "read: row, offset, size %d, %d, %d\n", read_message.row, read_message.offset, read_message.size);
            #ifdef MEASURE_ESSENTIAL
            uint64_t read_bytes = RDMA_READ_REQUSET_SIZE;
            read_bytes += RDMA_READ_RESPONSE_BASE_SIZE + read_message.size;
            _total_bytes += read_bytes;
            _insert_operation_bytes += read_bytes;
            _read_bytes += read_bytes;
            _total_reads++;
            #endif
        }

        #ifdef MEASURE_ESSENTIAL
        _insert_operation_messages+=read_messages.size() + 1;
        uint64_t masked_cas_bytes = RDMA_MASKED_CAS_REQUEST_SIZE + RDMA_MASKED_CAS_RESPONSE_SIZE;
        _total_bytes += masked_cas_bytes;
        _insert_operation_bytes += masked_cas_bytes;
        _masked_cas_bytes +=masked_cas_bytes;
        _total_masked_cas++;
        _current_insert_rtt++;
        _insert_rtt_count++;
        #endif

        // wr[read_messages.size()].exp_send_flags |= IBV_EXP_SEND_FENCE;
        wr[read_messages.size()].exp_send_flags |= IBV_EXP_SEND_SIGNALED;
        
        send_bulk(total_messages, _qp, wr);

        //Increment Measurement Counters

        int outstanding_messages = 1; //It's two because we send the read and CAS
        int n = bulk_poll(_completion_queue, outstanding_messages, _wc);
        while (n < outstanding_messages) {
            n += bulk_poll(_completion_queue, outstanding_messages - n, _wc + n);
        }

        //This is just extra safty we can probably remove it
        // _outstanding_read_requests--;
        // assert(n == outstanding_messages); //This is just a safty for now.
        // bool fail_on_this_request = false;

        // if (_wc[0].status != IBV_WC_SUCCESS) {
        //     ALERT("lock aquire", "Failed on lock %s\n", lock_message.to_string().c_str());
        //     ALERT("lock aquire", " masked cas failed somehow\n");
        //     ALERT("lock aquire", " masked cas %s\n", lock_message.to_string().c_str());
        //     print_wc(&_wc[0]);
        //     fail_on_this_request = true;
        // }
        // if (_wc[1].status != IBV_WC_SUCCESS) {
        //     print_wc(&_wc[1]);
        //     ALERT(log_id(), " [lock aquire] spanning read failed somehow\n");
        //     ALERT(log_id(), " errno: %d \n", -errno);
        //     ALERT(log_id(), " [lock aquire] read %s\n", read_message.to_string().c_str());
        //     ALERT(log_id(), " [lock aquire] table size %d\n", _table.get_row_count());
        //     fail_on_this_request = true;
        // }
        // if (fail_on_this_request) {
        //     exit(1);
        // }

    }

    void RCuckoo::send_read(vector <VRReadData> reads) {

        #define MAX_READ_PACKETS 64
        struct ibv_sge sg [MAX_READ_PACKETS];
        struct ibv_exp_send_wr wr [MAX_READ_PACKETS];
        assert(reads.size() <= MAX_READ_PACKETS);

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
                _wr_id
            );
            _wr_id++;
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

        uint64_t crc_address = (uint64_t) get_entry_pointer(insert_message.row, _table.get_entries_per_row());
        uint64_t remote_server_crc_address = local_to_remote_table_address(crc_address);


        uint64_t local_address = (uint64_t) get_entry_pointer(insert_message.row, insert_message.offset);
        uint64_t remote_server_address = local_to_remote_table_address(local_address);


        //Perhaps we could insert the entry locally here too?

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

    bool RCuckoo::send_insert_crc_and_unlock_messages_with_fault(vector<VRCasData> &insert_messages, vector<VRMaskedCasData> & unlock_messages, unsigned int fault, unsigned int max_fault_rate_us) {

        struct ibv_sge sg [MAX_INSERT_CRC_AND_UNLOCK_MESSAGE_COUNT];
        struct ibv_exp_send_wr wr [MAX_INSERT_CRC_AND_UNLOCK_MESSAGE_COUNT];

        // if (insert_messages.size() < 2) {
        //     // ALERT(log_id(), "trying to insert a fault %d, but we want to have a path longer than 2",fault);
        //     return false;
        // }

        //Only perform a corruption once we have finished priming
        //This reduces the number of potential faults but
        if (insert_messages.size() != 1 || _state == INSERTING){
            return false;
        }

        #ifndef ROW_CRC
        ALERT("ROW CRC", "dont run this code unless we are running row CRC (send_insert_crc_and_unlock_messages)");
        assert(false);
        #endif

        //There are 5 distinct fault scenarios
        assert(fault >=FAULT_CASE_0 && fault <= FAULT_CASE_4);
        INFO(log_id(), "Executing insert fault %d\n", fault);

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
            send_insert_and_crc(insert_messages[i], &sg[i*2], &wr[i*2], &_wr_id);
        }
        for ( unsigned int i=0; i < unlock_messages.size(); i++) {
            set_unlock_message(unlock_messages[i], &sg[i + insert_messages_with_crc], &wr[i + insert_messages_with_crc], &_wr_id);
        }

        string lock_string = "[";
        for (int i=0;i<_locking_context.lock_indexes_size;i++) {
            lock_string += std::to_string(_locking_context.lock_indexes[i]);
            if (i < _locking_context.lock_indexes_size -1){
                lock_string += ",";
            }
        }
        lock_string += "]";

        if (insert_messages.size() > 1) {
            if (fault == FAULT_CASE_0 || fault == FAULT_CASE_4) {
                total_messages=0;
            }
            if (fault == FAULT_CASE_1) {
                total_messages=1;
            }
            if (fault == FAULT_CASE_2) {
                total_messages=2;
            }
            if (fault == FAULT_CASE_3) {
                total_messages=3;
            }
        } else if (insert_messages.size() == 1) {
            if (fault == FAULT_CASE_0) {
                total_messages=0;
            }
            if (fault == FAULT_CASE_3) {
                total_messages=1;
            }
        } else {
            //we can only generate case 0
            ALERT("FAILURE FAULTING", "we should not be inserting with path length 0 HOW?");
            exit(0);
        }

        _faults_injected++;

        //Send out the Failed request
        if (total_messages > 0) { 
            set_signal(&wr[total_messages-1]);
            send_bulk(total_messages, _qp, wr);
            bulk_poll(_completion_queue,1,_wc);
        }

        string bucket_string = "[";
        for (int i=0;i<total_messages;i++) {
            bucket_string += std::to_string(_search_context.path[i].bucket_index);
            if (i < _search_context.path.size() -1){
                bucket_string += ",";
            }
        }
        bucket_string += "]";

        ALERT(log_id(), "Fault #%d injected on locks %s and buckets %s %s. Sleeping for %d %ss", fault, lock_string.c_str(), bucket_string.c_str(), "\U0001F608",max_fault_rate_us, "\u00b5");
        usleep(max_fault_rate_us);
        return true;
    }


    void RCuckoo::send_insert_crc_and_unlock_messages(vector<VRCasData> &insert_messages, vector<VRMaskedCasData> & unlock_messages) {

        struct ibv_sge sg [MAX_INSERT_CRC_AND_UNLOCK_MESSAGE_COUNT];
        struct ibv_exp_send_wr wr [MAX_INSERT_CRC_AND_UNLOCK_MESSAGE_COUNT];

        #ifndef ROW_CRC
        ALERT("ROW CRC", "dont run this code unless we are running row CRC (send_insert_crc_and_unlock_messages)");
        assert(false);
        #endif


        int fault = -1;
        if (_simulate_failures) {
            if (_id == 0 ){
                if (insert_messages.size() > 1){
                    fault = rand()%FAULT_CASE_4;
                } else if (insert_messages.size() == 1) {
                    //we can only generate case 0, 3
                    fault = rand()%2;
                    if (fault == 0) {
                        fault = FAULT_CASE_0;
                    } else {
                        fault = FAULT_CASE_3;
                    }
                } 
                if (fault >= 0) {
                    bool injected_fault = send_insert_crc_and_unlock_messages_with_fault(insert_messages,unlock_messages,fault,_delay_between_failures_us);
                    if (injected_fault) {
                        return;
                    }
                }

            }
        }

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
        // ALERT(log_id(), "sending a total of %d insert messages and %d unlock messages\n", insert_messages.size(), unlock_messages.size());
        for ( unsigned int i=0; i < insert_messages.size(); i++) {
            // ALERT("ROW CRC", "sending insert message %d %d %lu\n", insert_messages[i].row, insert_messages[i].offset, insert_messages[i].new_value);
            send_insert_and_crc(insert_messages[i], &sg[i*2], &wr[i*2], &_wr_id);
        }
        for ( unsigned int i=0; i < unlock_messages.size(); i++) {
            set_unlock_message(unlock_messages[i], &sg[i + insert_messages_with_crc], &wr[i + insert_messages_with_crc], &_wr_id);
            // ALERT("ROW CRC", "sending unlock message %d %lX %lX\n", unlock_messages[i].min_lock_index, unlock_messages[i].old, unlock_messages[i].new_value);
        }
        wr[insert_messages_with_crc].exp_send_flags |= IBV_EXP_SEND_FENCE;
        set_signal(&wr[total_messages-1]);
        //Send and receive messages
        send_bulk(total_messages, _qp, wr);
        bulk_poll(_completion_queue, 1, _wc);
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

    void set_retry_counter(unsigned int * retries, uint64_t set_locks) {
        uint64_t one = 1;
        for (uint64_t i=0;i<BITS_IN_MASKED_CAS;i++){
            bool set = (set_locks & (one << i));
            if (set) {
                retries[i]++;
            } else {
                retries[i]=0;
            }
        }
    }

    //return a single bit for the lock we need to get
    uint64_t retry_crossed_threshold(unsigned int * retries, unsigned int threshold){
        uint64_t one = 1;
        for (uint64_t i=0;i<BITS_IN_MASKED_CAS;i++){
            if (retries[i] > threshold) {
                return (one << i);
            }
        }
        return 0;
    }

    void clear_retry_counter(unsigned int * retries) {
        for (int i=0;i<BITS_IN_MASKED_CAS;i++){
            retries[i]=0;
        }
    }


    //Returns the ID of the repair lock
    bool RCuckoo::aquire_repair_lease(unsigned int lock) {
        Repair_Lease * lease = (Repair_Lease *) _table.get_repair_lease_pointer(0);

        //Before anything else we want to just read the pointer//
        //After we read the pointer we can decide what we need to do to set it.

        struct ibv_sge sg;
        struct ibv_exp_send_wr wr;

        uint64_t local_address = (uint64_t) lease;
        uint64_t remote_server_address = local_to_remote_repair_lease_address(local_address);

        //Covering Read
        setRdmaReadExp(
            &sg,
            &wr,
            local_address,
            remote_server_address,
            sizeof(Repair_Lease),
            _repair_lease_mr->lkey,
            _table_config->lease_table_key,
            true,
            _wr_id
        );
        _wr_id++;
        send_bulk(1, _qp, &wr);
        int n = bulk_poll(_completion_queue, 1, _wc);
        INFO(log_id(), "Attempted to grab lease, current lease %s\n", lease->to_string().c_str());

        //The repair lease is currently locked return a failure
        if(lease->islocked()) {
            return false;
        }

        uint64_t compare = *((uint64_t *)lease);
        lease->counter++;
        lease->Lock();
        uint64_t swap = *((uint64_t *)lease);

        rdmaCompareAndSwapExp(
            _qp,
            local_address,
            remote_server_address,
            compare,
            swap,
            _repair_lease_mr->lkey,
            _table_config->lease_table_key,
            true,
            _wr_id
        );
        _wr_id++;
        bulk_poll(_completion_queue, 1, _wc);
        uint64_t check = *((uint64_t *)lease);

        if (check == compare) {
            return true;
        } else {
            return false;
        }
    }

    int RCuckoo::release_repair_lease(unsigned int lease_id){

        Repair_Lease * lease = (Repair_Lease *) _table.get_repair_lease_pointer(0);
        //Before anything else we want to just read the pointer//
        //After we read the pointer we can decide what we need to do to set it.

        struct ibv_sge sg;
        struct ibv_exp_send_wr wr;

        uint64_t local_address = (uint64_t) lease;
        uint64_t remote_server_address = local_to_remote_repair_lease_address(local_address);

        lease->counter++;
        lease->Lock();
        uint64_t compare = *((uint64_t *)lease);
        lease->counter++;
        lease->Unlock();
        uint64_t swap = *((uint64_t *)lease);

        rdmaCompareAndSwapExp(
            _qp,
            local_address,
            remote_server_address,
            compare,
            swap,
            _repair_lease_mr->lkey,
            _table_config->lease_table_key,
            true,
            _wr_id
        );
        _wr_id++;
        bulk_poll(_completion_queue, 1, _wc);
        uint64_t check = *((uint64_t *)lease);

        if (check != compare) {
            ALERT(log_id(), "We did not successfuly release the lease. Serious error CRASHING...");
            exit(0);
        }

        //Set RDMA Cas with the same value
        return 0;
    }

    int RCuckoo::get_broken_row(Entry broken_entry) {
        hash_locations hloc = _location_function(broken_entry.key,_table.get_row_count());
        int broken_row;
        if (!_table.crc_valid_row(hloc.primary)) {
            broken_row = hloc.primary;
        } else if (!_table.crc_valid_row(hloc.secondary)){
            broken_row = hloc.secondary;
        } else {

            ALERT(log_id(), "Checking row %d %s",hloc.primary,_table.row_to_string(hloc.primary).c_str());
            ALERT(log_id(), "Checking row %d %s",hloc.secondary, _table.row_to_string(hloc.secondary).c_str());
            ALERT(log_id(), "We are attemptying to repair, however neither of the rows has a bad CRC crashing...");
            exit(0);
        }
        return broken_row;
    }

    void RCuckoo::repair_table(Entry broken_entry, unsigned int error_state){
        #define MAX_REPAIR_MESSAGES 3
        struct ibv_sge sg [MAX_REPAIR_MESSAGES];
        struct ibv_exp_send_wr wr [MAX_REPAIR_MESSAGES];
        int message_count=0;

        //TODO
        //If we want to support parallel repair we need to obtain potentially more than one lease.
        //In the case where we need more than one lease we have to get them in order and be careful with how long
        //we are going to hold them for.
        //If we use multiple repair leases we should only hold them for long enough to verify and send messages
        //If we do this we should move getting the leases to here.
        //Once a lease is obtained we need to go and read the table again and ensure that the state
        //behind the entries which we want to modify has not changed.
        //If the state has not changed then we are good to repair.
        //The reason we need to check is that if we defer repair untill this point another process may
        //have repaired the table in the meantime.

        if (error_state == FAULT_CASE_0 || error_state == FAULT_CASE_4) {
            INFO(log_id(), "No table repairs needed case %d and %d only require unlock\n", FAULT_CASE_0, FAULT_CASE_4);
            return;
        }

        if (error_state == FAULT_CASE_1) {
            INFO(log_id(), "Transition from Fault 1 -> 2\n");
            //To transition from fault case 1 to fault case 2 we need to write a new CRC to the bucket of the broken entry
            int broken_row = get_broken_row(broken_entry);

            uint64_t step_1_repair_crc = _table.crc64_row(broken_row);
            Entry crc_entry;
            crc_entry.set_as_uint64_t(step_1_repair_crc);
            _table.set_entry(broken_row,_table.get_entries_per_row(), crc_entry);
            uint64_t crc_address = (uint64_t) get_entry_pointer(broken_row, _table.get_entries_per_row());
            uint64_t remote_server_crc_address = local_to_remote_table_address(crc_address);
            // printf("crc_address %lX remote_server_crc_address %lX\n", crc_address, remote_server_crc_address);
            setRdmaWriteExp(
                &sg[message_count],
                &wr[message_count],
                crc_address,
                remote_server_crc_address,
                sizeof(uint64_t),
                _table_mr->lkey,
                _table_config->remote_key,
                -1,
                false,
                _wr_id
            );
            _wr_id++;
            message_count++;
            error_state = FAULT_CASE_2;
        }
        
        if (error_state == FAULT_CASE_2) {
            INFO(log_id(), "Transition from Fault 2 -> 3\n");
            hash_locations hloc = _location_function(broken_entry.key,_table.get_row_count());
            unsigned int delete_row = hloc.secondary;
            unsigned int entry_location = _table.get_keys_offset_in_row(delete_row,broken_entry.key);
            Entry zero_entry;
            zero_entry.zero_out();
            //Its important that we set the entry so that we reach FAULT_CASE_3 locally as well
            _table.set_entry(delete_row,entry_location,zero_entry);
            uint64_t zero_entry_location = (uint64_t) get_entry_pointer(delete_row,entry_location);
            uint64_t remote_server_address = local_to_remote_table_address(zero_entry_location);
            setRdmaWriteExp(
                &sg[message_count],
                &wr[message_count],
                zero_entry_location,
                remote_server_address,
                sizeof(Entry),
                _table_mr->lkey,
                _table_config->remote_key,
                -1,
                false,
                _wr_id
            );
            _wr_id++;
            message_count++;
            error_state = FAULT_CASE_3;
        }

        if (error_state == FAULT_CASE_3) {
            //TODO basically the same as fault case 1. We could dedup some of this code.
            INFO(log_id(), "Transition from Fault 3 -> 4\n");
            int broken_row = get_broken_row(broken_entry);
            uint64_t step_3_repair_crc = _table.crc64_row(broken_row);
            Entry crc_entry;
            crc_entry.set_as_uint64_t(step_3_repair_crc);
            _table.set_entry(broken_row,_table.get_entries_per_row(), crc_entry);
            uint64_t crc_address = (uint64_t) get_entry_pointer(broken_row, _table.get_entries_per_row());
            uint64_t remote_server_crc_address = local_to_remote_table_address(crc_address);
            // printf("crc_address %lX remote_server_crc_address %lX\n", crc_address, remote_server_crc_address);
            setRdmaWriteExp(
                &sg[message_count],
                &wr[message_count],
                crc_address,
                remote_server_crc_address,
                sizeof(uint64_t),
                _table_mr->lkey,
                _table_config->remote_key,
                -1,
                false,
                _wr_id
            );
            _wr_id++;
            message_count++;
        }

        //At this point we should have message buffers filled with the nessisary writes to repair the table.
        set_signal(&wr[message_count-1]);
        send_bulk(message_count,_qp,wr);
        int n = bulk_poll(_completion_queue, 1, _wc);
        assert(n==1);

        //At this point we have repaired the remote table and can unlock one or more of the entries
        return;

    }

    void RCuckoo::reclaim_lock(unsigned int lock) {
        hash_locations dup_entry;
        unsigned int error_state = FAULT_CASE_0;


        //Step 0 is to determine which state we are in. In order to do so we need first determine if we have any duplicates.
        //We own the lock now so the first step is to get a covering read for the lock.
        LockingContext repair_context = copy_context(_locking_context);
        vector<unsigned int> lock_arr;
        lock_arr.push_back(lock);
        lock_indexes_to_buckets_context(repair_context.buckets,lock_arr,repair_context);
        vector<VRReadData> reads;

        for (int i=0;i < repair_context.buckets.size(); i++){
            VRReadData read;
            read.row = repair_context.buckets[i];
            read.size = _table.row_size_bytes();
            read.offset = 0;
            reads.push_back(read);
        }
        send_read(reads);
        int outstanding_messages = 1;
        int n =0;
        while (n < 1) {
            //We only signal a single read, so we should only get a single completion
            n = bulk_poll(_completion_queue, outstanding_messages - n, _wc + n);
        }

        //Step 1 now we need to walk through each of the buckets that we read and check each entry to detect if they have duplicates.
        bool bad_crc = false;
        vector<Entry> entries_to_check;
        for(int i=0;i<repair_context.buckets.size();i++){
            unsigned row = repair_context.buckets[i];
            INFO(log_id(), "Checking row %s",_table.row_to_string(row).c_str());
            //If this row has never been used, just keep moving.
            if (_table.bucket_is_empty(row)) {
                continue;
            }
            //If we find that the CRC is bad here, we know that we are in state 1 or 3
            if (!_table.crc_valid_row(row)) {
                INFO(log_id(), "Bucket %d is corrupted, in recovery state %d or %d",row, FAULT_CASE_1, FAULT_CASE_3);
                if(bad_crc) {
                    ALERT(log_id(), "We have found more than one corrupted CRC behind a lock. This is an unknown error condition. Crashing...");
                    ALERT(log_id(), "Failure Row %s",_table.row_to_string(row).c_str());
                    ALERT(log_id(), "For now we are not going to exit, we are going to leave a broken row so that I can take the measurement Jan 12 2023");
                    // exit(0);
                }
                bad_crc=true;
            }
            for (int j=0;j<_table.get_entries_per_row();j++){
                Entry e = _table.get_entry(row,j);
                if (!e.is_empty()){
                    entries_to_check.push_back(e);
                }
            }
        }

        //Step 2 go through each entry and perform a read to get duplicate information.
        bool found_duplicates = false;
        Entry broken_entry;
        for(int i=0;i<entries_to_check.size();i++){
            bad_crc = false;
            //TODO I can issue these in bulk and deduplicate read rows for better performance.
            //TODO I'm issuing one read at a time because it's a bit easier for me
            Entry e = entries_to_check[i];
            read_theshold_message(reads,_location_function,e.key,0,_table.get_row_count(), _table.get_buckets_per_row());
            INFO(log_id(), "Checking for duplicate of %s in buckets %d, %d\n",e.key.to_string().c_str(), reads[0].row, reads[1].row);

            send_read(reads);
            int outstanding_messages = reads.size();
            int n =0;
            while (n < 1) {
                //We only signal a single read, so we should only get a single completion
                n = bulk_poll(_completion_queue, outstanding_messages - n, _wc + n);
            }
            dup_entry = _location_function(e.key,_table.get_row_count());

            INFO(log_id(), "Checking local table for duplicate of %s in buckets %d, %d\n",e.key.to_string().c_str(), dup_entry.primary, dup_entry.secondary);
            found_duplicates = _table.bucket_contains(dup_entry.primary,e.key) && _table.bucket_contains(dup_entry.secondary, e.key);
            if (found_duplicates) {
                INFO(log_id(), "Duplicates of %s found!",e.key.to_string().c_str());
                INFO(log_id(), "Checking for duplicates 1) row %s",_table.row_to_string(reads[0].row).c_str());
                INFO(log_id(), "Checking for duplicates 2) row %s",_table.row_to_string(reads[1].row).c_str());
                bad_crc |= (!_table.crc_valid_row(reads[0].row));
                bad_crc |= (!_table.crc_valid_row(reads[1].row));
            }
            //Now that I'm here I can check for duplicates

            //Here we attempt to detect the error
            if(found_duplicates && bad_crc){
                broken_entry = entries_to_check[i];
                error_state = FAULT_CASE_1;
                break;
            } else if (found_duplicates) {
                broken_entry = entries_to_check[i];
                error_state = FAULT_CASE_2;
                break;
            } else if (bad_crc) {
                broken_entry = entries_to_check[i];
                error_state = FAULT_CASE_3;
                break;
            }
        }

        //At this point we have the error code corretly determined
        WARNING(log_id(), "Error State %d detected on lock %d Begin Repair...",error_state, lock);
        repair_table(broken_entry,error_state);

        //Final step is to release the lock
        repair_context.clear_operation_state();
        if (error_state == FAULT_CASE_0 || error_state == FAULT_CASE_3 || error_state == FAULT_CASE_4) {
            unsigned int original_lock_bucket = lock * _buckets_per_lock;
            INFO(log_id(), "Unlocking single lock %d\n",original_lock_bucket);
            repair_context.buckets.push_back(original_lock_bucket);
        } else if (error_state == FAULT_CASE_1 || error_state == FAULT_CASE_2) {
            unsigned int first_bucket = dup_entry.min_bucket();
            unsigned int second_bucket = dup_entry.max_bucket();
            INFO(log_id(), "Unlocking multiple buckets %d and %d as part of one repair event\n", first_bucket, second_bucket);
            repair_context.buckets.push_back(first_bucket);
            repair_context.buckets.push_back(second_bucket);
        }

        get_unlock_list_fast_context(repair_context);

        #ifdef _LOG_ALERT
        string lock_string = "[";
        for (int i=0;i<repair_context.lock_indexes_size;i++){
            lock_string += std::to_string(repair_context.lock_indexes[i]);
            if(i < repair_context.lock_indexes_size -1) {
                lock_string += ",";
            }
        }
        lock_string += "]";
        // ALERT(log_id(), "😇 Repaired Locks %s from failures case %d", lock_string.c_str(), error_state);
        #endif

        vector<VRCasData> insert_messages;
        send_insert_crc_and_unlock_messages(insert_messages, repair_context.lock_list);

    }

    bool RCuckoo::lock_was_aquired(VRMaskedCasData lock) {
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

            return (received_locks & mask) == old_value;
    }

    unsigned int RCuckoo::get_reclaim_lock(VRMaskedCasData lock, uint64_t timed_out_lock) {
            unsigned int locks[BITS_IN_MASKED_CAS];
            VRMaskedCasData lock_to_unlock = lock;
            lock_to_unlock.mask = timed_out_lock;
            lock_to_unlock.old = timed_out_lock;
            unsigned int total_locks = lock_message_to_lock_indexes(lock_to_unlock,locks);
            assert(total_locks == 1);
            return locks[0];
    }



    //Precondition _locking_context.buckets contains the locks we want to get
    bool RCuckoo::top_level_aquire_locks(bool covering_reads) {
        get_lock_list_fast_context(_locking_context);
        INFO(log_id(), "[aquire_locks] gathering locks for buckets %s\n", vector_to_string(_locking_context.buckets).c_str());

        if (covering_reads) {
            get_covering_reads_context(_locking_context, _covering_reads, _table, _buckets_per_lock);
        } else {
            get_covering_reads_for_update(_locking_context, _covering_reads, _table, _buckets_per_lock);
        }

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
        unsigned int retries[BITS_IN_MASKED_CAS];
        unsigned int retries_untill_timeout = _lock_timeout_us / MEAN_RTT_US;
        unsigned int retries_untill_lease_timeout = _lease_timeout_us / MEAN_RTT_US;

        clear_retry_counter(retries);
        while (!locking_complete) {

            //TODO we need to make sure we have not crossed the timeout limit here.
            assert(message_index < _locking_context.lock_list.size());

            VRMaskedCasData lock = _locking_context.lock_list[message_index];
            // VRReadData read = _covering_reads[message_index][0];

            //This is for testing the benifit of locks only
            if (!_use_mask) {
                lock.mask = 0xFFFFFFFFFFFFFFFF;
            }

            send_lock_and_cover_message(lock, _covering_reads[message_index]);

            if (lock_was_aquired(lock)) {
                receive_successful_locking_message(message_index);
                message_index++;
                failed_last_request=false;
                clear_retry_counter(retries);
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
                // ALERT(log_id(),"Failed to get the lock %s\n", lock.to_string().c_str(), mask);
                // ALERT(log_id(),"unable to set %lX\n", mask & received_locks);
                // for (int i=0;i<total_locks;i++) {
                //     ALERT(log_id(), "Unable to aquire lock %d\n",locks[i]);
                // }
                uint64_t received_locks = *((uint64_t*) get_lock_pointer(lock.min_lock_index));
                set_retry_counter(retries, lock.mask & received_locks);

                uint64_t timed_out_lock = retry_crossed_threshold(retries,retries_untill_timeout);
                if (timed_out_lock > 0) {
                    clear_retry_counter(retries);

                    unsigned int lock_we_will_reclaim = get_reclaim_lock(lock, timed_out_lock);
                    ALERT(log_id(), "Timed out on lock %d", lock_we_will_reclaim)
                    INFO(log_id(), "Timeout while Inserting %s. Begin reclaim, while holding %d locks", _current_insert_key.to_string().c_str(), message_index);
                    bool aquired=false;
                    int lease_retries=0;
                    while(!aquired && lease_retries < retries_untill_lease_timeout) {
                        aquired=aquire_repair_lease(lock_we_will_reclaim);
                        lease_retries++;
                    }
                    if (aquired) {
                        SUCCESS(log_id(), "Lease Aquired");
                        reclaim_lock(lock_we_will_reclaim);
                        release_repair_lease(lock_we_will_reclaim);
                        SUCCESS(log_id(), "Lease Released");
                    } else {
                        ALERT(log_id(), "Unable to aquire lease. Restart timeout on lock %d", lock_we_will_reclaim);
                    }

                }
            }

            if (_locking_context.lock_list.size() == message_index) {
                locking_complete = true;
                INFO(log_id(), " [put-direct] we got all the locks!\n");
                break;
            }
        }
        int bad_row = -1;
        // int bad_row = _table.crc_valid();
        if (bad_row >= 0) {
            ALERT(log_id(), "we have a bad row %d\n", bad_row);
            _table.print_table();
            exit(1);
        }
        // _table.print_table();
        return true;

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

        bool inserted = false;
        if (_state == INSERTING) {
            // ALERT("insert direct", "inserting key %s\n", _current_insert_key.to_string().c_str());
            // ALERT("insert path local", "inserting key %s\n", path_to_string(_search_context.path).c_str());
            inserted = insert_cuckoo_path_local(_table, _search_context.path);
            //TODO remove
            // int bad_row = _table.crc_valid();
            // if (bad_row >= 0) {
            //     ALERT(log_id(),"BAD ROW FOUND bad row = %d\n",bad_row);
            //     ALERT(log_id(),"BAD ROW crc should be %lX, is %lX", _table.crc64_row(bad_row), _table.get_entry(bad_row, _table.get_entries_per_row()).get_as_uint64_t());
            //     ALERT(log_id(), "BAD ROW %s", _table.row_to_string(bad_row).c_str());
            //     ALERT(log_id(), "Currently holding %d locks", _locking_context.lock_indexes_size);
            //     for(int i=0;i<_locking_context.lock_indexes_size;i++){
            //         ALERT(log_id(), "Failing while holding lock %d\n",_locking_context.lock_indexes[i]);
            //     }
            //     _table.print_table();
            //     exit(1);
            // }

        }

        #ifdef ROW_CRC
        send_insert_crc_and_unlock_messages(_insert_messages, _locking_context.lock_list);
        #else
        send_insert_and_unlock_messages(_insert_messages, _locking_context.lock_list, _wr_id);
        bulk_poll(_completion_queue, 1, _wc + 1);
        #endif

        _locks_held.clear();

        if (_state == INSERTING) {
            // #define VALIDATE_INSERT
            // insert_cuckoo_path_local(_table, _search_context.path);
            //finish the insert by inserting the key into the local table.
            if (!inserted) {
                printf("fuck\n");
            }
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

        // hash_locations hl = _location_function(_current_insert_key,_table.get_row_count());
        // ALERT("DEBUG", "key %s hash locations %s\n", _current_insert_key.to_string().c_str(),hl.to_string().c_str());
        // ALERT("search", "Successful local search for [key %s] -> [path %s]\n", _current_insert_key.to_string().c_str(), path_to_string(_search_context.path).c_str());
        _state = AQUIRE_LOCKS;


        /* copied from aquire locks function */ 
        _locking_message_index = 0;

        

        search_path_to_buckets_fast(_search_context.path, _locking_context.buckets);
        VERBOSE("locally computed path", "key %s path %s\n", _current_insert_key.to_string().c_str(), path_to_string(_search_context.path).c_str());

        bool covering_reads = true;
        bool have_locks = top_level_aquire_locks(covering_reads);
        if(!have_locks){
            ALERT(log_id(), "Unable to aquire locks for key %s\n", _current_insert_key.to_string().c_str());
            exit(1);
        }

        return insert_direct();
    }

    void RCuckoo::get_direct(void) {
        read_theshold_message(_reads, _location_function, _current_read_key, _read_threshold_bytes, _table.get_row_count(), _table.get_buckets_per_row());
        send_read(_reads);

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
            WARNING("[get-direct]", "%2d did not find key %s \n", _id, _current_read_key.to_string().c_str());
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
        _locking_context.clear_operation_state();
        _locking_context.buckets.push_back(buckets.min_bucket());
        _locking_context.buckets.push_back(buckets.max_bucket());

        bool covering_reads = false;
        bool have_locks = top_level_aquire_locks(covering_reads);

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
        } 
        
        _insert_messages.clear();
        if (success) {
            VRCasData insert;
            insert.row = bucket;
            insert.offset = _table.get_keys_offset_in_row(bucket, _current_update_key);
            insert.new_value = 0xFFFFFFFFFFFFFFFF;
            _insert_messages.push_back(insert);
        } else {
            WARNING("[update-direct]", "%2d did not find key %s \n", _id, _current_update_key.to_string().c_str());
            if (_covering_reads.size() == 2){
                WARNING("[update-direct]", "primany,seconday [%d,%d] - read p[%d,%d]\n", buckets.primary, buckets.secondary, _covering_reads[0][0].row, _covering_reads[1][0].row);
            } else {
                WARNING("[update-direct]", "primany,seconday [%d,%d] - read p[%d,%d]\n", buckets.primary, buckets.secondary, _covering_reads[0][0].row, _covering_reads[0][1].row);
            }
        }

        //get offset

        _locking_message_index = 0;
        fill_current_unlock_list();

        INFO("update direct", "about to unlock a total of %d lock messages\n", _locking_context.lock_list.size());
        #ifdef ROW_CRC
        send_insert_crc_and_unlock_messages(_insert_messages, _locking_context.lock_list);
        #else
        send_insert_and_unlock_messages(_insert_messages, _locking_context.lock_list, _wr_id);
        #endif

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
