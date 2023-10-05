#ifndef REPLICATED_LOG_H
#define REPLICATED_LOG_H

#include <stdint.h>
#include "../slib/log.h"
#include <string>

using namespace std;

namespace replicated_log {

    typedef struct Basic_Entry {
        uint16_t entry_size;
        uint8_t entry_type;
        uint8_t repeating_value;
        string ToString();
    } Basic_Entry;

    class Replicated_Log {
        public:
            Replicated_Log();
            Replicated_Log(unsigned int memory_size);
            ~Replicated_Log() {ALERT("REPLICATED_LOG", "deleting replicated log");}
            void Append_Basic_Entry(Basic_Entry &bs);
            void Print_All_Entries();

        private:
            unsigned int _memory_size;
            uint8_t* _log;
            uint64_t _tail_pointer;
    };

}

#endif