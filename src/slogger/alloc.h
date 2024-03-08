#ifndef ALLOC_H
#define ALLOC_H

#include "slogger.h"
#include "replicated_log.h"

#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"
#include <jemalloc/jemalloc.h>

using namespace slogger;
using namespace rdma_helper;


#define MAX_EXTENTS 512
#define METADATA_ALLOCATION_SIZE 2097152 // 512 pages, seems to be what the metadata allocator uses.
#define MAX_ALLOCATIONS 1000000
#define PRE_ALLOC_SPACE 128
#define PRE_ALLOC_SIZE 1024

enum malloc_ops {
    mallocx_op,
    deallocx_op,
};

static const char *malloc_ops_str[] = {
    "mallocx_op",
    "deallocx_op",
};

typedef struct __attribute__ ((packed)) malloc_op_entry {
    enum malloc_ops type;
    void * ptr;
    size_t size;
    size_t alignment;
    size_t number;
    int flags;
    string toString() {
        return "malloc_op_entry: type: " + string(malloc_ops_str[type]) + " ptr: " + to_string((uint64_t)ptr) + " size: " + to_string(size) + " alignment: " + to_string(alignment) + " number: " + to_string(number) + " flags: " + to_string(flags);
    }
} malloc_op_entry;

class RMalloc : public SLogger {
    public:
        RMalloc(unordered_map<string,string> config);
        void Execute(malloc_op_entry op);
        void Apply_Ops();

        void *mallocx(size_t size, int flags);
        void deallocx(void *ptr, int flags);

        void fsm();
        void * my_hooks_alloc(extent_hooks_t *extent_hooks, void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit, unsigned arena_ind);

        vector<void *> _allocations;
        vector<size_t> _allocation_sizes;
        vector<ibv_mr *> _mrs;

    private:

        void Preallocate_Local_Buffers(int n, int size);
        float calculate_local_memory_usage();
        float calculate_remote_memory_usage();
        void zero_extent_metadata();
        void print_alloc_hook_args(void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit, unsigned arena_ind);
        void create_n_arenas(int n, extent_hooks_t *new_hooks);
        int test_0_make_n_allocations_then_frees_from_arena(int n, int thread);

        int _thread_to_arena_index[MAX_EXTENTS];
        int _local_allocs[MAX_EXTENTS];
        int _locally_allocated_memory[MAX_EXTENTS];
        uint64_t _remote_allocs[MAX_EXTENTS];
        uint64_t _remote_allocated_memory[MAX_EXTENTS];

        void *_jemalloc_metadata_start;
        void *_jemalloc_metadata_current;
        int _jemalloc_metadata_size;
        uint64_t _remote_start;
        uint64_t _remote_size;
        uint64_t _remote_current;

};

#endif