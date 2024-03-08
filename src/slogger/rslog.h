#ifndef RSLOG_H
#define RSLOG_H

#include "replicated_log.h"
#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"

#define MAX_CONCURRENT_MESSAGES 32

namespace slogger {

    class log_interface {
        public:
            virtual void FAA_Alocate(unsigned int entries) = 0;
            virtual void CAS_Allocate(unsigned int entries) = 0;
            virtual void RCAS_Position(uint64_t compare, uint64_t swap, uint64_t mask, uint64_t offset) = 0;
            virtual void Read_Tail_Pointer() = 0;
            virtual void Read_Client_Positions(bool block) = 0;
            virtual void Write_Log_Entries(uint64_t local_address, uint64_t size_bytes) = 0;
            virtual int Batch_Read_Log(uint64_t local_address, uint64_t entries) = 0;
            virtual void poll_one() = 0;
    };


    class RSlog: public log_interface{
        public:
            RSlog(){};
            RSlog(rdma_info remote_info, Replicated_Log * local_log, string name, int memory_server_index);
            void FAA_Alocate(unsigned int entries);
            void CAS_Allocate(unsigned int entries);
            void RCAS_Position(uint64_t compare, uint64_t swap, uint64_t mask, uint64_t offset);
            void Read_Tail_Pointer();
            void Read_Client_Positions(bool block);
            void Write_Log_Entries(uint64_t local_address, uint64_t size_bytes);
            int Batch_Read_Log(uint64_t local_address, uint64_t entries);
            void poll_one();
            uint64_t local_to_remote_log_address(uint64_t local_address);
            int get_memory_server_index(){return _memory_server_index;}
            int get_id(){return _local_log->get_id();}
        
        private:
            int _memory_server_index;
            Replicated_Log * _local_log;
            ibv_qp * _qp;
            ibv_pd *_protection_domain;
            struct ibv_cq * _completion_queue;
            ibv_mr *_log_mr;
            ibv_mr *_tail_pointer_mr;
            ibv_mr *_client_position_table_mr;
            ibv_mr *_scratch_memory_mr;
            struct ibv_wc *_wc;
            uint64_t _wr_id;
            string _name;
            slog_config *_slog_config;
    };

    class RSlogs: public log_interface {
        public:
            RSlogs(){};
            RSlogs(RSlog &rslog);
            RSlogs(vector<RSlog> rslogs);
            void Add_Slog(RSlog rslog);
            void FAA_Alocate(unsigned int entries);
            void CAS_Allocate(unsigned int entries);
            void RCAS_Position(uint64_t compare, uint64_t swap, uint64_t mask, uint64_t offset);
            void Read_Tail_Pointer();
            void Read_Client_Positions(bool block);
            void Write_Log_Entries(uint64_t local_address, uint64_t size_bytes);
            int Batch_Read_Log(uint64_t local_address, uint64_t entries);
            void poll_one();
            int poll_batch_read();
            int remote_server_count(){return _rslogs.size();}
            RSlog get_slog(int index){return _rslogs[index];}
        
        private:
            vector<RSlog> _rslogs;
            int get_read_machine();
            string _name;
    };        
}

#endif