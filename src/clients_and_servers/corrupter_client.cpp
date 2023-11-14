#include "../rdma/rdma_common.h"
#include "../rdma/rdma_engine.h"
#include "../slib/state_machines.h"
#include "../slib/config.h"
#include "../slib/log.h"
#include "../corrupter/corrupter.h"



using namespace std;
using namespace rdma_engine;
using namespace corrupter;


int main(int argc, char **argv){
    if (argc > 2) {
        ALERT("SLogger Client", "ERROR Too many arguments Usage: ./slogger_client <config_file>\n");
        for (int i = 0; i < argc; i++) {
            ALERT("Slogger Client", "ERROR Argument %d: %s\n", i, argv[i]);
        }
        exit(1);
    }
    string config_filename = "configs/default_config.json";
    if (argc == 2) {
        config_filename = argv[1];
    }
    INFO("Corrupter", "Starting Corrupter Client with config file %s\n", config_filename.c_str());
    unordered_map<string, string> config = read_config_from_file(config_filename);
    RDMA_Engine client_1 = RDMA_Engine(config, corrupter_client);
    client_1.start();

    //now we call the engine
}