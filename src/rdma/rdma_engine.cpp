
#include <vector>
#include <infiniband/verbs.h>
#include <atomic>

#include <linux/kernel.h>
#include <sched.h>

#include <chrono>

#include "rdma_engine.h"
#include "rdma_common.h"
#include "rdma_client_lib.h"
#include "rdma_helper.h"

#include "../cuckoo/virtual_rdma.h"
#include "../cuckoo/cuckoo.h"

#include "../slogger/slogger.h"

#include "../slib/state_machines.h"
#include "../slib/config.h"
#include "../slib/log.h"
#include "../slib/memcached.h"


using namespace std;
using namespace state_machines;
using namespace rdma_helper;
using namespace cuckoo_rcuckoo;
using namespace slogger;


volatile bool global_start_flag = false;
volatile bool global_prime_flag = false;
volatile bool global_end_flag = false;


#define MAX_THREADS 40
State_Machine *state_machine_holder[MAX_THREADS];

const int yeti_core_order[40]={1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43,45,47,49,51,53,55,57,59,61,63,65,67,69,71,73,75,77,79};
const int yeti_control_core = 0;
int yak_core_order[24]={0,2,4,6,8,10,12,14,16,18,20,22,1,3,5,7,9,11,13,15,17,19,21,23};
int yak_control_core = 0;

//These are the functions we have to implement to run different state machines
void (*collect_stats) (State_Machine ** state_machines, unordered_map<string,string> config, int num_clients, chrono::milliseconds ms_int);
void * (*thread_init) (void * arg);
void * (*thread_runner) (void * arg);


void * cuckoo_fsm_runner(void * args){
    VERBOSE("RDMA Engine","launching threads in a cuckoo fsm runner\n");
    RCuckoo * cuck = (RCuckoo *) args;
    cuck->rdma_fsm();
    pthread_exit(NULL);
}

void * rcuckoo_thread_init(void * arg) {
    using namespace rdma_engine;
    state_machine_init_arg * rcuckoo_arg = (state_machine_init_arg *) arg;
    unordered_map <string, string> config;
    std::copy(rcuckoo_arg->config.begin(), rcuckoo_arg->config.end(), std::inserter(config, config.end()));
    

    ALERT("RDMA Engine", "Rcuckoo instace %i\n", rcuckoo_arg->id);
    config["id"]=to_string(rcuckoo_arg->id);
    RCuckoo * rcuckoo = new RCuckoo(config);

    struct rcuckoo_rdma_info info;
    info.qp = rcuckoo_arg->cm->client_qp[rcuckoo_arg->id];
    info.completion_queue = rcuckoo_arg->cm->client_cq_threads[rcuckoo_arg->id];
    info.pd = rcuckoo_arg->cm->pd;
    rcuckoo->init_rdma_structures(info);
    state_machine_holder[rcuckoo_arg->id] = rcuckoo;
    pthread_exit(NULL);
}

memory_stats * get_cuckoo_memory_stats(){
    int wait_coutner = 0;
    while (true) {
        memory_stats * stats = memcached_get_memory_stats();
        if (stats->finished_run) {
            return stats;
        }
        wait_coutner++;
        usleep(1000);
        printf("Waiting for the memory server to deliver the stats %d\n", wait_coutner);

        if (wait_coutner > 1000) {
            printf("Error: the memory server is not responding\n");
            exit(1);
        }
    }
}

void rcuckoo_stat_collection(State_Machine ** state_machines, unordered_map<string,string> config, int num_clients, auto ms_int){
    //Collect statistics from each of the threads
    vector<unordered_map<string,string>> client_statistics;
    for (int i=0;i<num_clients;i++) {
        INFO("RDMA Engine", "Grabbing Statistics Off of Client Thread %d\n", i);
        if (RCuckoo * cuckoo = dynamic_cast<RCuckoo *>(state_machines[i])) {
            client_statistics.push_back(cuckoo->get_stats());
        }
        else {
            ALERT("RDMA Engine", "Could not cast state machine to RCuckoo\n");
            exit(1);
        }
    }
    SUCCESS("RDMA Engine", "Grabbed Statistics Off of All Client %d Threads\n", num_clients);

    uint64_t puts = 0;
    uint64_t gets = 0;
    for (int i=0;i<num_clients;i++) {
        puts += stoull(client_statistics[i]["completed_puts"]);
        gets += stoull(client_statistics[i]["completed_gets"]);
    }

    unordered_map<string,string> system_statistics;
    system_statistics["runtime_ms"] = to_string(ms_int.count());
    system_statistics["runtime_s"]= to_string(ms_int.count() / 1000.0);
    system_statistics["put_throughput"] = to_string(puts / (ms_int.count() / 1000.0));
    system_statistics["get_throughput"] = to_string(gets / (ms_int.count() / 1000.0));
    system_statistics["throughput"]= to_string((puts + gets) / (ms_int.count() / 1000.0));


    float throughput = puts / (ms_int.count() / 1000.0);
    SUCCESS("RDMA Engine", "Throughput: %f\n", throughput);
    ALERT("Final Tput", "%d,%f\n", num_clients, throughput);

    memory_stats *ms;

    
    unordered_map<string,string> memory_statistics;
    memory_statistics["fill"]= to_string(ms->fill);
    write_statistics(config, system_statistics, client_statistics, memory_statistics);
    // free(thread_ids);
    VERBOSE("RDMA Engine", "done running state machine!");
}

void slogger_stat_collection(State_Machine ** state_machines, unordered_map<string,string> config, int num_clients, auto ms_int) {
    ALERT("RDMA Engine", "TODO we don't collect slogger statistics at the moment\n");
}

void * slogger_thread_init(void * arg) {
    using namespace rdma_engine;
    using namespace slogger;

    state_machine_init_arg * slogger_arg = (state_machine_init_arg *) arg;
    unordered_map <string, string> config;
    std::copy(slogger_arg->config.begin(), slogger_arg->config.end(), std::inserter(config, config.end()));
    

    ALERT("RDMA Engine", "Slogger instace %i\n", slogger_arg->id);
    config["id"]=to_string(slogger_arg->id);
    SLogger * slogger = new SLogger(config);


    ALERT("RDMA Engine", "TODO setup RDMA rdma resources within the slogger\n");
    // struct rcuckoo_rdma_info info;
    // info.qp = slogger_arg->cm->client_qp[rcuckoo_arg->id];
    // info.completion_queue = rcuckoo_arg->cm->client_cq_threads[rcuckoo_arg->id];
    // info.pd = rcuckoo_arg->cm->pd;
    // rcuckoo->init_rdma_structures(info);
    state_machine_holder[slogger_arg->id] = slogger;
    pthread_exit(NULL);
}

void * slogger_fsm_runner(void * args){
    ALERT("RDMA Engine","launching threads in a slogger fsm\n");
    SLogger * slogger = (SLogger *) args;
    slogger->fsm();
    pthread_exit(NULL);
}


namespace rdma_engine {

    
    RDMA_Engine::RDMA_Engine(){
        ALERT("RDMA Engine", "Don't blindly allocate an RDMA state machine");
        exit(1);
    }

    void RDMA_Engine::Set_State_Machine(state_machine_type sm){
        printf("setting the state machine type\n");
        switch (sm) {
            case rcuckoo_client:
                collect_stats = rcuckoo_stat_collection;
                thread_init = rcuckoo_thread_init;
                thread_runner = cuckoo_fsm_runner;
                break;
            case slogger_client:
                ALERT("RDMA Engine", "TODO SLOGGER\n");
                collect_stats = slogger_stat_collection;
                thread_init = slogger_thread_init;
                thread_runner = slogger_fsm_runner;
                // exit(0);
                break;
            default:
                ALERT("RDMA Engine", "Unknown state machine type\n");
                exit(1);
        }
    }

    void RDMA_Engine::Init_State_Machines(unordered_map<string, string> config) {
        int i;
        pthread_t thread_ids[MAX_THREADS];
        state_machine_init_arg init_args[MAX_THREADS];
        try {
            for (i=0;i<_num_clients;i++) {

                init_args[i].config = config;
                init_args[i].cm = _connection_manager;
                init_args[i].id = i;
                pthread_create(&thread_ids[i], NULL, thread_init,&init_args[i]);

            }

        } catch (exception& e) {
            ALERT("RDMA Engine", "RDMAConnectionManager failed to create state machine wrapper %i\n", i);
            ALERT("RDMA Engine", "Error: %s\n", e.what());
            exit(1);
            return;
        }

        for (i=0;i<_num_clients;i++) {
            ALERT("RDMA Engine", "Joinging Thread %d\n",i);
            pthread_join(thread_ids[i], NULL);
        }
        ALERT("RDMA Engine", "State Machine Threads Joined\n");

    }

            
    RDMA_Engine::RDMA_Engine(unordered_map<string, string> config, state_machine_type sm) {

        _config = config;
        try {
            _num_clients = stoi(config["num_clients"]);
            _prime = (config["prime"] == "true");

        } catch (exception &e) {
            ALERT("RDMA Engine",": unable to parse rdma engine config %s", e.what());
            exit(1);
        } 

        try {
            RDMAConnectionManagerArguments args;


            args.num_qps = _num_clients;
            if (args.num_qps < 1) {
                ALERT("RDMA Engine", "Error: num_qps must be at least 1\n");
                exit(1);
            }
            #define MAX_RDMA_ENGINE_QPS MAX_THREADS
            if (args.num_qps > MAX_RDMA_ENGINE_QPS) {
                ALERT("RDMA Engine", "Error: num_qps must be at most %d, we are only enabling a few QP per process\n", MAX_RDMA_ENGINE_QPS);
                ALERT("RDMA Engine", "TODO; we probably need a better way to scale clients if we are going more than this.\n");
                exit(1);
            }

            //resolve the address from the config
            struct sockaddr_in server_sockaddr = server_address_to_socket_addr(config["server_address"]);
            args.server_sockaddr = &server_sockaddr;

            INFO("RDMA Engine","assigning base_port %s\n", config["base_port"].c_str());
            args.base_port = stoi(config["base_port"]);

            // _connection_manager = RDMAConnectionManager(args);
            _connection_manager = new RDMAConnectionManager(args);
            VERBOSE("RDMA Engine", "RDMAConnectionManager created\n");
        } catch (exception& e) {
            ALERT("RDMA Engine", "RDMAConnectionManager failed to create\n");
            ALERT("RDMA Engine", "Error: %s\n", e.what());
            exit(1);
            return;
        }

        Set_State_Machine(sm);
        Init_State_Machines(config);

        return;
    }


    experiment_control * RDMA_Engine::get_experiment_control(){
        return memcached_get_experiment_control();
    }


    void RDMA_Engine::set_control_flag(State_Machine *machine) {
            machine->set_global_start_flag(&global_start_flag);
            machine->set_global_end_flag(&global_end_flag);
            machine->set_global_prime_flag(&global_prime_flag);
    }

    bool RDMA_Engine::start() {
        VERBOSE("RDMA Engine", "starting rdma engine\n");
        VERBOSE("RDMA Engine", "for the moment just start the first of the state machines\n");
        assert(_num_clients <= MAX_THREADS);
        if (_num_clients > MAX_THREADS) {
            ALERT("RDMA Engine", "Error: num_clients must be at most %d, we are only enabling a few QP per process\n", MAX_THREADS);
            ALERT("RDMA Engine", "TODO; we probably need a better way to scale clients if we are going more than this.\n");
            exit(1);
        }

        //if we are not priming then set the prime flag right away
        if(!_prime){
            global_prime_flag=true;
        }

        pthread_t thread_ids[MAX_THREADS];
        for(int i=0;i<_num_clients;i++){
            INFO("RDMA Engine","Creating Client Thread %d\n", i);
            set_control_flag(state_machine_holder[i]);
            pthread_create(&thread_ids[i], NULL, thread_runner, (state_machine_holder[i]));
            stick_thread_to_core(thread_ids[i], yak_core_order[i]);
        }

        stick_this_thread_to_core(yak_control_core);

        using std::chrono::high_resolution_clock;
        using std::chrono::duration_cast;
        using std::chrono::duration;
        using std::chrono::milliseconds;

        while(true){
            experiment_control *ec = get_experiment_control();
            if(ec->experiment_start){
                ALERT("RDMA Engine", "Experiment Starting Globally\n");
                global_start_flag = true;
                break;
            }
        }

        //Start the treads
        auto t1 = high_resolution_clock::now();
        bool priming_action_taken = false;

        while(true){
            experiment_control *ec = get_experiment_control();
            if(ec->experiment_stop){
                ALERT("RDMA Engine", "Experiment Stop Globally\n");
                global_end_flag = true;
                break;
            }
            //reset statistics if we are doing a priming run
            if(_prime &&
             ec->priming_complete && 
             !priming_action_taken) {
                ALERT("RDMA Engine", "Experiment Priming Complete -- do priming things\n");
                priming_action_taken = true;
                global_prime_flag = true;
                t1=high_resolution_clock::now();
            }
            if(global_end_flag == true) {
                break;
            }
            // free(ec);
        }
        auto t2 = high_resolution_clock::now();
        chrono::milliseconds ms_int = duration_cast<milliseconds>(t2 - t1);

        //Get all of the threads to join
        for (int i=0;i<_num_clients;i++){
            INFO("RDMA Engine", "Joining Client Thread %d\n", i);
            pthread_join(thread_ids[i],NULL);
        }
        ALERT("RDMA Engine", "Experiment Complete\n");

        collect_stats(state_machine_holder,_config, _num_clients, ms_int);
    }

}