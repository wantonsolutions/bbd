#include <string>
#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <vector>
#include <any>
#include <fstream>

#include "state_machines.h"
#include "util.h"
#include "log.h"

#include "../cuckoo/tables.h"
#include "../cuckoo/virtual_rdma.h"

#include <iostream>



using namespace std;

template <typename T>
string array_to_string(vector<T> array) {
    string result = "";
    for (long unsigned int i = 0; i < array.size(); i++) {
        result += to_string(array[i]);
        if (i < array.size() - 1) {
            result += ",";
        }
    }
    return result;
}


string key_array_to_string(vector<Key> array) {
    string result = "";
    for (long unsigned int i = 0; i < array.size(); i++) {
        result += array[i].to_string();
        if (i < array.size() - 1) {
            result += ",";
        }
    }
    return result;
}
namespace state_machines {


    State_Machine::State_Machine() {
        _config = unordered_map<string,string>();
        clear_statistics();
    }

    State_Machine::State_Machine(unordered_map<string,string>config) {
        _config = config;
        clear_statistics();
    }


    void State_Machine::set_global_start_flag(volatile bool * flag) {
        assert(flag != NULL);
        // printf("cuckoo address global start flag %p\n", flag);
        _global_start_flag = flag;
        assert(_global_start_flag != NULL);
    }

    void State_Machine::set_global_end_flag(volatile bool * flag) {
        assert(flag != NULL);
        // printf("cuckoo address global stop flag %p\n", flag);
        _global_end_flag = flag;
        assert(_global_end_flag != NULL);
    }

    void State_Machine::set_global_prime_flag(volatile bool * flag) {
        assert(flag != NULL);
        // printf("cuckoo address global stop flag %p\n", flag);
        _global_prime_flag = flag;
        assert(_global_prime_flag != NULL);
    }

    void State_Machine::clear_statistics() {
        //clear control state
        _complete = false;
        _inserting = false;
        _reading = false;

        //clear counters (statistics)
        _total_bytes = 0;
        _read_bytes = 0;
        _write_bytes = 0;
        _cas_bytes = 0;
        _masked_cas_bytes=0;

        _total_reads = 0;
        _total_writes = 0;
        _total_cas = 0;

        _total_cas_failures = 0;
        _total_masked_cas = 0;
        _total_masked_cas_failures=0;

        _current_insert_key = Key();
        _current_update_key = Key();


        _insert_path_lengths = vector<int>();
        _index_range_per_insert = vector<int>();
        _current_insert_messages = 0;
        _messages_per_insert = vector<int>();
        _completed_inserts = vector<Key>();
        _completed_insert_count = 0;
        _failed_inserts_second_search = vector<int>();
        _failed_insert_second_search_count = 0;
        _insert_operation_bytes = 0;
        _insert_operation_messages = 0;


        _failed_insert_first_search_this_insert=0;
        _failed_insert_first_search_count=0;
        _failed_inserts_first_search = vector<int>();

        _failed_lock_aquisitions = vector<int>();
        _failed_lock_aquisition_this_insert = 0;

        _current_insert_rtt = 0;
        _insert_rtt = vector<int>();
        _insert_rtt_count = 0;

        _current_read_messages = 0;
        _messages_per_read = vector<int>();
        _completed_reads = vector<Key>();
        _completed_read_count = 0;
        _failed_reads = vector<Key>();
        _failed_read_count = 0;
        _read_operation_bytes = 0;
        _read_operation_messages = 0;
        _current_read_rtt =0;
        _read_rtt = vector<int>();
        _read_rtt_count = 0;


        // _operation_start_time; //not sure how to clear
        // _operation_end_time; //not sure how to clear
        _sum_insert_latency_ns = 0;
        _sum_read_latency_ns = 0;
        _insert_latency_ns = vector<int>();
        _read_latency_ns = vector<int>();

        _faults_injected = 0;
    }

    string State_Machine::get_state_machine_name() {
        return "State Machine Super Class";
    }

    bool State_Machine::is_complete(){
        return _complete;
    }

    vector<Key> State_Machine::get_completed_inserts() {
        return _completed_inserts;
    }

    void State_Machine::set_max_fill(float max_fill){
        printf("SET MAX FILL Overload this function (set value %f)", max_fill);
        throw logic_error("SET MAX FILL Overload this function");
    }

    void State_Machine::complete_read_stats(bool success, Key read_key){
        if (success) {
            #ifdef MEASURE_MOST
            _completed_reads.push_back(read_key);
            #endif
            #ifdef MEASURE_ESSENTIAL
            _completed_read_count++;
            #endif
        } else {
            #ifdef MEASURE_MOST
            _failed_reads.push_back(read_key);
            #endif
            #ifdef MEASURE_ESSENTIAL
            _failed_read_count++;
            #endif
        }
        read_key = read_key;
        uint64_t latency = (_operation_end_time - _operation_start_time).count();
        #ifdef MEASURE_MOST
        _messages_per_read.push_back(_current_read_messages);
        _read_rtt.push_back(_current_read_rtt);
        _read_latency_ns.push_back(latency);
        #endif

        #ifdef MEASURE_ESSENTIAL    
        _sum_read_latency_ns += latency;
        _read_rtt_count += _current_read_rtt;
        _current_read_messages = 0;
        _current_read_rtt = 0;
        #endif

    }
    void State_Machine::complete_insert_stats(bool success){
        //We should not have failed inserts
        assert(success == true);


        // if (success) {
        //     #ifdef MEASURE_MOST
        //     #endif
        //     #ifdef MEASURE_ESSENTIAL
        //     #endif
        // } else {
        //     ALERT("Failed insert", "EXITING");
        //     exit(1);
        // }
        uint64_t latency = (_operation_end_time - _operation_start_time).count();
        #ifdef MEASURE_MOST
        _completed_inserts.push_back(_current_insert_key);
        _failed_inserts_second_search.push_back(_failed_insert_second_search_this_insert);
        _failed_inserts_first_search.push_back(_failed_insert_first_search_this_insert);

        _failed_lock_aquisitions.push_back(_failed_lock_aquisition_this_insert);
        _messages_per_insert.push_back(_current_insert_messages);
        _insert_rtt.push_back(_current_insert_rtt);
        _insert_latency_ns.push_back(latency);
        #endif
        #ifdef MEASURE_ESSENTIAL
        _completed_insert_count++;
        #endif
        _current_insert_messages = 0;
        _current_insert_rtt = 0;
        _failed_insert_second_search_this_insert = 0;
        _failed_insert_first_search_this_insert = 0;
        _failed_lock_aquisition_this_insert=0;
        _sum_insert_latency_ns += latency;

    }

    void State_Machine::complete_update_stats(bool success) {
        success=success; //kill a compiler error and we might need this again
        uint64_t latency = (_operation_end_time - _operation_start_time).count();
        #ifdef MEASURE_MOST
        _completed_updates.push_back(_current_update_key);

        _failed_lock_aquisitions.push_back(_failed_lock_aquisition_this_insert);
        _messages_per_update.push_back(_current_update_messages);
        _update_rtt.push_back(_current_update_rtt);
        _update_latency_ns.push_back(latency);
        #endif
        #ifdef MEASURE_ESSENTIAL
        _completed_update_count++;
        #endif
        _current_update_messages = 0;
        _current_update_rtt = 0;
        _sum_update_latency_ns += latency;
    }
    unordered_map<string, string> State_Machine::get_stats(){
        unordered_map<string, string> stats;
        stats["total_bytes"] = to_string(_total_bytes);
        stats["read_bytes"] = to_string(_read_bytes);
        stats["write_bytes"] = to_string(_write_bytes);
        stats["cas_bytes"] = to_string(_cas_bytes);

        stats["total_reads"] = to_string(_total_reads);
        stats["total_writes"] = to_string(_total_writes);
        stats["total_cas"] = to_string(_total_cas);
        stats["total_cas_failures"] = to_string(_total_cas_failures);

        stats["insert_path_lengths"] = array_to_string(_insert_path_lengths);
        stats["index_range_per_insert"] = array_to_string(_index_range_per_insert);
        stats["messages_per_insert"] = array_to_string(_messages_per_insert);
        stats["completed_inserts"] = key_array_to_string(_completed_inserts);
        stats["completed_insert_count"] = to_string(_completed_insert_count);
        stats["failed_inserts_second_search"] = array_to_string(_failed_inserts_second_search);
        stats["failed_insert_second_search_count"] = to_string(_failed_insert_second_search_count);
        stats["failed_inserts_first_search"] = array_to_string(_failed_inserts_first_search);
        stats["failed_insert_first_search_count"] = to_string(_failed_insert_first_search_count);

        stats["insert_operation_bytes"] = to_string(_insert_operation_bytes);
        stats["insert_operation_messages"] = to_string(_insert_operation_messages);
        stats["failed_lock_aquisitions"] = array_to_string(_failed_lock_aquisitions);
        stats["failed_lock_aquisition_count"] = to_string(_failed_lock_aquisition_count);

        stats["insert_rtt"] = array_to_string(_insert_rtt);
        stats["insert_rtt_count"] = to_string(_insert_rtt_count);

        stats["messages_per_read"] = array_to_string(_messages_per_read);
        stats["completed_reads"] = key_array_to_string(_completed_reads);
        stats["completed_read_count"] = to_string(_completed_read_count);
        stats["failed_reads"] = key_array_to_string(_failed_reads);
        stats["failed_read_count"] = to_string(_failed_read_count);
        stats["read_operation_bytes"] = to_string(_read_operation_bytes);
        stats["read_operation_messages"] = to_string(_read_operation_messages);
        stats["read_rtt"] = array_to_string(_read_rtt);
        stats["read_rtt_count"] = to_string(_read_rtt_count);

        stats["messages_per_update"] = array_to_string(_messages_per_update);
        stats["completed_updates"] = key_array_to_string(_completed_updates);
        stats["completed_update_count"] = to_string(_completed_update_count);
        // stats["failed_updates"] = key_array_to_string(_failed_updates);
        // stats["failed_update_count"] = to_string(_failed_update_count);
        // stats["update_operation_bytes"] = to_string(_update_operation_bytes);
        // stats["update_operation_messages"] = to_string(_update_operation_messages);
        stats["update_rtt"] = array_to_string(_update_rtt);
        // stats["update_rtt_count"] = to_string(_update_rtt_count);

        stats["sum_insert_latency_ns"] = to_string(_sum_insert_latency_ns);
        stats["sum_read_latency_ns"] = to_string(_sum_read_latency_ns);
        stats["insert_latency_ns"] = array_to_string(_insert_latency_ns);
        stats["read_latency_ns"] = array_to_string(_read_latency_ns);

        stats["faults_injected"] = to_string(_faults_injected);
        
        return stats;
    }


    static const char *ycsb_workload_names[] = {"ycsb-a", "ycsb-b", "ycsb-c", "ycsb-w"};
    const char* get_ycsb_workload_name(ycsb_workload workload) {
        return ycsb_workload_names[workload];
    }

    Client_Workload_Driver::Client_Workload_Driver(){
        _total_requests = 0;
        _client_id = 0;
        _total_clients = 0;
        _deterministic = false;
        _random_factor = 0;
        _completed_requests = 0;
        _completed_puts = 0;
        _completed_gets = 0;
        _completed_updates = 0;
        _workload = A;
        _last_request = Request();
    }

    void Client_Workload_Driver::read_in_distribution_from_file(string filename){
        string filename_with_path = "workloads/"+filename;
        ifstream infile(filename_with_path);
        if(!infile.is_open()){
            ALERT("ERROR", "Could not open file %s\n", filename_with_path.c_str());
            exit(0);
        }
        try {
            string line;
            while (getline(infile, line)) {
                int key = stoi(line);
                // ALERT("DEBUG", "Read in key %d", key);
                key_distribution.push_back(key);
            }
        } catch (exception& e) {
            printf("ERROR: Client_Workload_Driver config missing required field :%s \n", e.what());
            infile.close();
            exit(0);
        }
        string id = to_string(_client_id);
        ALERT(filename.c_str(), "Read in %lu keys", key_distribution.size());
        infile.close();
    }

    Client_Workload_Driver::Client_Workload_Driver(unordered_map<string, string> config) {
        _completed_requests = 0;
        _completed_puts = 0;
        _completed_gets = 0;
        _completed_updates = 0;
        _last_request = Request();
        _time_seed = time(NULL);

        // for (auto it = config.begin(); it != config.end(); ++it) {
        //     printf("client workload driver : %s %s\n", it->first.c_str(), it->second.c_str());
        // }
        try{
            _total_requests = stoi(config["total_requests"]);
            _starting_id = stoi(config["starting_id"]);
            _client_id = stoi(config["id"]) + _starting_id;
            _total_clients = stoi(config["num_clients"]) * stoi(config["num_client_machines"]);
            _deterministic = config["deterministic"] == "True";
            set_workload(config["workload"]);
        } catch (exception& e) {
            printf("ERROR: Client_Workload_Driver config missing required field :%s \n", e.what());
            throw logic_error("ERROR: Client_Workload_Driver config missing required field");
        }

        // string workload_filename = config["workload_filename"];
        // string workload_filename = "zipf_50.txt";
        string workload_filename = "zipf_99.txt";
        // string workload_filename = "uniform.txt";
        read_in_distribution_from_file(workload_filename);
        
        if (_deterministic) {
            _random_factor = 1;
        } else {
            _random_factor = rand_r(&_time_seed) % 100;
        }
    }

    unordered_map<string,string> Client_Workload_Driver::get_stats() {
        unordered_map<string, string> stats;
        stats["completed_requests"] = to_string(_completed_requests);
        stats["completed_puts"] = to_string(_completed_puts);
        stats["completed_gets"] = to_string(_completed_gets);
        stats["completed_updates"] = to_string(_completed_updates);
        stats["workload"] = to_string(_workload);
        stats["total_requests"] = to_string(_total_requests);
        stats["client_id"] = to_string(_client_id);
        stats["starting_id"] = to_string(_starting_id);
        stats["total_clients"] = to_string(_total_clients);
        return stats;
    }

    void Client_Workload_Driver::set_workload(ycsb_workload workload) {
        _workload = workload;
    }

    void Client_Workload_Driver::set_workload(string workload) {
        INFO("Workload Driver", "setting workload to %s\n", workload.c_str());
        if (workload == "ycsb-a"){
            _workload = A;
        } else if (workload == "ycsb-b"){
            _workload = B;
        } else if (workload == "ycsb-c"){
            _workload = C;
        } else if (workload == "ycsb-w"){
            _workload = W;
        } else {
            ALERT("ERROR", "unknown workload\n");
            throw logic_error("ERROR: unknown workload");
        }
    }

    void Client_Workload_Driver::record_last_request() {
        if (_last_request.op == PUT) {
            _completed_puts++;
        } else if (_last_request.op == GET) {
            _completed_gets++;
        } else if (_last_request.op == UPDATE) {
            _completed_updates++;
        } else if (_last_request.op == DELETE) {
            ALERT("ERROR", "Delete not implemented\n");
            throw logic_error("ERROR: not implemented operation DELETE");
        } else {
            ALERT("Gonna crash", " on client %d, Request %s\n", _client_id, _last_request.to_string().c_str());
            ALERT("Gonna crash", " on client %d, Request value %d\n", _client_id, _last_request.op);
            ALERT("ERROR", "unknown operation\n");
            throw logic_error("ERROR: unknown operation");
        }
    }

    Key Client_Workload_Driver::unique_insert(int insert_index, int client_id, int total_clients, int factor) {
        uint64_t key_int = ((insert_index + 1) * total_clients * factor) + client_id;
        Key key;
        VERBOSE("DEBUG: unique_insert", "Calculated unique insert %lu -- params: insert_index: %d client_id: %d total_clients: %d factor: %d\n", key_int, insert_index, client_id, total_clients, factor);
        key.set(key_int);
        return key;
    }

    Key Client_Workload_Driver::unique_update(int get_index, int client_id, int total_clients, int factor) {
        uint64_t key_int = ((get_index + 1) * total_clients * factor) + client_id;
        Key key;
        key.set(key_int);
        return key;
    }

    Key Client_Workload_Driver::unique_get(int get_index, int client_id, int total_clients, int factor) {
        uint64_t key_int = ((get_index + 1) * total_clients * factor) + client_id;
        Key key;
        key.set(key_int);
        return key;
    }

    Key Client_Workload_Driver::direct_get(int get_index) {
        Key key;
        key.set(get_index);
        return key;
    }


    Request Client_Workload_Driver::next_put() {
        Key key = unique_insert(_completed_puts, _client_id, _total_clients, _random_factor);
        assert(_client_id < _total_clients);
        Value val = Value();
        Request req = Request{PUT, key, val};
        _last_request = req;
        return req;

    }
    const uint32_t BASE_KEY=1;

    Request Client_Workload_Driver::next_get() {
        uint32_t next_key_index;
        if (_deterministic){
            next_key_index =  BASE_KEY + (_completed_puts - 2);
        } else {
            if (_completed_puts <=1 ) {
                next_key_index = 0;
            } else {
                // next_key_index = rand_r(&_time_seed) % (_completed_puts - 1);
                next_key_index = key_distribution[rand_r(&_time_seed) % key_distribution.size()];
            }
        }
        // Key key = unique_get(next_key_index, _client_id, _global_clients, _random_factor);
        Key key = direct_get(next_key_index);
        // ALERT("DEBUG", "next get key %d\n", next_key_index);
        Request req = Request{GET, key, Value()};
        _last_request = req;
        return req;
    }

    Request Client_Workload_Driver::next_update() {
        uint32_t next_key_index;
        if (_deterministic){
            next_key_index =  BASE_KEY + (_completed_puts - 2);
        } else {
            if (_completed_puts <=1 ) {
                next_key_index = 0;
            } else {
                // next_key_index = rand_r(&_time_seed) % (_completed_puts - 1);
                next_key_index = key_distribution[rand_r(&_time_seed) % key_distribution.size()];
            }
        }
        // Key key = unique_get(next_key_index, _client_id, _global_clients, _random_factor);
        // if (next_key_index == 1){
        //     ALERT("1","1");
        // }
        Key key = direct_get(next_key_index);
        Request req = Request{UPDATE, key, Value()};
        _last_request = req;
        return req;
    }

    operation Client_Workload_Driver::gen_next_operation() {
        if (_workload == A) {
            return (rand_r(&_time_seed) % 100) < 50 ? GET : UPDATE;
        } else if (_workload == B) {
            return (rand_r(&_time_seed) % 100) < 95 ? GET : UPDATE;
        } else if (_workload == C) {
            return GET;
        } else if (_workload == W) {
            return PUT;
        } else {
            ALERT("ERROR", "unknown workload\n");
            throw logic_error("ERROR: unknown workload");
        }
    }

    Request Client_Workload_Driver::next() {
        record_last_request();
        if (_completed_requests >= _total_requests) {
            return Request{NO_OP, Key(), Value()};
        }
        if (_completed_puts == 0) {
            return next_put();
        }
        operation op = gen_next_operation();
        if (op == PUT) {
            return next_put();
        } else if (op == GET) {
            return next_get();
        } else if (op == UPDATE) {
            return next_update();
        } else {
            printf("ERROR: unknown operation\n");
            throw logic_error("ERROR: unknown operation");
        }
    }

    static const char *client_state_names[] = {"idle", "reading"};
    const char* get_client_state_name(client_state state) {
        return client_state_names[state];
    }

    Client_State_Machine::Client_State_Machine() : State_Machine() {
        // ALERT("TODO","client state machine constructor with no argument\n");
        // exit(1);
    }

    Client_State_Machine::Client_State_Machine(unordered_map<string,string> config) : State_Machine(config) {
        _config = config;
        try{
            INFO("CSM boot", "Client_State_Machine config:");
            for (auto i : config){
                INFO("CSM boot", "%s \t\t\t %s", i.first.c_str(), i.second.c_str());
            }
            _total_inserts = stoi(config["total_inserts"]);
            _id = stoi(config["id"]);
            _max_fill = stoi(config["max_fill"]);
            _state = IDLE;

            _current_read_key = Key();
            _outstanding_read_requests = 0;
            _read_values_found = 0;
            _read_values = vector<Key>();
            _duplicates_found = 0;
            _workload = config["workload"];
            _workload_driver = Client_Workload_Driver(config);
        } catch (exception& e) {
            ALERT("ERROR", "Client_State_Machine config missing required field\n");
            throw logic_error("ERROR: Client_State_Machine config missing required field");
        }
    }

    void Client_State_Machine::set_max_fill(int max_fill) {
        _max_fill = max_fill;
        return;
    }

    void Client_State_Machine::clear_statistics() {
        _state=IDLE;
        _current_read_key = Key();
        _outstanding_read_requests = 0;
        _read_values_found = 0;
        _read_values = vector<Key>();
        _duplicates_found = 0;
        // _workload_driver.clear_statistics();
        State_Machine::clear_statistics();
        return;
    }

    void Client_State_Machine::set_workload(ycsb_workload workload) {
        _workload_driver.set_workload(workload);
        _config["workload"] = get_ycsb_workload_name(workload);
        return;
    }


    bool Client_State_Machine::read_complete() {
        return _outstanding_read_requests == 0;
    }

    bool Client_State_Machine::read_successful(Key key) {
        bool success = false;
        key=key; // kill a compiler error when we have non verbose output
        if (_read_values_found == 0) {
            success = false;
            VERBOSE("read success", "Read incomplete (key: %s)", key.to_string().c_str());
        } else if (_read_values_found == 1) {
            success = true;
            VERBOSE("read success", "Read success (key: %s)", key.to_string().c_str());
        } else {
            success = false;
            _duplicates_found += (_read_values_found -1);
            VERBOSE("read success", "Read Success (key: %s) but duplicate found\n", key.to_string().c_str());
        }
        return success;
    }


    unordered_map<string, string> Client_State_Machine::get_stats(){
        unordered_map<string, string> stats = State_Machine::get_stats();
        unordered_map<string, string> workload_stats = _workload_driver.get_stats();
        stats.insert(workload_stats.begin(), workload_stats.end());
        return stats;
    }

    string Client_State_Machine::get_state_machine_name() {
        return "Client State Machine Super Class";
    }

    //Memory State Machine implementation
    Memory_State_Machine::Memory_State_Machine() : State_Machine() {
        _table = Table();
        _max_fill = 0;
    }

    Memory_State_Machine::Memory_State_Machine(unordered_map<string,string> config) : State_Machine(config) {
        try {
            
            unsigned int memory_size = stoi(config["memory_size"]);
            unsigned int bucket_size = stoi(config["bucket_size"]);
            unsigned int buckets_per_lock = stoi(config["buckets_per_lock"]);
            INFO("Creating Table", "Table_size: %d, bucket_size %d, buckets_per_lock %d\n", memory_size, bucket_size, buckets_per_lock);
            _table = Table(memory_size, bucket_size, buckets_per_lock);
            _max_fill = stoi(config["max_fill"]);
            _prime_fill = stoi(config["prime_fill"]);
            if (_max_fill < _prime_fill) {
                ALERT("ERROR memory state machine init", "max_fill must be greater than or equal to prime_fill\n");
                throw logic_error("ERROR: max_fill must be greater than or equal to prime_fill");
            }
        } catch (exception& e) {
            ALERT("ERROR memory state machine init", "Memory_State_Machine config missing required field\n");
            throw logic_error("ERROR: Memory_State_Machine config missing required field");
        }
    }

    void Memory_State_Machine::set_max_fill(int max_fill) {
        _max_fill = max_fill;
        return;
    }

    void Memory_State_Machine::set_prime_fill(int prime_fill) {
        _prime_fill = prime_fill;
        return;
    }


    void insert_cuckoo_path(Table &table, vector<path_element> path) {
        assert(path.size() >= 2);
        for (int i=path.size()-2; i >=0; i--){
            Entry e;
            e.key = path[i+1].key;
            table.set_entry(path[i].bucket_index, path[i].offset, e);
        }
    }

    void fill_key(Key &key, int value) {
        uint8_t *vp = (uint8_t*)&value;
        key.bytes[0] = vp[0];
        key.bytes[1] = vp[1];
        key.bytes[2] = vp[2];
        key.bytes[3] = vp[3];
    } 

    int Memory_State_Machine::crc_table() {
        return _table.crc_valid();
    }


    bool Memory_State_Machine::contains_duplicates(){
        return _table.contains_duplicates();
    }

    vector<Duplicate_Entry> Memory_State_Machine::get_duplicates(){
        return _table.get_duplicates();
    }

    bool Memory_State_Machine::contains(Key key){
        return _table.contains(key);
    }

    float Memory_State_Machine::get_fill_percentage(){
        return _table.get_fill_percentage();
    }

    float Memory_State_Machine::get_max_fill() {
        return _max_fill;
    }

    int Memory_State_Machine::get_prime_fill() {
        return _prime_fill;
    } 

    void Memory_State_Machine::print_table(){
        _table.print_table();
    }

    void Memory_State_Machine::print_lock_table(){
        _table.print_lock_table();
    }

    unsigned int Memory_State_Machine::get_table_size(){
        return _table.get_table_size_bytes();
    }

    Entry ** Memory_State_Machine::get_table_pointer(){
        return _table.get_underlying_table();
    }

    void Memory_State_Machine::set_table_pointer(Entry ** table){
        _table.set_underlying_table(table);
    }

    Table * Memory_State_Machine::get_table(){
        return &_table;
    }



    void * Memory_State_Machine::get_underlying_lock_table_address(){
        return _table.get_underlying_lock_table_address();

    }
    unsigned int Memory_State_Machine::get_underlying_lock_table_size_bytes(){
        return _table.get_underlying_lock_table_size_bytes();

    }
    void Memory_State_Machine::set_underlying_lock_table_address(void * address){
        _table.set_underlying_lock_table_address(address);
    }
}