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
#include "../corrupter/mem_chunks.h"

using namespace std;
using namespace mem_chunks;

#define CHUNK_MR_INDEX 0


static on_chip_memory_attr device_memory;

static void send_final_memory_stats_to_memcached_server(Mem_Chunks& mc){
    memory_stats ms;
    ms.finished_run = true;
    ms.fill = 0;
    ALERT("Corrupter server", "Do something with the corruption stats");
    memcached_publish_memory_stats(&ms);
}

static void send_mem_chunk_config_to_memcached_server(Mem_Chunks& mc)
{

    //Regiserting the memory for the table
    //Send the info for the table
    void * mc_ptr = mc.get_chunk_pointer(0);
    ibv_mr * log_mr = register_server_object_at_mr_index(mc_ptr, mc.get_memory_size(), CHUNK_MR_INDEX);

    corrupter_config config;
    // Table * table = msm.get_table();
    config.chunk_address = (uint64_t) log_mr->addr;
    config.chunk_key = (uint32_t) log_mr->lkey;
    config.chunk_mem_size = mc.get_memory_size();
    config.chunk_size = mc.get_chunk_size();

    memcached_publish_corrupter_config(&config);
}


void moniter_run(int print_frequency, int runtime, bool use_runtime, Mem_Chunks& mc) {

    //print buffers every second
    int print_step=0;
    time_t last_print;
    time_t experiment_start_time;
    time(&experiment_start_time);
    time(&last_print);
    while(true) {
        time_t now;
        time(&now);
        if(now - last_print >= print_frequency) {
            ALERT("TODO", "CALCULATE FILL PERCENTAGE");
            last_print = now;
            printf("Printing table after %d seconds\n", print_step * print_frequency);
            print_step++;
            // copy_device_memory_to_host_lock_table(msm);
            // msm.print_table();
            // msm.print_lock_table();
            // printf("%2.3f Full (total bytes %d) \n", fill_percentage, rl.get_size_bytes());
            ALERT("RUN MONITER", "TODO add the priming logic back in");
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
    printf("./slogger_server <config_path>\n");
    exit(1);
}


int main(int argc, char **argv) 
{

    ALERT("Starting corrputer server", "Starting server\n");

    if (argc > 2) {
        ALERT("parse error", "ERROR Too many arguemnts Usage: ./rdma_server <config_file>\n");
        exit(1);
    }
    string config_filename = "configs/default_config.json";
    if (argc == 2) {
        config_filename = argv[1];
    }

    unordered_map<string, string> config = read_config_from_file(config_filename);
    int memory_size_bytes = stoi(config["memory_size"]);
    int chunk_size = stoi(config["chunk_size"]);
    Mem_Chunks mc = Mem_Chunks(memory_size_bytes, chunk_size);

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
    // send_inital_memory_stats_to_memcached_server();
    send_inital_experiment_control_to_memcached_server(num_memory_servers);
    send_mem_chunk_config_to_memcached_server(mc);
    multi_threaded_connection_setup(server_sockaddr, base_port, num_qps);
    start_distributed_experiment(server_index);


    printf("All server setup complete, now serving memory requests\n");
    moniter_run(1,runtime,use_runtime, mc);

    // ALERT("RDMA memory server", "Sending results to the memcached server\n");
    ALERT("Chunk server", "TODO send the results to the memcached server");
    send_final_memory_stats_to_memcached_server(mc);

    ret = disconnect_and_cleanup(num_qps);
    if (ret) { 
        rdma_error("Failed to clean up resources properly, ret = %d \n", ret);
        return ret;
    }
    return 0;
}


