#ifndef REPLICATED_LOG_H
#define REPLICATED_LOG_H

#include <stdint.h>
#include "../slib/log.h"

using namespace std;

namespace replicated_log {

    class Replicated_Log {
        public:
            Replicated_Log();
            Replicated_Log(unsigned int memory_size);
            ~Replicated_Log() {ALERT("REPLICATED_LOG", "deleting replicated log");}

        private:
            unsigned int _memory_size;
            uint8_t* _log;
    };

}

#endif