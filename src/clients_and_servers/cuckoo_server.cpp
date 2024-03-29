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
#include "../slib/state_machines.h"
#include "../slib/memcached.h"
#include "../slib/config.h"
#include "../slib/log.h"

using namespace std;
using namespace state_machines;

#define TABLE_MR_INDEX 0
#define LOCK_TABLE_MR_INDEX 1
#define LOCK_TABLE_STARTING_ADDRESS 0
#define LEASE_TABLE_MR_INDEX 2


static on_chip_memory_attr device_memory;


void copy_device_memory_to_host_lock_table(Memory_State_Machine &msm) {
    copy_device_memory_to_host_object((void *)msm.get_underlying_lock_table_address(), msm.get_underlying_lock_table_size_bytes(), device_memory);
}


static void send_inital_memory_stats_to_memcached_server(){
    memory_stats ms;
    ms.finished_run = false;
    ms.fill = 0.0;
    memcached_publish_memory_stats(&ms);
}

static void send_final_memory_stats_to_memcached_server(Memory_State_Machine &msm){
    memory_stats ms;
    ms.finished_run = true;
    ms.fill = msm.get_fill_percentage();
    memcached_publish_memory_stats(&ms);
}

static void send_table_config_to_memcached_server(Memory_State_Machine& msm)
{

    //Regiserting the memory for the table
    //Send the info for the table
    printf("Allocating table of size %d bytes\n", msm.get_table_size());
    void * table_ptr = msm.get_table_pointer()[0];
    ibv_mr * table_mr = register_server_object_at_mr_index(table_ptr, msm.get_table_size(), TABLE_MR_INDEX);


    printf("Allocating lease table %d bytes\n",msm.get_underlying_repair_lease_table_size_bytes());
    void * lease_table_ptr = msm.get_underlying_repair_lease_table_address();
    ibv_mr *lease_table_mr = register_server_object_at_mr_index(lease_table_ptr, msm.get_underlying_repair_lease_table_size_bytes(), LEASE_TABLE_MR_INDEX);

    //TODO map the lock table to device memory
    printf("allocing device memory for lock table\n");
    printf("asking for a table of size %d\n", (uint64_t) msm.get_underlying_lock_table_size_bytes());
    device_memory = register_device_memory(LOCK_TABLE_STARTING_ADDRESS, msm.get_underlying_lock_table_size_bytes());

    // msm.set_underlying_lock_table_address(server_qp_buffer_mr[LOCK_TABLE_MR_INDEX]->addr);
    printf("allocated a device memory buffer for the lock table\n");
    printf("Device Memory lkey %d\n", device_memory.mr->lkey);

    printf("Table Address %p registered address %p\n", table_ptr, table_mr->addr);
    printf("Lock Table Address %p registered address %p\n", msm.get_underlying_lock_table_address(), device_memory.mr->addr);
    printf("Sending Table Configuration to Memcached Server\n");
    table_config config;
    Table * table = msm.get_table();

    // config.table_address = (uint64_t) msm.get_table_pointer();
    config.table_address = (uint64_t) table_mr->addr;
    config.remote_key = (uint32_t) table_mr->lkey;
    printf("todo when you get back set up the remote key!!");
    config.table_size_bytes = table->get_table_size_bytes();
    config.num_rows = table->get_row_count();
    config.buckets_per_row = table->get_buckets_per_row();
    config.entry_size_bytes = table->get_entry_size_bytes();
    config.lock_table_address = (uint64_t) LOCK_TABLE_STARTING_ADDRESS;
    config.lock_table_size_bytes = table->get_underlying_lock_table_size_bytes();
    config.lock_table_key = (uint32_t) device_memory.mr->lkey;

    config.lease_table_address = (uint64_t) lease_table_mr->addr;
    config.lease_table_size_bytes = msm.get_underlying_repair_lease_table_size_bytes();
    config.lease_table_key = lease_table_mr->lkey;
    memcached_pubish_table_config(&config);
}



void moniter_run(int num_qps, int print_frequency, bool prime, int runtime, bool use_runtime, Memory_State_Machine &msm) {

    //print buffers every second
    int print_step=0;
    bool priming_complete = false;
    time_t last_print;
    time_t experiment_start_time;
    time(&last_print);
    while(true) {
        time_t now;
        time(&now);
        float fill_percentage = msm.get_fill_percentage();
        if(now - last_print >= print_frequency) {
            last_print = now;
            // printf("Printing table after %d seconds\n", print_step * print_frequency);
            // print_step++;
            // msm.print_table();
            // copy_device_memory_to_host_lock_table(msm);
            // msm.print_lock_table();
            printf("%2.3f/%2.3f Full\n", fill_percentage, msm.get_max_fill());
        }

        if(prime && 
        !priming_complete &&
        (fill_percentage * 100.0) >= msm.get_prime_fill()
        ) {
            printf("Table has reached it's priming factor\n");
            announce_priming_complete();
            priming_complete = true;
            if (use_runtime) {
                time(&experiment_start_time);
            }
        }

        if((fill_percentage * 100.0) >= msm.get_max_fill()) {
            printf("Table has reached it's full capactiy. Exiting globally\n");
            end_experiment_globally();
            break;
        }


        if(use_runtime && (now - experiment_start_time) >= runtime) {
            printf("Experiment has reached it's runtime capactiy. Exiting globally\n");
            end_experiment_globally();
            break;
        }
    }
    ALERT("RDMA memory server", "Experiment has been ended globally\n");
    ALERT("RDMA memory server", "Write the stats to the memcached server\n");

}

void usage() 
{
    printf("Usage:\n");
    printf("rdma_server: [-a <server_addr>] [-p <server_port>]\n");
    printf("(default port is %d)\n", DEFAULT_RDMA_PORT);
    exit(1);
}


int main(int argc, char **argv) 
{

    ALERT("Starting server", "Starting server\n");

    if (argc > 2) {
        ALERT("parse error", "ERROR Too many arguemnts Usage: ./rdma_server <config_file>\n");
        exit(1);
    }
    string config_filename = "configs/default_config.json";
    if (argc == 2) {
        config_filename = argv[1];
    }
    unordered_map<string, string> config = read_config_from_file(config_filename);
    Memory_State_Machine msm = Memory_State_Machine(config);

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
    int num_qps = stoi(config["num_clients"]);

    string workload = config["workload"];
    int runtime = stoi(config["runtime"]);
    bool use_runtime = true;

    bool prime = config["prime"] == "true";


    int i; 
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
    send_inital_memory_stats_to_memcached_server();
    send_inital_experiment_control_to_memcached_server(num_memory_servers);
    send_table_config_to_memcached_server(msm);
    multi_threaded_connection_setup(server_sockaddr, base_port, num_qps);
    start_distributed_experiment(server_index);


    printf("All server setup complete, now serving memory requests\n");
    moniter_run(num_qps, 1 ,prime, runtime, use_runtime, msm);

    ALERT("RDMA memory server", "Sending results to the memcached server\n");
    // msm.print_table();
    int bad_row = msm.crc_table();
    if (bad_row > 0) {
        Table * table = msm.get_table();
        ALERT("RDMA memory server", "CRC table failed on row %d\n", bad_row);
        ALERT("RDMA memory server", "CRC should be %lX, printing current row\n", table->crc64_row(bad_row));
        ALERT("RDMA memory server", "%s", table->row_to_string(bad_row).c_str());
    }

    send_final_memory_stats_to_memcached_server(msm);
    ALERT("RDMA memory server", "Run Complete");
    ret = disconnect_and_cleanup(num_qps);
    if (ret) { 
        rdma_error("Failed to clean up resources properly, ret = %d \n", ret);
        return ret;
    }
    return 0;
}


