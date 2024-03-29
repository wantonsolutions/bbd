
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
#include "../slogger/nt.h"
#include "../slogger/alloc.h"

#include "../corrupter/corrupter.h"

#include "../slib/state_machines.h"
#include "../slib/config.h"
#include "../slib/log.h"
#include "../slib/memcached.h"


using namespace std;
using namespace state_machines;
using namespace rdma_helper;
using namespace cuckoo_rcuckoo;
using namespace slogger;
using namespace corrupter;
using namespace nt;


volatile bool global_start_flag = false;
volatile bool global_prime_flag = false;
volatile bool global_end_flag = false;


#define MAX_THREADS 40
#define MAX_RDMA_ENGINE_QPS MAX_THREADS

State_Machine *state_machine_holder[MAX_CLIENT_THREADS];

#define YETI_CORES 40
#define YAK_CORES 24
int yeti_core_order[YETI_CORES]={1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43,45,47,49,51,53,55,57,59,61,63,65,67,69,71,73,75,77,79};
int yeti_control_core = 0;
int yak_core_order[YAK_CORES]={0,2,4,6,8,10,12,14,16,18,20,22,1,3,5,7,9,11,13,15,17,19,21,23};
int yak_control_core = 1;

int control_core;
int *core_order;
int total_cores;

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

void set_core_order(void) {
    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);
    if (strncmp(hostname,"yak",3)==0) {
        ALERT("ENV CONF", "Running on a yak machine");
        core_order = yak_core_order;
        control_core = yak_control_core;
        total_cores = YAK_CORES;
    }else if (strncmp(hostname, "yeti",4) == 0){
        ALERT("ENV CONF", "Running on a yeti machine");
        core_order = yeti_core_order;
        control_core = yeti_control_core;
        total_cores = YETI_CORES;
    } else {
        ALERT("ENV CONF", "Runnin on an unknown host %s, please create a core config. Exiting...",hostname);
        exit(0);
    }
}

void * rcuckoo_thread_init(void * arg) {
    using namespace rdma_engine;
    state_machine_init_arg * rcuckoo_arg = (state_machine_init_arg *) arg;
    unordered_map <string, string> config;
    std::copy(rcuckoo_arg->config.begin(), rcuckoo_arg->config.end(), std::inserter(config, config.end()));
    

    config["id"]=to_string(rcuckoo_arg->id);
    RCuckoo * rcuckoo = new RCuckoo(config);

    struct rdma_info info;

    ALERT("WARNING", "RCUCKOO WAS NEVER SET UP TO HAVE MULTIPLE MEMORY MACHIENS");
    for (int i=0;i<rcuckoo_arg->cms.size();i++){
        info.qp = rcuckoo_arg->cms[i]->client_qp[rcuckoo_arg->id];
        info.completion_queue = rcuckoo_arg->cms[i]->client_cq_threads[rcuckoo_arg->id];
        info.pd = rcuckoo_arg->cms[i]->pd;
        rcuckoo->init_rdma_structures(info);
    }
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

void rcuckoo_stat_collection(State_Machine ** state_machines, unordered_map<string,string> config, int num_clients, chrono::milliseconds  ms_int){
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
    uint64_t updates = 0;
    for (int i=0;i<num_clients;i++) {
        puts += stoull(client_statistics[i]["completed_puts"]);
        gets += stoull(client_statistics[i]["completed_gets"]);
        updates += stoull(client_statistics[i]["completed_update_count"]);
    }

    unordered_map<string,string> system_statistics;
    system_statistics["runtime_ms"] = to_string(ms_int.count());
    system_statistics["runtime_s"]= to_string(ms_int.count() / 1000.0);
    system_statistics["put_throughput"] = to_string(puts / (ms_int.count() / 1000.0));
    system_statistics["get_throughput"] = to_string(gets / (ms_int.count() / 1000.0));
    system_statistics["update_throughput"] = to_string(updates / (ms_int.count() / 1000.0));
    system_statistics["throughput"]= to_string((puts + gets + updates) / (ms_int.count() / 1000.0));


    float throughput = (puts + gets + updates) / (ms_int.count() / 1000.0);
    SUCCESS("RDMA Engine", "Throughput: %f\n", throughput);
    ALERT("Final Tput", "%d,%f\n", num_clients, throughput);

    // memory_stats *ms;
    // ms->fill=0.13371337;

    
    unordered_map<string,string> memory_statistics;
    memory_statistics["fill"]="0.13371337";
    write_statistics(config, system_statistics, client_statistics, memory_statistics);
    // free(thread_ids);
    VERBOSE("RDMA Engine", "done running state machine!");
}

void slogger_stat_collection(State_Machine ** state_machines, unordered_map<string,string> config, int num_clients, chrono::milliseconds  ms_int) {
    vector<unordered_map<string,string>> client_statistics;
    for (int i=0;i<num_clients;i++) {
        INFO("RDMA Engine", "Grabbing Statistics Off of Client Thread %d\n", i);
        if (SLogger * cuckoo = dynamic_cast<SLogger *>(state_machines[i])) {
            client_statistics.push_back(cuckoo->get_stats());
        }
        else {
            ALERT("RDMA Engine", "Could not cast state machine to RCuckoo\n");
            exit(1);
        }
    }
    SUCCESS("RDMA Engine", "Grabbed Statistics Off of All Client %d Threads\n", num_clients);


    unordered_map<string,string> system_statistics;
    system_statistics["runtime_ms"] = to_string(ms_int.count());
    system_statistics["runtime_s"]= to_string(ms_int.count() / 1000.0);
    ALERT("RDMA Engine", "Runtime ms %d\n",(int)ms_int.count());

    uint64_t puts = 0;
    uint64_t gets = 0;
    uint64_t updates = 0;
    for (int i=0;i<num_clients;i++) {
        puts += stoull(client_statistics[i]["completed_read_count"]);
        gets += stoull(client_statistics[i]["completed_insert_count"]);
        updates += stoull(client_statistics[i]["completed_update_count"]);
    }
    system_statistics["put_throughput"] = to_string(puts / (ms_int.count() / 1000.0));
    system_statistics["get_throughput"] = to_string(gets / (ms_int.count() / 1000.0));
    system_statistics["update_throughput"] = to_string(updates / (ms_int.count() / 1000.0));
    system_statistics["throughput"]= to_string((puts + gets + updates) / (ms_int.count() / 1000.0));

    float throughput = (puts + gets + updates) / (ms_int.count() / 1000.0);
    SUCCESS("RDMA Engine", "Throughput: %f\n", throughput);
    ALERT("Final Tput", "%d,%f\n", num_clients, throughput);

    unordered_map<string,string> memory_statistics;
    memory_statistics["fill"]="0.13371337666";
    ALERT("RDMA Engine", "Writing out statistics\n");
    write_statistics(config, system_statistics, client_statistics, memory_statistics);


    VERBOSE("RDMA Engine", "done running state machine!");
}

void * slogger_thread_init(void * arg) {
    using namespace rdma_engine;
    using namespace slogger;

    state_machine_init_arg * slogger_arg = (state_machine_init_arg *) arg;
    unordered_map <string, string> config;
    std::copy(slogger_arg->config.begin(), slogger_arg->config.end(), std::inserter(config, config.end()));

    ALERT("RDMA Engine", "Slogger instace %i\n", slogger_arg->id);
    config["id"]=to_string(slogger_arg->id);
    // SLogger * slogger = new SLogger(config);
    SLogger * slogger = new RMalloc(config);
    // SLogger * slogger = new NT(config);
    string name = config["name"];

    int num_clients = stoi(config["num_clients"]);
    int thread_id = slogger_arg->id % num_clients;


    struct rdma_info info;
    for (int i=0;i<slogger_arg->cms.size();i++){
        info.qp = slogger_arg->cms[i]->client_qp[thread_id];
        info.completion_queue = slogger_arg->cms[i]->client_cq_threads[thread_id];
        info.pd = slogger_arg->cms[i]->pd;
        slogger->add_remote(info,name,i);
    }
    state_machine_holder[thread_id] = slogger;
    pthread_exit(NULL);
}

void * slogger_fsm_runner(void * args){
    VERBOSE("RDMA Engine","launching threads in a slogger fsm\n");
    // SLogger * slogger = (SLogger *) args;
    // NT * slogger = (NT *) args;
    RMalloc * slogger = (RMalloc *) args;
    slogger->fsm();
    pthread_exit(NULL);
}

void corrupter_stat_collection(State_Machine ** state_machines, unordered_map<string,string> config, int num_clients, chrono::milliseconds  ms_int) {
    vector<unordered_map<string,string>> client_statistics;
    for (int i=0;i<num_clients;i++) {
        INFO("RDMA Engine", "Grabbing Statistics Off of Client Thread %d\n", i);
        if (Corrupter * corrupter = dynamic_cast<Corrupter *>(state_machines[i])) {
            client_statistics.push_back(corrupter->get_stats());
        }
        else {
            ALERT("RDMA Engine", "Could not cast state machine to RCuckoo\n");
            exit(1);
        }
    }
    SUCCESS("RDMA Engine", "Grabbed Statistics Off of All Client %d Threads\n", num_clients);

    unordered_map<string,string> system_statistics;
    system_statistics["runtime_ms"] = to_string(ms_int.count());
    system_statistics["runtime_s"]= to_string(ms_int.count() / 1000.0);
    ALERT("RDMA Engine", "Runtime ms %d\n",(int)ms_int.count());

    unordered_map<string,string> memory_statistics;
    memory_statistics["fill"]="0.13371337616";
    ALERT("RDMA Engine", "Writing out statistics\n");
    write_statistics(config, system_statistics, client_statistics, memory_statistics);
    // free(thread_ids);
    VERBOSE("RDMA Engine", "done running state machine!");
}

void * corrupter_thread_init(void * arg) {
    using namespace rdma_engine;

    state_machine_init_arg * corrupter_arg = (state_machine_init_arg *) arg;
    unordered_map <string, string> config;
    std::copy(corrupter_arg->config.begin(), corrupter_arg->config.end(), std::inserter(config, config.end()));
    

    ALERT("RDMA Engine", "Slogger instace %i\n", corrupter_arg->id);
    config["id"]=to_string(corrupter_arg->id);
    Corrupter * corrupter = new Corrupter(config);
    // SLogger * slogger = new NT(config);


    ALERT("WARNING", "CORRUPTER WAS NEVER SET UP TO HAVE MULTIPLE MEMORY MACHIENS");
    struct rdma_info info;

    for (int i=0;i<corrupter_arg->cms.size();i++){
        
        info.qp = corrupter_arg->cms[i]->client_qp[corrupter_arg->id];
        info.completion_queue = corrupter_arg->cms[i]->client_cq_threads[corrupter_arg->id];
        info.pd = corrupter_arg->cms[i]->pd;
        corrupter->init_rdma_structures(info);
    }
    state_machine_holder[corrupter_arg->id] = corrupter;
    pthread_exit(NULL);
}

void * corrupter_fsm_runner(void * args){
    ALERT("RDMA Engine","launching threads in a slogger fsm\n");
    Corrupter * corrupter = (Corrupter *) args;
    // NT * slogger = (NT *) args;
    corrupter->fsm();
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
                collect_stats = slogger_stat_collection;
                thread_init = slogger_thread_init;
                thread_runner = slogger_fsm_runner;
                // exit(0);
                break;
            case corrupter_client:
                collect_stats = corrupter_stat_collection;
                thread_init = corrupter_thread_init;
                thread_runner = corrupter_fsm_runner;
                break;
            default:
                ALERT("RDMA Engine", "Unknown state machine type\n");
                exit(1);
        }
    }

    void RDMA_Engine::Init_State_Machines(unordered_map<string, string> config) {
        int i;
        pthread_t thread_ids[MAX_CLIENT_THREADS];
        state_machine_init_arg init_args[MAX_CLIENT_THREADS];
        try {
            for (i=0;i<_num_clients;i++) {

                init_args[i].config = config;
                init_args[i].cms = _connection_managers;
                init_args[i].id = i + (_machine_id * _num_clients);
                pthread_create(&thread_ids[i], NULL, thread_init,&init_args[i]);

            }

        } catch (exception& e) {
            ALERT("RDMA Engine", "RDMAConnectionManager failed to create state machine wrapper %i\n", i);
            ALERT("RDMA Engine", "Error: %s\n", e.what());
            exit(1);
            return;
        }
        ALERT("RDMA Engine", "Created %d Threads. Waiting for them to init...\n",_num_clients);  
        for (i=0;i<_num_clients;i++) {
            pthread_join(thread_ids[i], NULL);
        }
        ALERT("RDMA Engine", "Joined %d Threads\n",_num_clients);
    }

    void * memserver_init(void * arg) {
        ALERT("RDMA Engine", "Creating RDMAConnectionManager\n");
        RDMAConnectionManagerArguments * args = (RDMAConnectionManagerArguments *) arg;
        RDMAConnectionManager * cm = new RDMAConnectionManager(*args);
        return cm;
    }

            

    void RDMA_Engine::Init_Memory_Server_Connections(vector<string> server_addresses, vector<string> base_ports) {
        int i;
        vector<pthread_t> thread_ids;
        _connection_managers.clear();
        _connection_managers.resize(server_addresses.size());
        thread_ids.resize(server_addresses.size());
        try{
            for (i=0;i<server_addresses.size();i++) {
                RDMAConnectionManagerArguments args;
                args.server_sockaddr = server_address_to_socket_addr(server_addresses[i]);
                args.base_port = stoi(base_ports[i]) + (_machine_id * _num_clients);
                args.num_qps = _num_clients;
                pthread_create(&thread_ids[i], NULL, &memserver_init, &args);
                usleep(100);
            }
        } catch (exception& e) {
            ALERT("RDMA Engine", "RDMAConnectionManager failed to create\n");
            ALERT("RDMA Engine", "Error: %s\n", e.what());
            exit(1);
            return;
        }
        for (i=0;i<server_addresses.size();i++) {
            pthread_join(thread_ids[i], (void **)&(_connection_managers[i]));
        }
    }



    RDMA_Engine::RDMA_Engine(unordered_map<string, string> config, state_machine_type sm) {

        _config = config;
        try {
            _num_clients = stoi(config["num_clients"]);
            _num_client_machines = stoi(config["num_client_machines"]);
            _prime = (config["prime"] == "true");

        } catch (exception &e) {
            ALERT("RDMA Engine",": unable to parse rdma engine config %s", e.what());
            exit(1);
        } 

        try {
            RDMAConnectionManagerArguments args;


            set_core_order();
            args.num_qps = _num_clients;
            // int current_clients = memcached_get_current_slogger_client_count();
            // ALERT("RDMA_ENGINE", "There are currently %d clients",current_clients);
            _machine_id = (int) memcached_get_next_slogger_client_id();
            config["machine_id"] = to_string(_machine_id);
            if (args.num_qps < 1) {
                ALERT("RDMA Engine", "Error: num_qps must be at least 1\n");
                exit(1);
            }
            if (args.num_qps > MAX_RDMA_ENGINE_QPS) {
                ALERT("RDMA Engine", "Error: num_qps must be at most %d, we are only enabling a few QP per process\n", MAX_RDMA_ENGINE_QPS);
                ALERT("RDMA Engine", "TODO; we probably need a better way to scale clients if we are going more than this.\n");
                exit(1);
            }

            if (!check_memory_server_config(config)) {
                ALERT("RDMA Engine", "Error: memory server config is not set\n");
                exit(1);
            }

            ALERT("RDMA Engine", "Starting Client Machine #%d\n", _machine_id);

            vector<string> server_addresses = split(config["server_addresses"], ',');
            vector<string> base_ports = split(config["base_ports"], ',');
            int num_memory_servers = server_addresses.size();
            Init_Memory_Server_Connections(server_addresses, base_ports);

            // for (int i=0;i<num_memory_servers;i++) {

            //     INFO("RDMA Engine", "Memory Server %d: %s:%s\n", i, server_addresses[i].c_str(), base_ports[i].c_str());
            //     args.server_sockaddr = server_address_to_socket_addr(server_addresses[i]);
            //     INFO("RDMA Engine","assigning base_port %s\n", base_ports[i].c_str());
            //     args.base_port = stoi(base_ports[i]) + (_machine_id * _num_clients);
            //     _connection_managers.push_back(new RDMAConnectionManager(args));
            //     VERBOSE("RDMA Engine", "RDMAConnectionManager created\n");
            // }


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
        assert(_num_clients <= MAX_CLIENT_THREADS);
        if (_num_clients > MAX_CLIENT_THREADS) {
            ALERT("RDMA Engine", "Error: num_clients must be at most %d, we are only enabling a few QP per process\n", MAX_CLIENT_THREADS);
            ALERT("RDMA Engine", "TODO; we probably need a better way to scale clients if we are going more than this.\n");
            exit(1);
        }

        //if we are not priming then set the prime flag right away
        if(!_prime){
            global_prime_flag=true;
        }

        pthread_t thread_ids[MAX_CLIENT_THREADS];
        for(int i=0;i<_num_clients;i++){
            assert(i < total_cores);
            INFO("RDMA Engine","Creating Client Thread %d\n", i);
            set_control_flag(state_machine_holder[i]);
            pthread_create(&thread_ids[i], NULL, thread_runner, (state_machine_holder[i]));
            // stick_thread_to_core(thread_ids[i], yak_core_order[i]);
            // ALERT("stick core", "Core %d",yeti_core_order[i]);
            stick_thread_to_core(thread_ids[i], core_order[i]);
        }

        stick_this_thread_to_core(yak_control_core);

        using std::chrono::high_resolution_clock;
        using std::chrono::duration_cast;
        using std::chrono::duration;
        using std::chrono::milliseconds;

        while(true){
            experiment_control *ec = get_experiment_control();
            if(ec->is_experiment_running()){
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
            // printf("ec lock %p\n", ec);
            if(ec->is_experiment_stopped()){
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
            ALERT("RDMA Engine", "Joining Client Thread %d\n", i);
            pthread_join(thread_ids[i],NULL);
        }

        ALERT("RDMA Engine", "Experiment Complete\n");

        collect_stats(state_machine_holder,_config, _num_clients, ms_int);
        return true;
    }

}