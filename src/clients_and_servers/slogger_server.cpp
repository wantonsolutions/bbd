/*
 * This is a RDMA server side code. 
 *
 * Author: Animesh Trivedi 
 *         atrivedi@apache.org 
 *
 * TODO: Cleanup previously allocated resources in case of an error condition
 */

#include <unordered_map>

#include "../rdma/rdma_common.h"
#include "../rdma/rdma_server_lib.h"
#include "../slib/memcached.h"
#include "../slib/config.h"
#include "../slib/log.h"
#include "../slogger/replicated_log.h"

using namespace std;
using namespace slogger;

enum slog_mr_index {
    LOG_MR_INDEX,
    CLIENT_POSITION_TABLE_MR_INDEX,
    SCRATCH_MEMORY_MR_INDEX,
    MR_PER_SLOG_COUNT, //make sure this is the last one we want to know how many we have

};
#define DEVICE_MEMORY_STARTING_ADDRESS 0
#define DEVICE_MEMORY_TO_ALLOCATE 1024 * 8 //1MB
#define TAIL_POINTER_STARTING_ADDRESS DEVICE_MEMORY_STARTING_ADDRESS

static on_chip_memory_attr device_memory;

int get_abosolute_mr_index(int index, int slog_id){
    return slog_id * MR_PER_SLOG_COUNT + index;
}
int get_absolute_tail_pointer_starting_address(int slog_id){
    return TAIL_POINTER_STARTING_ADDRESS + slog_id * sizeof(uint64_t); // This is the min allowable
    // return TAIL_POINTER_STARTING_ADDRESS + slog_id * GAP_BETWEEN_TAIL_POINTERS;
}

void copy_tail_pointer_from_device_memory(Replicated_Log &rl) {
    copy_device_memory_to_host_object((void *)rl.get_tail_pointer_address(), rl.get_tail_pointer_size_bytes(), device_memory);
}


uint64_t get_epoch(Replicated_Log &rl ) {
    copy_tail_pointer_from_device_memory(rl);
    uint64_t tail_pointer = *((uint64_t*)rl.get_tail_pointer_address());
    uint64_t epoch = tail_pointer / rl.get_number_of_entries();
    return epoch;
}

static void send_final_memory_stats_to_memcached_server(Replicated_Log rl){
    memory_stats ms;
    ms.finished_run = true;

    ms.fill = get_epoch(rl);
    memcached_publish_memory_stats(&ms);
}

void wait_for_all_servers_to_connect() {
    while(true){
        experiment_control *ec = memcached_get_experiment_control();
        if(ec->is_experiment_running()){
            ALERT("RDMA Engine", "Experiment Starting Globally\n");
            break;
        }
    }
}


static void send_slog_config_to_memcached_server(Replicated_Log& rl, string name, int scratch_memory_size_bytes, int memory_server_index)
{

    //Regiserting the memory for the table
    //Send the info for the table
    ibv_mr * log_mr = register_server_object_at_mr_index(rl.get_log_pointer(), rl.get_log_size_bytes(), get_abosolute_mr_index(LOG_MR_INDEX, rl.get_id()));

    if (rl.get_id() == 0) {
        printf("registering %d bytes of device memory at addres %d", DEVICE_MEMORY_TO_ALLOCATE, DEVICE_MEMORY_STARTING_ADDRESS);
        device_memory = register_device_memory(DEVICE_MEMORY_STARTING_ADDRESS, DEVICE_MEMORY_TO_ALLOCATE);
    }

    ibv_mr * client_position_table_mr = register_server_object_at_mr_index(rl.get_client_positions_pointer(), rl.get_client_positions_size_bytes(), get_abosolute_mr_index(CLIENT_POSITION_TABLE_MR_INDEX, rl.get_id()));

    void * scratch_ptr = malloc(scratch_memory_size_bytes);
    ibv_mr * scratch_mr = register_server_object_at_mr_index(scratch_ptr, scratch_memory_size_bytes, get_abosolute_mr_index(SCRATCH_MEMORY_MR_INDEX, rl.get_id()));

    slog_config config;
    // Table * table = msm.get_table();
    config.slog_address = (uint64_t) log_mr->addr;
    config.slog_key = (uint32_t) log_mr->lkey;
    config.slog_size_bytes = rl.get_log_size_bytes();

    //This check makes sure that we are not reaching outside of device memory
    assert(get_absolute_tail_pointer_starting_address(rl.get_id()) + sizeof(uint64_t) < DEVICE_MEMORY_STARTING_ADDRESS + DEVICE_MEMORY_TO_ALLOCATE);

    config.tail_pointer_address = (uint64_t) get_absolute_tail_pointer_starting_address(rl.get_id());
    config.tail_pointer_key = (uint32_t) device_memory.mr->lkey;
    config.tail_pointer_size_bytes = rl.get_tail_pointer_size_bytes();

    config.client_position_table_address = (uint64_t) client_position_table_mr->addr;
    config.client_position_table_key = (uint32_t) client_position_table_mr->lkey;
    config.client_position_table_size_bytes = rl.get_client_positions_size_bytes();

    config.scratch_memory_address = (uint64_t) scratch_mr->addr;
    config.scratch_memory_key = (uint32_t) scratch_mr->lkey;
    config.scratch_memory_size_bytes = scratch_memory_size_bytes;


    printf("config: [%s] %s\n", name.c_str(), config.to_string().c_str());
    memcached_publish_slog_config(&config, name, memory_server_index);
}



void moniter_run(int print_frequency, bool prime, int runtime, bool use_runtime, int prime_epochs, int max_epochs, Replicated_Log& rl) {

    //print buffers every second

    ALERT("RUN MONITER", "Starting run moniter Runtime: %d, Prime Epochs: %d, Max Epochs: %d", runtime, prime_epochs, max_epochs);
    int print_step=0;
    bool priming_complete = false;
    time_t last_print;
    time_t experiment_start_time;
    time(&experiment_start_time);
    time(&last_print);
    while(true) {
        time_t now;
        time(&now);
        // ALERT("EPOCH", "epoch %ld", epoch);
        // rl.Chase_Tail_Pointer();
        uint64_t epoch = get_epoch(rl);

        // ALERT("EPOCH", "epoch %ld", epoch);
        if(now - last_print >= print_frequency) {
            last_print = now;
            unsigned int data_written_mb = (epoch * rl.get_log_size_bytes()) / 1000000;
            printf("[Runtime %d] %d MB written [epoch %ld] \n", print_step* print_frequency, data_written_mb, epoch);
            // rl.print_client_position_raw_hex();
            rl.print_client_positions();
            print_step++;
        }

        //TODO add priming critera
        if(prime && 
        !priming_complete &&
        (epoch >= (uint64_t)prime_epochs)
        ) {
            printf("Table has reached it's priming epoch %d (current epoch %ld)\n", prime_epochs, epoch);
            announce_priming_complete();
            priming_complete = true;
            if (use_runtime) {
                time(&experiment_start_time);
            }
        }

        //Max fill of zero means we are not using max fill
        if((max_epochs != 0) && (epoch >= (uint64_t)max_epochs)){
            printf("Table has reached it's max epoch %d (current fill %ld). Exiting globally\n", max_epochs, epoch);
            end_experiment_globally();
            break;
        }

        if(use_runtime && (now - experiment_start_time) >= runtime) {
            printf("Experiment has reached it's runtime capactiy. Exiting globally\n");
            end_experiment_globally();
            break;
        }
    }
    int sleep_time = 1;
    ALERT("Slogger Server", "Experiment has been ended globally sleeping for %d seconds to let clients wrap up\n", sleep_time);
    sleep(sleep_time);
    ALERT("Slogger Server", "Experiment over writing results to memcached");

}

void usage() 
{
    printf("Usage:\n");
    printf("./slogger_server <config_path>\n");
    exit(1);
}



int main(int argc, char **argv) 
{

    ALERT("Starting slogger server", "Starting server\n");

    if (argc > 2) {
        ALERT("parse error", "ERROR Too many arguemnts Usage: ./rdma_server <config_file>\n");
        exit(1);
    }
    string config_filename = "configs/slogger_default.json";
    if (argc == 2) {
        config_filename = argv[1];
    }

    unordered_map<string, string> config = read_config_from_file(config_filename);

    bool prime = (config["prime"] == "true");
    unsigned int prime_epochs = stoi(config["prime_epochs"]);
    unsigned int  max_epochs = stoi(config["max_epochs"]);

    if (!check_memory_server_config(config)) {
        ALERT("RDMA memory server", "ERROR: Invalid config file\n");
        exit(1);
    }
    vector<string> server_addresses = split(config["server_addresses"], ',');
    vector<string> base_ports = split(config["base_ports"], ',');
    int num_memory_servers = server_addresses.size();

    int server_index = get_memory_server_index(server_addresses);
    string server_address_string = server_addresses[server_index];
    string base_port_string = base_ports[server_index];

    ALERT("SUCCESS", "I am Memory server %d, with address %s and port %s\n", server_index, server_address_string.c_str(), base_port_string.c_str());

    //resolve the address from the config
    struct sockaddr_in server_sockaddr = server_address_to_socket_addr(server_address_string);
    printf("assigning base_port %s\n", base_port_string.c_str());
    int base_port = stoi(base_port_string);
    int num_qps = stoi(config["num_client_machines"]) * stoi(config["num_clients"]);

    int memory_size_bytes = stoi(config["memory_size"]);
    int scratch_memory_size_bytes = stoi(config["scratch_memory_size"]);
    int entry_size_bytes = stoi(config["entry_size"]);
    int bits_per_client_position = stoi(config["bits_per_client_position"]);
    int rl_id = 0;
    string slog_name = config["name"];
    Replicated_Log rl = Replicated_Log(memory_size_bytes, entry_size_bytes, num_qps, rl_id, bits_per_client_position);

    string workload = config["workload"];
    int runtime = stoi(config["runtime"]);
    bool use_runtime = true;


    int ret = setup_shared_resources();
    if (ret) { 
        rdma_error("Failed to setup shared resources, ret = %d \n", ret);
        return ret;
    }

    /* Accept connections from client QPs 
     * NOTE: In absence of separate thread for monitoring incoming connections and 
     * allocate resources, we need to spell out the exact protocol and go about it 
     * step-by-step, or the server or client may block indefinitely; changing order 
     * of things here without also making relevant changes in the client might not 
     * be wise!
     */

    ALERT("RDMA memory server", "RDMA server setting up distributed resources\n");
    // send_inital_memory_stats_to_memcached_server();
    if (server_index == 0) {
        send_inital_experiment_control_to_memcached_server(num_memory_servers);
        ALERT("RDMA memory server", "Zeroing Client Count\n");
        memcached_zero_slogger_client_count();
    }

    send_slog_config_to_memcached_server(rl, slog_name, scratch_memory_size_bytes, server_index);
    rl.set_client_id(1);
    send_slog_config_to_memcached_server(rl, slog_name, scratch_memory_size_bytes, server_index);

    multi_threaded_connection_setup(server_sockaddr, base_port, num_qps);
    start_distributed_experiment(server_index);
    wait_for_all_servers_to_connect();


    printf("All server setup complete, now serving memory requests\n");
    moniter_run(1 ,prime, runtime, use_runtime, prime_epochs, max_epochs,rl);


    // ALERT("RDMA memory server", "Sending results to the memcached server\n");
    ALERT("SLogger server", "TODO send the results to the memcached server");
    send_final_memory_stats_to_memcached_server(rl);



    ret = disconnect_and_cleanup(num_qps);
    if (ret) { 
        rdma_error("Failed to clean up resources properly, ret = %d \n", ret);
        return ret;
    }
    return 0;
}


