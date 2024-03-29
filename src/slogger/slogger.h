#ifndef SLOG
#define SLOG

#include "../slib/log.h"
#include "../slib/config.h"
#include "../slib/state_machines.h"
#include "../rdma/rdma_common.h"
#include "rslog.h"
#include "replicated_log.h"
#include "logger.h"

using namespace state_machines;

#define ID_SIZE 64
#define NOOP_BUFFER_SIZE 1024 * 1024 * 8


namespace slogger {


    class SLogger : public logger_interface, public State_Machine {
        public:
            SLogger(){};
            SLogger(unordered_map<string, string> config);

            //Write operation puts the operation into the log
            bool Write_Operation(void* op, int size);
            bool Write_Operations(void ** ops, unsigned int * sizes, unsigned int num_ops);
            void * Next_Operation();
            void * Peek_Next_Operation();
            void Sync_To_Last_Write();

            void Sync_To_Remote_Log();



            uint64_t local_to_remote_log_address(uint64_t local_address);
            void add_remote(rdma_info info, string name, int memory_server_index);
            void fsm();
            void Clear_Statistics();
            unordered_map<string, string> get_stats();

        protected:
            void Read_Remote_Tail_Pointer();
            void Read_Client_Positions(bool block);
            bool Update_Remote_Client_Position(uint64_t new_tail);
            void Update_Client_Position(uint64_t new_tail);
            const char * log_id();

            Replicated_Log _replicated_log;
            RSlog _rslog;
            RSlogs _rslogs;

            Client_Workload_Driver _workload_driver;
            ycsb_workload _workload;
            void set_workload(string workload);

            char _log_identifier[ID_SIZE];
            uint32_t _id;
            uint32_t _total_clients;

            vector<rdma_info> get_remote_info() {return _rslogs.get_rdma_info();}


        private:

            void Write_Log_Entry(void* data, unsigned int size);
            void Write_Log_Entries(void ** data, unsigned int * sizes, unsigned int num_entries);
            void Syncronize_Log(uint64_t offset);
            void Batch_Read_Next_N_Entries(int entries);

            bool (SLogger::*_allocate_log_entry)(unsigned int entries);
            bool faa_allocate_log_entry(unsigned int entries);
            bool cas_allocate_log_entry(unsigned int entries);

            bool insert_n_sequential_ints(int starting_int, int batch_size);

            void set_epoch_and_tail_pointer_after_FAA(uint64_t add);
            void set_allocate_function(unordered_map<string, string> config);

            //silly variables
            int _entry_size;
            int _batch_size;
            int _bits_per_client_position;

            void faa_alloc_stats();
            void cas_alloc_stats(bool allocated);
            void read_tail_stats();
            void read_position_stats(int position_size);
            void write_log_entries_stats(uint64_t size);
            void insert_stats(uint64_t latency, uint64_t batch_size);

            /// Statistic Variables
            uint64_t _sync_calls;
            uint64_t _sync_calls_retry;
            uint64_t _stall_count;

    };
}

#endif