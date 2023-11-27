#ifndef MEM_CHUNKS_H
#define MEM_CHUNKS_H

#include <stdint.h>
#include "../slib/log.h"
#include <string>

using namespace std;

namespace mem_chunks {


    class Mem_Chunks {
        public:
            Mem_Chunks();
            Mem_Chunks(unsigned int memory_size, unsigned int chunk_size);
            // ~Replicated_Log() {ALERT("REPLICATED_LOG", "deleting replicated log");}

            void * get_chunk_pointer(unsigned int chunk_index) {
                if(chunk_index >= this->get_num_chunks()){
                    ALERT("Mem Chunks", "Chunk index %d is out of bounds", chunk_index);
                    return NULL;
                }
                return (void*) (this->_chunk_start + (chunk_index * this->_chunk_size));
            }
            int get_size_bytes();
            unsigned int get_memory_size() {return this->_memory_size;}
            unsigned int get_chunk_size() {return this->_chunk_size;}
            unsigned int get_num_chunks() {return this->_memory_size / this->_chunk_size;}

        private:
            unsigned int _memory_size;
            unsigned int _chunk_size;
            uint8_t* _chunk_start;

    };

}

#endif