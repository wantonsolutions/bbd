#include "../slib/state_machines.h"
#include "slogger.h"

namespace slogger {

    void SLogger::faa_alloc_stats(){
        #ifdef MEASURE_ESSENTIAL
        uint64_t request_size = RDMA_FAA_REQUEST_SIZE + RDMA_FAA_RESPONSE_SIZE;
        _faa_bytes += request_size;
        _total_bytes += request_size;
        _insert_operation_bytes += request_size;
        _current_insert_rtt++;
        _insert_rtt_count++;
        _total_faa++;
        #endif
    }

    void SLogger::cas_alloc_stats(bool allocated){
        #ifdef MEASURE_ESSENTIAL
        uint64_t request_size = RDMA_CAS_REQUEST_SIZE + RDMA_CAS_RESPONSE_SIZE;
        _cas_bytes += request_size;
        _total_bytes += request_size;
        _insert_operation_bytes += request_size;
        _current_insert_rtt++;
        _insert_rtt_count++;
        _total_cas++;
        if(!allocated) {
            _total_cas_failures++;
        }
        #endif
    }

    void SLogger::read_tail_stats(){
        #ifdef MEASURE_ESSENTIAL
        uint64_t request_size = sizeof(uint64_t) + RDMA_READ_REQUSET_SIZE + RDMA_READ_RESPONSE_BASE_SIZE;
        _read_bytes += request_size;
        _total_bytes += request_size;
        _read_operation_bytes += request_size;
        _current_read_rtt++;
        _read_rtt_count++;
        _total_reads++;
        #endif
    }

    void SLogger::read_position_stats(int position_size) {
        #ifdef MEASURE_ESSENTIAL
        uint64_t request_size = position_size + RDMA_READ_REQUSET_SIZE + RDMA_READ_RESPONSE_BASE_SIZE;
        _read_bytes += request_size;
        _total_bytes += request_size;
        _read_operation_bytes += request_size;
        _current_read_rtt++;
        _read_rtt_count++;
        _total_reads++;
        #endif
    }

    void SLogger::write_log_entries_stats(uint64_t size){
        #ifdef MEASURE_ESSENTIAL
        uint64_t request_size = size + RDMA_WRITE_REQUEST_BASE_SIZE + RDMA_WRITE_RESPONSE_SIZE;
        _cas_bytes += request_size;
        _total_bytes += request_size;
        _insert_operation_bytes += request_size;
        _current_insert_rtt++;
        _insert_rtt_count++;
        _total_writes++;
        #endif
    }
}

