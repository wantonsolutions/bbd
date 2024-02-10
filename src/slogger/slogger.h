#ifndef SLOG
#define SLOG

#include "../slib/log.h"
#include "../slib/config.h"
#include "../slib/state_machines.h"
#include "../rdma/rdma_common.h"
#include "rslog.h"
#include "replicated_log.h"

using namespace state_machines;

#define ID_SIZE 64
#define NOOP_BUFFER_SIZE 1024 * 1024 * 8


namespace slogger {


    class SLogger : public State_Machine {
        public:
            SLogger(){};
            SLogger(unordered_map<string, string> config);

            bool FAA_Allocate_Log_Entry(unsigned int entries);
            bool CAS_Allocate_Log_Entry(unsigned int entries);
            // bool Set_Client_Tail_Update(ibv_sge *sg, ibv_exp_send_wr* wr, uint64_t old_tail, uint64_t new_tail);

            bool Update_Remote_Client_Position(uint64_t new_tail);
            void Update_Client_Position(uint64_t new_tail);

            bool (SLogger::*_allocate_log_entry)(unsigned int entries);


            void Read_Remote_Tail_Pointer();

            void Read_Client_Positions(bool block);
            void Write_Log_Entry(void* data, unsigned int size);
            void Write_Log_Entries(void ** data, unsigned int * sizes, unsigned int num_entries);

            void Sync_To_Remote_Log();
            void Sync_To_Last_Write();
            void Syncronize_Log(uint64_t offset);


            void Write_Operation(void* op, int size);
            void * Next_Operation();
            uint64_t local_to_remote_log_address(uint64_t local_address);

            void fill_allocated_log_with_noops(uint64_t size);
            void add_remote(rdma_info info);
            void fsm();
            void clear_statistics();
            const char * log_id();
            unordered_map<string, string> get_stats();

        protected:
            Client_Workload_Driver _workload_driver;
            ycsb_workload _workload;
            void set_workload(string workload);

            char _log_identifier[ID_SIZE];
            uint32_t _id;
            uint32_t _total_clients;


        private:
            bool insert_n_sequential_ints(int starting_int, int batch_size);

            void set_epoch_and_tail_pointer_after_FAA(uint64_t add);
            void set_allocate_function(unordered_map<string, string> config);


            Replicated_Log _replicated_log;
            //RDMA Variables
            // ibv_qp * _qp;
            // ibv_pd *_protection_domain;
            // struct ibv_cq * _completion_queue;
            // slog_config *_slog_config;

            // ibv_mr *_log_mr;
            // ibv_mr *_tail_pointer_mr;
            // ibv_mr *_client_position_table_mr;
            // struct ibv_wc *_wc;
            // uint64_t _wr_id;

            RSlog _rslog;


            //silly variables
            int _entry_size;
            int _batch_size;
            int _bits_per_client_position;

    };
}

#endif