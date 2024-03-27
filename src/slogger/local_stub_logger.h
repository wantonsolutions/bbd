#ifndef LOCAL_STUB_LOGGER_H
#define LOCAL_STUB_LOGGER_H

#include "logger.h"
#include "replicated_log.h"
#include <unordered_map>
#include <string>

namespace slogger {

    class Local_Stub_Logger : public logger_interface {
        public:
            Local_Stub_Logger();
            Local_Stub_Logger(unordered_map<string, string> config);

            //Write operation puts the operation into the log
            bool Write_Operation(void* op, int size);
            bool Write_Operations(void ** ops, unsigned int * sizes, unsigned int num_ops);
            void * Next_Operation();
            void * Peek_Next_Operation();
            void Sync_To_Last_Write();


        private:
            int _entry_size;
            int _batch_size;
            int _bits_per_client_position;
            int _id;
            int _total_clients;
            Replicated_Log _replicated_log;
            bool alloc_log_entries(unsigned int entries);
            void Write_Log_Entry(void* data, unsigned int size);
            void Write_Log_Entries(void ** data, unsigned int * sizes, unsigned int num_entries);
            void Update_Client_Position(uint64_t new_tail);
            void Syncronize_Log(uint64_t offset);

            const char * log_id();



    };
}

#endif