#include "slogger.h"
#include "replicated_log.h"
#include "log.h"

using namespace replicated_log;

namespace slogger {

    SLogger::SLogger(unordered_map<string, string> config) : State_Machine(config){

        try {
            unsigned int memory_size = stoi(config["memory_size"]);
            _replicated_log = Replicated_Log(memory_size);
        } catch (exception& e) {
            ALERT("SLOG", "Error in SLogger constructor: %s", e.what());
            exit(1);
        }   

        ALERT("Slogger", "Done creating SLogger");
    }

}