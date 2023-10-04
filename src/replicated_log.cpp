#include "replicated_log.h"

namespace replicated_log {

    Replicated_Log::Replicated_Log(unsigned int memory_size) {
        this->_memory_size = memory_size;
        this->_log = new uint8_t[memory_size];
    }
    Replicated_Log::Replicated_Log(){
        this->_memory_size = 0;
        this->_log = NULL;
        }

}