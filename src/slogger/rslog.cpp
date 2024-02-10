#include "rslog.h"
#include "replicated_log.h"
#include <assert.h>
#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"

using namespace rdma_helper;

namespace slogger {
    RSlog::RSlog(rdma_info info, Replicated_Log * local_log) {

        INFO("RSLog", "SLogger Initializing RDMA Structures");
        assert(info.qp != NULL);
        assert(info.completion_queue != NULL);
        assert(info.pd != NULL);

        _qp = info.qp;
        _completion_queue = info.completion_queue;
        _protection_domain = info.pd;

        _slog_config = memcached_get_slog_config();
        assert(_slog_config != NULL);
        assert(_slog_config->slog_size_bytes == local_log->get_log_size_bytes());
        INFO("RSlog","got a slog config from the memcached server and it seems to line up\n");

        SUCCESS("RSlog", "Set RDMA Structs from memcached server");
        // INFO(log_id(), "Registering table with RDMA device size %d, location %p\n", get_table_size_bytes(), get_table_pointer()[0]);
        _log_mr = rdma_buffer_register(_protection_domain, local_log->get_log_pointer(), local_log->get_log_size_bytes(), MEMORY_PERMISSION);
        // INFO(log_id(), "Registering lock table with RDMA device size %d, location %p\n", get_lock_table_size_bytes(), get_lock_table_pointer());
        _tail_pointer_mr = rdma_buffer_register(_protection_domain, local_log->get_tail_pointer_address(), local_log->get_tail_pointer_size_bytes(), MEMORY_PERMISSION);
        _client_position_table_mr = rdma_buffer_register(_protection_domain, local_log->get_client_positions_pointer(), local_log->get_client_positions_size_bytes(), MEMORY_PERMISSION);

        _wr_id = 10000000;
        _wc = (struct ibv_wc *) calloc (MAX_CONCURRENT_MESSAGES, sizeof(struct ibv_wc));
        _local_log = local_log;
        SUCCESS("RSlog", "Done registering memory regions for SLogger");
    }

    void RSlog::FAA_Alocate(unsigned int entries){
        // printf("FETCH AND ADD\n");
        uint64_t local_tail_pointer_address = (uint64_t) _local_log->get_tail_pointer_address();
        uint64_t remote_tail_pointer_address = _slog_config->tail_pointer_address;
        uint64_t add  = entries;

        rdmaFetchAndAddExp(
            _qp,
            local_tail_pointer_address,
            remote_tail_pointer_address,
            add,
            _tail_pointer_mr->lkey,
            _slog_config->tail_pointer_key,
            true,
            _wr_id);

        _wr_id++;

    }

    void RSlog::CAS_Allocate(unsigned int entries){
        uint64_t local_tail_pointer_address = (uint64_t) _local_log->get_tail_pointer_address();
        uint64_t remote_tail_pointer_address = _slog_config->tail_pointer_address;
        uint64_t compare  = _local_log->get_tail_pointer();
        uint64_t new_tail_pointer = compare + entries;

        //Send out a request for client positions along with the fetch and add
        //They are two seperate requests
        //We can batch together for a bit better latency.
        // Read_Client_Positions(false);
        rdmaCompareAndSwapExp(
            _qp,
            local_tail_pointer_address,
            remote_tail_pointer_address,
            compare,
            new_tail_pointer,
            _tail_pointer_mr->lkey,
            _slog_config->tail_pointer_key,
            true,
            _wr_id);

        _wr_id++;

    }
    void RSlog::RCAS_Position(uint64_t compare, uint64_t swap, uint64_t mask, uint64_t offset){
        uint64_t local_address = (uint64_t) _local_log->get_client_positions_pointer() + offset;
        uint64_t remote_address = _slog_config->client_position_table_address + offset;

        bool success = rdmaCompareAndSwapMask(
            _qp,
            local_address,
            remote_address,
            compare,
            swap,
            _client_position_table_mr->lkey,
            _slog_config->client_position_table_key,
            mask,
            true,
            _wr_id);
        _wr_id++;
    }

    void RSlog::Read_Tail_Pointer() {
        uint64_t local_tail_pointer_address = (uint64_t) _local_log->get_tail_pointer_address();
        uint64_t remote_tail_pointer_address = _slog_config->tail_pointer_address;
        uint64_t size = sizeof(uint64_t);
        rdmaReadExp(
            _qp,
            local_tail_pointer_address,
            remote_tail_pointer_address,
            size,
            _log_mr->lkey,
            _slog_config->slog_key,
            true,
            _wr_id);

        _wr_id++;
    }

    void RSlog::Read_Client_Positions(bool block) {

        uint64_t local_address = (uint64_t) _local_log->get_client_positions_pointer();
        uint64_t remote_address = _slog_config->client_position_table_address;
        uint64_t size = _local_log->get_client_positions_size_bytes();
        rdmaReadExp(
            _qp,
            local_address,
            remote_address,
            size,
            _client_position_table_mr->lkey,
            _slog_config->client_position_table_key,
            block,
            _wr_id);

        _wr_id++;
    }

    void RSlog::Write_Log_Entries(uint64_t local_address, uint64_t entries) {
        rdmaWriteExp(
            _qp,
            local_address,
            local_to_remote_log_address(local_address),
            entries * _local_log->get_entry_size_bytes(),
            _log_mr->lkey,
            _slog_config->slog_key,
            -1,
            true,
            _wr_id);
        _wr_id++;
    }

    int RSlog::Batch_Read_Log(uint64_t local_address, uint64_t entries) {
        #define MTU_SIZE 1024
        #define RDMA_READ_OVERHEAD 64 //#eth 12+4, #ip 20 # udp 8# beth 12 #aeth 4 #icrc 4 = 64
        #define MAX_READ_BATCH 128
        struct ibv_sge sg [MAX_READ_BATCH];
        struct ibv_exp_send_wr wr [MAX_READ_BATCH];


        int max_size_read = (MTU_SIZE - RDMA_READ_OVERHEAD);
        int max_entries_per_read = max_size_read / _local_log->get_entry_size_bytes();
        uint64_t remote_log_tail_address = local_to_remote_log_address(local_address);
        int i=0;
        int entries_left_to_read=0;

        //Resize the number of entries we are going to read based on the max batch size
        if (entries > max_entries_per_read * MAX_READ_BATCH) {
            entries = max_entries_per_read * MAX_READ_BATCH;
        }

        entries_left_to_read = entries;
        while (entries_left_to_read > 0) {
            //Calculate how many entries will be read in this batch
            int entries_to_read = entries_left_to_read;
            if (entries_to_read > max_entries_per_read) {
                entries_to_read = max_entries_per_read;
            }
            entries_left_to_read -= entries_to_read;

            uint64_t entry_offset = (i * max_entries_per_read * _local_log->get_entry_size_bytes());
            setRdmaReadExp(
                &sg[i],
                &wr[i],
                local_address + entry_offset,
                remote_log_tail_address + entry_offset,
                entries_to_read * _local_log->get_entry_size_bytes(),
                _log_mr->lkey,
                _slog_config->slog_key,
                false,
                _wr_id);
            _wr_id++;
            i++;
        }
        wr[i-1].exp_send_flags = IBV_EXP_SEND_SIGNALED;
        send_bulk(i,_qp, wr);

        return entries;

    }
    

    void RSlog::poll_one() {
        int outstanding_messages = 1;
        int n = bulk_poll(_completion_queue, outstanding_messages, _wc);
        if (n < 0) {
            ALERT("SLOG", "Error polling completion queue");
            exit(1);
        }
    }

    uint64_t RSlog::local_to_remote_log_address(uint64_t local_address) {
        uint64_t base_address = (uint64_t) _local_log->get_log_pointer();
        uint64_t address_offset = local_address - base_address;
        return _slog_config->slog_address + address_offset;
    }


}