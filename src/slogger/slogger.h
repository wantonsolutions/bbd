#ifndef SLOG
#define SLOG

#include "../slib/log.h"
#include "../slib/state_machines.h"
#include "replicated_log.h"

using namespace state_machines;
using namespace replicated_log;

namespace slogger {

    class SLogger : public State_Machine {
        public:
            SLogger(){};
            SLogger(unordered_map<string, string> config);
            ~SLogger() {ALERT("SLOG", "deleting slog");}
            void clear_statistics();

        private:
            Replicated_Log _replicated_log;

    };
}

#endif