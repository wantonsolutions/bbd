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
using namespace replicated_log;

#define LOG_MR_INDEX 0
// #define LOCK_TABLE_MR_INDEX 1
#define TAIL_POINTER_STARTING_ADDRESS 0


static on_chip_memory_attr device_memory;


void copy_tail_pointer_from_device_memory(Replicated_Log &rl) {
    copy_device_memory_to_host_object((void *)rl.get_tail_pointer(), rl.get_tail_pointer_size_bytes(), device_memory);
}

static void send_slog_config_to_memcached_server(Replicated_Log& rl)
{

    //Regiserting the memory for the table
    //Send the info for the table
    void * log_ptr = rl.get_log_pointer();

    ibv_mr * log_mr = register_server_object_at_mr_index(log_ptr, rl.get_size_bytes(), LOG_MR_INDEX);

    int tail_pointer_size = rl.get_tail_pointer_size_bytes();
    printf("asking for a tail pointer of size %d\n", tail_pointer_size);
    device_memory = register_device_memory(TAIL_POINTER_STARTING_ADDRESS, tail_pointer_size);

    slog_config config;
    // Table * table = msm.get_table();
    config.slog_address = (uint64_t) log_mr->addr;
    config.slog_key = (uint32_t) log_mr->lkey;
    config.slog_size_bytes = rl.get_size_bytes();

    config.tail_pointer_address = (uint64_t) TAIL_POINTER_STARTING_ADDRESS;
    config.tail_pointer_key = (uint32_t) device_memory.mr->lkey;
    config.tail_pointer_size_bytes = tail_pointer_size;
    memcached_publish_slog_config(&config);
}



void moniter_run(int num_qps, int print_frequency, bool prime, int runtime, bool use_runtime, Replicated_Log& rl) {

    //print buffers every second
    int print_step=0;
    bool priming_complete = false;
    time_t last_print;
    time_t experiment_start_time;
    time(&last_print);
    while(true) {
        time_t now;
        time(&now);
        float fill_percentage = rl.get_fill_percentage();
        if(now - last_print >= print_frequency) {
            ALERT("TODO", "CALCULATE FILL PERCENTAGE");
            last_print = now;
            printf("Printing table after %d seconds\n", print_step * print_frequency);
            print_step++;
            rl.Chase_Tail_Pointer();
            rl.Print_All_Entries();
            // copy_device_memory_to_host_lock_table(msm);
            // msm.print_table();
            // msm.print_lock_table();
            printf("%2.3f Full (total bytes %d) \n", fill_percentage, rl.get_size_bytes());
            ALERT("RUN MONITER", "TODO add the priming logic back in");
        }

        // if(prime && 
        // !priming_complete &&
        // (fill_percentage * 100.0) >= rl.get_prime_fill()
        // ) {
        //     printf("Table has reached it's priming factor\n");
        //     announce_priming_complete();
        //     priming_complete = true;
        //     if (use_runtime) {
        //         time(&experiment_start_time);
        //     }
        // }

        // if((fill_percentage * 100.0) >= rl.get_max_fill()) {
        //     printf("Table has reached it's full capactiy. Exiting globally\n");
        //     end_experiment_globally();
        //     break;
        // }

        // if(use_runtime && (now - experiment_start_time) >= runtime) {
        //     printf("Experiment has reached it's runtime capactiy. Exiting globally\n");
        //     end_experiment_globally();
        //     break;
        // }
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

    ALERT("Starting slogger server", "Starting server\n");

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
    Replicated_Log rl = Replicated_Log(memory_size_bytes);

    bool prime = (config["prime"] == "true");
    // msm.fill_table_with_incremental_values();

    //resolve the address from the config
    struct sockaddr_in server_sockaddr = server_address_to_socket_addr(config["server_address"]);

    printf("assigning base_port %s\n", config["base_port"].c_str());
    int base_port = stoi(config["base_port"]);
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
    send_inital_experiment_control_to_memcached_server();
    send_slog_config_to_memcached_server(rl);
    multi_threaded_connection_setup(server_sockaddr, base_port, num_qps);
    start_distributed_experiment();


    printf("All server setup complete, now serving memory requests\n");
    moniter_run(num_qps, 1 ,prime, runtime, use_runtime, rl);

    // ALERT("RDMA memory server", "Sending results to the memcached server\n");
    ALERT("SLogger server", "TODO send the results to the memcached server");
    // send_final_memory_stats_to_memcached_server(msm);

    ret = disconnect_and_cleanup(num_qps);
    if (ret) { 
        rdma_error("Failed to clean up resources properly, ret = %d \n", ret);
        return ret;
    }
    return 0;
}


