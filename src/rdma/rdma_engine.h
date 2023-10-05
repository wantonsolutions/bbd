#pragma once
#ifndef RDMA_ENGINE_H
#define RDMA_ENGINE_H

#include "../cuckoo/virtual_rdma.h"
#include "../cuckoo/cuckoo.h"
#include "../slib/state_machines.h"
#include "../slib/config.h"
#include "rdma_client_lib.h"

using namespace state_machines;
using namespace cuckoo_rcuckoo;
#define ID_SIZE 64

namespace rdma_engine {

    typedef struct state_machine_init_arg{
        unordered_map <string, string> config;
        RDMAConnectionManager * cm;
        int id;
    } state_machine_init_arg;

    enum state_machine_type {
        rcuckoo_client,
        slogger_client,
    };
    
    class RDMA_Engine {
        public:
            RDMA_Engine();
            ~RDMA_Engine() {
                printf("RDMA_Engine destructor\n");
            }
            RDMA_Engine(unordered_map<string, string> config, state_machine_type sm);
            void Init_State_Machines(unordered_map<string,string> config);
            void Set_State_Machine(state_machine_type sm);
            void debug_masked_cas();
            bool start();

        private:
            bool _prime;
            int _num_clients;
            void set_control_flag(State_Machine *machine);
            void start_distributed_experiment();
            void stop_distributed_experiment();
            experiment_control *get_experiment_control();
            memory_stats * get_memory_stats();
            unordered_map<string, string> _config;
            RDMAConnectionManager  *_connection_manager;
    };
}

#endif