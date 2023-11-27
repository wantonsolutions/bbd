#include "mem_chunks.h"
#include "string.h"

namespace mem_chunks {

    Mem_Chunks::Mem_Chunks(unsigned int memory_size, unsigned int chunk_size){
        this->_memory_size = memory_size;
        this->_chunk_size = chunk_size;
        this->_chunk_start = new uint8_t[memory_size];
        ALERT("Mem Chunks", "Creating Mem Chunks with memory size %d and chunk size %d", memory_size, chunk_size);
        memset(this->_chunk_start, 0, memory_size);
    }


    Mem_Chunks::Mem_Chunks(){
        this->_memory_size = 0;
        this->_chunk_size = 0;
        this->_chunk_start = NULL;
    }
}