// #include <iostream>
#include "jemalloc/jemalloc.h"
#include <errno.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jemalloc/jemalloc.h>
// #include "jemalloc_util.h"
#include <sys/mman.h>
#include <string>
#include "alloc.h"

#include <mutex>
std::mutex jemalloc_start_mutex;

#include "../slib/log.h"

using namespace std;


static RMalloc * GlobalMalloc[128];
static extent_hooks_t * Original_Hooks[128];
const int size_2MB = 2097152;

void * Global_Alloc_Hook(extent_hooks_t *extent_hooks, void *new_addr, size_t size,
		size_t alignment, bool *zero, bool *commit, unsigned arena_ind) {
            printf("In Global_Alloc_Hook\n");
            assert(GlobalMalloc[arena_ind] != NULL);
            return GlobalMalloc[arena_ind]->my_hooks_alloc(extent_hooks, new_addr, size, alignment, zero, commit, arena_ind);
        }

extent_hooks_t hooks = {Global_Alloc_Hook,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};

void RMalloc::write_x_for_n_bytes_to_remote_buffer(int local_index, void* remote, size_t size, uint8_t value) {
    // ALERT("RMalloc", "Writing %d bytes to remote buffer", size);

    assert(local_index < _allocations.size());
    assert(size <= _allocation_sizes[local_index]);
    void * local = _allocations[local_index];
    ibv_mr * mr = _mrs[local_index];
    int remote_log_index = 0;

    //Write value to the local buffer for size bytes
    memset(local, value, size);
    rdma_info ri = get_remote_info()[remote_log_index];
    int write_x_wrid = 50;
    rdmaWriteExp(ri.qp,
        (uint64_t)local,
        (uint64_t)remote,
        size,
        mr->rkey,
        _slog_config->scratch_memory_key,
        -1,
        true,
        write_x_wrid);

    //Wait for the write to complete
    RSlog rslog = _rslogs.get_slog(remote_log_index);
    rslog.poll_one();

}

void RMalloc::read_x_for_n_bytes_from_remote_buffer(int local_index, void * remote, size_t size, uint8_t value){
    // ALERT("RMalloc", "Reading %d bytes from remote buffer", size);
    assert(local_index < _allocations.size());
    assert(size <= _allocation_sizes[local_index]);
    void * local = _allocations[local_index];
    ibv_mr * mr = _mrs[local_index];
    int remote_log_index = 0;

    rdma_info ri = get_remote_info()[remote_log_index];
    int read_x_wrid = 51;
    rdmaReadExp(ri.qp,
        (uint64_t)local,
        (uint64_t)remote,
        size,
        mr->rkey,
        _slog_config->scratch_memory_key,
        true,
        read_x_wrid);

    //Wait for the read to complete
    RSlog rslog = _rslogs.get_slog(remote_log_index);
    rslog.poll_one();

    //Check that the local buffer has the value
    // ALERT(log_id(), "Checking that the local buffer has the value %d", value);
    for (int i=0;i<size;i++) {
        // printf("%d ", ((uint8_t *)local)[i]);
        assert(((uint8_t *)local)[i] == value);
    }
    // printf("\n");

}


void RMalloc::fsm() {

    ALERT("RMalloc", "FSM");
    Preallocate_Local_Buffers(PRE_ALLOC_SPACE, PRE_ALLOC_SIZE);
    // const int itterations = PRE_ALLOC_SPACE;
    const int itterations = 100;
    const int alloc_size_base = 128;
    int alloc_size;
    int base_level_areans = 1;
    int thread_arena = _id;
    void * ptrs[PRE_ALLOC_SPACE];
    // printf("allocing on thread arena %d\n", thread_arena);
    int arena_index = MALLOCX_ARENA(_thread_to_arena_index[_id]) | MALLOCX_TCACHE_NONE;
    for (int i=0;i<itterations-1;i++) {
        // alloc_size = alloc_size_base + i;
        alloc_size = alloc_size_base;
        ptrs[i] =  mallocx(alloc_size, arena_index);
        ALERT(log_id(), "Mallocx:  %p %d [arena %d]",ptrs[i], alloc_size,  arena_index);
        //Zero out local buffer
        memset(_allocations[i], 0, alloc_size);
        write_x_for_n_bytes_to_remote_buffer(i, ptrs[i],alloc_size, i);
        memset(_allocations[i], 0, alloc_size);
        read_x_for_n_bytes_from_remote_buffer(i, ptrs[i], alloc_size, i);
    }
    // ALERT(log_id(), "Done Allocations, begginning free");
    for (int i=0;i<itterations-1;i++) {
        ALERT(log_id(), "Deallocx: %p size %d [arenas %d]", ptrs[i], alloc_size_base + i, arena_index);
        deallocx(ptrs[i], arena_index);
    }

    ALERT(log_id(),"Done with FSM sleeping...");
    sleep(5);
    ALERT(log_id(),"Done sleeping going to exit now %d", rand());


}

void RMalloc::Apply_Ops() {
    // ALERT("RMalloc", "Applying Ops");
    // _replicated_log.Print_All_Entries();
    // assert(GlobalMalloc != NULL);
    malloc_op_entry *op;
    malloc_op_entry *peek_op;
    // if (Peek_Next_Operation() == NULL) {
    //     return;
    // }
    // op = (malloc_op_entry *)Next_Operation();
    int arena_index = MALLOCX_ARENA(_thread_to_arena_index[_id]) | MALLOCX_TCACHE_NONE;
    while ((peek_op = (malloc_op_entry *)Peek_Next_Operation()) != NULL) {
        op = (malloc_op_entry *)Next_Operation();
        peek_op = (malloc_op_entry *)Peek_Next_Operation();
        if (peek_op == NULL) {
            break;
        }
        // if (peek_op == NULL) {
        //     printf("OP %s\nNext op NULL", op->toString().c_str());
        // } else {
        //     printf("OP %s\nNext op %s", op->toString().c_str(), peek_op->toString().c_str());
        // }
        if (op->type == mallocx_op) {
            // ALERT("RMalloc", "Applying mallocx");

            // je_mallocx(op->size, op->flags);
            void * ptr = je_mallocx(op->size, arena_index);
            ALERT(log_id(), "Mallocx:  %p %d [arena %d] (apply)",ptr, op->size,  arena_index);

        } else if (op->type == deallocx_op) {
            // ALERT("RMalloc", "Applying Other deallocx");
            ALERT(log_id(), "Deallocx: %p [arena %d] (apply)", op->ptr, arena_index);
            // je_dallocx(op->ptr, op->flags);
            je_dallocx(op->ptr, arena_index);
        }
    }
    // printf("Done Applying Returning the last opeation %s\n", op->toString().c_str());
}

void RMalloc::Execute(malloc_op_entry op) {
    Write_Operation(&op, sizeof(op));
    Sync_To_Last_Write();
    Apply_Ops();
}

void *RMalloc::mallocx(size_t size, int flags) {
    Execute({mallocx_op, NULL, size, 0, 0, flags});
    return je_mallocx(size, flags);
}

void RMalloc::deallocx(void *ptr, int flags) {
    Execute({deallocx_op, ptr, 0, 0, 0, flags});
    return je_dallocx(ptr, flags);
}

void RMalloc::Preallocate_Local_Buffers(int n, int size) {
    ALERT("RMalloc", "Preallocating %d buffers of size %d", n, size);
    vector<rdma_info> rinfos = get_remote_info();
    ALERT("RMalloc",
    "I don't think that it's the best idea to preallocate local buffers this way. We need to register a lot of memory, and it's not obvious that it's helpful to do it this way"
    "I think a better way is to reuse jemalloc to set up an rdma mapped region. When a client wants to allocate a remote location they can call a version of malloc that gives them both a remote, and a local pointer to a mapped region"
    "To do this I need to do basically what I've done so far with je_malloc, except that I should also use a locally mapped region for one of the allocations."
    "When the caller calls RMALLLOC they will get both a local and a remote pointer back");
    
    ALERT("MALLOC", "TODO currently only using a single memory server, spread it out");
    rdma_info rinfo = rinfos[0];
    for (int i=0;i<n;i++) {
        void * ptr = malloc(size);
        ibv_mr * mr = rdma_buffer_register(rinfo.pd, ptr, size, MEMORY_PERMISSION);
        _mrs.push_back(mr);
        _allocations.push_back(ptr);
        _allocation_sizes.push_back(size);
    }
}

RMalloc::RMalloc(unordered_map<string,string> config) : SLogger(config) {
    ALERT("RMalloc", "Initalizing RMalloc");
	size_t ret, sz;
	unsigned arena_ind = -1;
	extent_hooks_t *new_hooks;
	size_t hooks_len, memsize = 4096 * 4096 * 64;
	_jemalloc_metadata_start = mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (!_jemalloc_metadata_start) {
		perror("Could not pre-allocate memory");
		exit(1);
	}
    _jemalloc_metadata_current = _jemalloc_metadata_start;
    _jemalloc_metadata_size = memsize;

    //TODO - This is a hack to get scratch space from the server. We have multiple servers
    //TODO - so we should be splitting up requests across all of them
    ALERT("TODO", "DO A BETTER JOB OF GETTING REMOTE ALLOC SPACE SET UP");
    string name = config["name"];
    _slog_config = memcached_get_slog_config(name, 0);
    _remote_start = _slog_config->scratch_memory_address;
    _remote_size = _slog_config->scratch_memory_size_bytes;
    _remote_current = _remote_start;

    ALERT("RMalloc", "Remote Start: [%p,%llu], Remote Size: %lu", _remote_start,_remote_start, _remote_size);

    // void * (RMalloc::*new_hooks_alloc)(extent_hooks_t *extent_hooks, void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit, unsigned arena_ind);

    //Create a pointer to the alloc hook
    // void * (*new_hooks_alloc)(extent_hooks_t *extent_hooks, void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit, unsigned arena_ind);
    //Assign a typecasted version of our internal function
    // new_hooks_alloc = (void *(*) (extent_hooks_t *extent_hooks, void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit, unsigned arena_ind))&RMalloc::my_hooks_alloc;
    //create a new extent_hooks_t struct using our local function 

    zero_extent_metadata();
    // int threads=2;
    create_arena(&hooks);
}


void RMalloc::zero_extent_metadata() {
    for (int i = 0; i < MAX_EXTENTS; i++) {
        _local_allocs[i] = 0;
        _locally_allocated_memory[i] = 0;
        _remote_allocs[i] = 0;
        _remote_allocated_memory[i] = 0;
    }
}

float RMalloc::calculate_remote_memory_usage() {
    return float(_remote_current - _remote_start) / float(_remote_size);
}


float RMalloc::calculate_local_memory_usage() {
    return float((uint64_t)_jemalloc_metadata_current - (uint64_t)_jemalloc_metadata_start) / float(_jemalloc_metadata_size);
}

// template <typename T, typename U>
// static inline T align_up(T val, U alignment) {
//     assert((alignment & (alignment - 1)) == 0);
//     T return_val = (val + alignment - 1) & ~(alignment - 1);
//     assert((uintptr_t)return_val & (alignment - 1));
//     return return_val;
// }

// template <typename T, typename U>
// static inline T align(T val, U alignment) {
//     //This function aligns the value to the nearest multiple of the alignment
//     return (val + alignment - 1) & ~(alignment - 1);
// }

template <typename T>
inline T align_to_power_of_2(T ptr, size_t alignment) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t mask = alignment - 1;
    if (alignment && (alignment & mask) == 0) {
        return reinterpret_cast<T>((addr + mask) & ~mask);
    }
    // Invalid alignment, return original pointer
    return ptr;
}

void RMalloc::print_alloc_hook_args(void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit, unsigned arena_ind) {
    printf("In wrapper alloc_hook: new_addr:%p "
        "size:%lu(%lu pages) alignment:%lu "
        "zero:%s commit:%s arena_ind:%u\n",
        new_addr, size, size / 4096, alignment,
        (*zero) ? "true" : "false",
        (*commit) ? "true" : "false",
        arena_ind);
}

void * RMalloc::my_hooks_alloc(extent_hooks_t *extent_hooks, void *new_addr, size_t size,
		size_t alignment, bool *zero, bool *commit, unsigned arena_ind)
{

    // print_alloc_hook_args(new_addr, size, alignment, zero, commit, arena_ind);    
    ALERT(log_id(), "In my_hooks_alloc: arena %d, size %d", arena_ind, size);
    assert(arena_ind < MAX_EXTENTS);
    void *ret = NULL;

    if (new_addr != NULL) {
        printf("WARNING: new_addr is not supported in remote allocations\n");
        return NULL;
    }

    print_alloc_hook_args(new_addr, size, alignment, zero, commit, arena_ind);

    //We assume that all requests for remote memory are not zeroed.  This is not an explicitly safe
    //assumption. However, we need a method for determining if a request is for remote memory.
    //jemalloc makes internal calls to alloc and we need to be able to distinguish between these
    //calls and calls from the user.
    // if (!*zero) {
    if (size != size_2MB) {
        _remote_allocs[arena_ind]+=1;
        // _remote_current = align_up((uint64_t)_remote_current + size, alignment);
        _remote_current = align_to_power_of_2(_remote_current + size, alignment);
        //Track the amount of memory we have allocated
        ret = (void *)_remote_current;

        if (_remote_current >= _remote_start + _remote_size) {
            printf("💀DEATH💀 Not enough remote memory failing %s:%s\n",__FILE__,__LINE__);
            exit(0);
        }
        // if (*commit){
        //     printf("WARNING: Commit is not supported in remote allocations\n");
        // }

    } else {
        return Original_Hooks[arena_ind]->alloc(extent_hooks, new_addr, size, alignment, zero, commit, arena_ind);

        // if (!*commit) {
        //     return NULL;
        // }

        // //There is strange behavior here, we continue to get allocations of 512 bytes and it's hard
        // //for me to understand why.  Each allocation is 512 pages, and we quickly allocate GB of
        // //memory. I have no intention of allowing JEMalloc to sabotage me so I'm just ignoring these
        // //allocations.  I'm sure that there is a way to prevent these allocations from running. But
        // //as of March 6th 2024 I have not found it - Stew
        // _local_allocs[arena_ind]+=1;
        // #define REASONABLE_NUMBER_OF_LOCAL_ALLOCS 128
        // if (_local_allocs[arena_ind] > REASONABLE_NUMBER_OF_LOCAL_ALLOCS){
        //     printf("WARNING: We are making a lot of local allocations, this should not be the case for a remote allocator. Consider debugging\n");
        //     printf("WARNING: At the time of writing we are catching allocations of 512 and assuming they are metadata allocations.");
        //     printf("This is not a good assumption. We should be able to catch all allocations and forward them to the remote allocator. This is a bug.\n");
        //     assert(_local_allocs[arena_ind] <= REASONABLE_NUMBER_OF_LOCAL_ALLOCS);
            
        // }
        // printf("Local  [%d] %d allocs %d bytes\t fill %2.2f\% \n",  arena_ind, _local_allocs[arena_ind], _locally_allocated_memory[arena_ind], calculate_local_memory_usage());
        // printf("Remote [%d] %d allocs %lu bytes\t fill %2.2f\% \n", arena_ind, _remote_allocs[arena_ind], _remote_allocated_memory[arena_ind], calculate_remote_memory_usage());
        // // printf("Allocating Local. Arena:%d Allocation Count %d\n", arena_ind, local_allocs[arena_ind]);

        // if (ret >= _jemalloc_metadata_start + _jemalloc_metadata_size) {
        //     printf("> Not enough memory\n");
        //     return NULL;
        // }

        // // _jemalloc_metadata_current = (char *)align_up((uint64_t)_jemalloc_metadata_current, alignment);
        // _jemalloc_metadata_current = (char *)align_to_power_of_2(_jemalloc_metadata_current + size, alignment);
        // ret = _jemalloc_metadata_current;


        // if (*zero){
        //     memset(ret, size, 0);
        // }
    }
    return ret;
}


//Each thread gets it's own arena. However I need a pointer to the hook for each thread which
//does not exist untill we finish running.  I need to predict what the index of the next arena
//will be. I can do this by globally locking, checking how many areans exist, and then setting
//the global malloc index to the next index. This is a hack, but it's the best I can do for now.
void RMalloc::create_arena(extent_hooks_t *new_hooks) {

    jemalloc_start_mutex.lock();
    ALERT("Create Areana", "We have aquired the arena lock");

    //Get the number of current arenas.
    unsigned n_arenas{0};
    size_t sz = sizeof(n_arenas);
    int err = je_mallctl("arenas.narenas", (void *)&n_arenas, &sz, nullptr, 0);
    ALERT("Create Arena", "There are currently n arenas: %u\n", n_arenas);

    //Set the global alloc hook for this thread
    GlobalMalloc[n_arenas]=this;
    _thread_to_arena_index[_id] = n_arenas;

    //Actually create the areana using the new hook.
    // int ret = je_mallctl("arenas.create", (void *)&n_arenas, &sz, (void *)&new_hooks, sizeof(extent_hooks_t *));
    int ret = je_mallctl("arenas.create", (void *)&n_arenas, &sz, NULL, 0);
    if (ret) {
        printf("mallctl error creating arena with new hooks\n");
        exit(1);
    }

    assert(n_arenas == _thread_to_arena_index[_id]);

    //At this point we have created the arena. We now need to go and get the original hook
    extent_hooks_t *original_hooks;
    sz = sizeof(extent_hooks_t *);
    string hook_name = "arena." + to_string(n_arenas) + ".extent_hooks";
    printf("Hook name: %s\n", hook_name.c_str());
    err = je_mallctl(hook_name.c_str(), (void *)&original_hooks, &sz, NULL, 0);
    if (err) {
        printf("mallctl error getting original hooks\n");
        exit(1);
    }
    assert(original_hooks);
    Original_Hooks[n_arenas] = original_hooks;

    ALERT("Create Arena", "Original hooks: %p New hook %p", original_hooks, new_hooks);
    err = je_mallctl(hook_name.c_str(), NULL, 0, (void *)&new_hooks, sizeof(extent_hooks_t *));
    if (err) {
        printf("mallctl error setting new hooks\n");
        exit(1);
    }


    jemalloc_start_mutex.unlock();
}