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

#include "../slib/log.h"

using namespace std;


RMalloc * GlobalMalloc = NULL;
void * Global_Alloc_Hook(extent_hooks_t *extent_hooks, void *new_addr, size_t size,
		size_t alignment, bool *zero, bool *commit, unsigned arena_ind) {
            assert(GlobalMalloc != NULL);
            return GlobalMalloc->my_hooks_alloc(extent_hooks, new_addr, size, alignment, zero, commit, arena_ind);
        }


RMalloc::RMalloc(unordered_map<string,string> config) : SLogger(config) {
    ALERT("RMalloc", "Initalizing RMalloc");
    // _replicated_log = Replicated_Log();


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

    // void * (RMalloc::*new_hooks_alloc)(extent_hooks_t *extent_hooks, void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit, unsigned arena_ind);

    //Create a pointer to the alloc hook
    // void * (*new_hooks_alloc)(extent_hooks_t *extent_hooks, void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit, unsigned arena_ind);
    //Assign a typecasted version of our internal function
    // new_hooks_alloc = (void *(*) (extent_hooks_t *extent_hooks, void *new_addr, size_t size, size_t alignment, bool *zero, bool *commit, unsigned arena_ind))&RMalloc::my_hooks_alloc;
    //create a new extent_hooks_t struct using our local function 
    GlobalMalloc = this;
    extent_hooks_t hooks = {Global_Alloc_Hook,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};

    zero_extent_metadata();
    int threads=1;
    create_n_arenas(threads, &hooks);

    unsigned n_arenas{0};
    sz = sizeof(n_arenas);
    int err = je_mallctl("opt.narenas", (void *)&n_arenas, &sz, nullptr, 0);
    printf("narenas: %u\n", n_arenas);
    if (err) {
        printf("mallctl error getting narenas\n");
        exit(1);
    }


	printf("----------------------------------------------\n");
    int allocs = 50;
    test_0_make_n_allocations_then_frees_from_arena(allocs, 0);
    for (int i=0;i<5;i++)printf("----------------------------------------------\n");
    test_0_make_n_allocations_then_frees_from_arena(allocs, 0);

    printf("we finished initing the RMALLOC\n");
    exit(0);
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

template <typename T, typename U>
static inline T align_up(T val, U alignment) {
    assert((alignment & (alignment - 1)) == 0);
    return (val + alignment - 1) & ~(alignment - 1);
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
    ALERT("RMalloc", "In my_hooks_alloc: arena %d, size %d", arena_ind, size);
    assert(arena_ind < MAX_EXTENTS);
    void *ret;

    //We assume that all requests for remote memory are not zeroed.  This is not an explicitly safe
    //assumption. However, we need a method for determining if a request is for remote memory.
    //jemalloc makes internal calls to alloc and we need to be able to distinguish between these
    //calls and calls from the user.
    if (!*zero) {
        _remote_allocs[arena_ind]+=1;
        _remote_current = align_up((uint64_t)_remote_current + size, alignment);
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
        if (!*commit) {
            return NULL;
        }
        print_alloc_hook_args(new_addr, size, alignment, zero, commit, arena_ind);

        //There is strange behavior here, we continue to get allocations of 512 bytes and it's hard
        //for me to understand why.  Each allocation is 512 pages, and we quickly allocate GB of
        //memory. I have no intention of allowing JEMalloc to sabotage me so I'm just ignoring these
        //allocations.  I'm sure that there is a way to prevent these allocations from running. But
        //as of March 6th 2024 I have not found it - Stew
        _local_allocs[arena_ind]+=1;
        #define REASONABLE_NUMBER_OF_LOCAL_ALLOCS 128
        if (_local_allocs[arena_ind] > REASONABLE_NUMBER_OF_LOCAL_ALLOCS){
            printf("WARNING: We are making a lot of local allocations, this should not be the case for a remote allocator. Consider debugging\n");
            printf("WARNING: At the time of writing we are catching allocations of 512 and assuming they are metadata allocations.");
            printf("This is not a good assumption. We should be able to catch all allocations and forward them to the remote allocator. This is a bug.\n");
            assert(_local_allocs[arena_ind] <= REASONABLE_NUMBER_OF_LOCAL_ALLOCS);
            
        }
        printf("Local  [%d] %d allocs %d bytes\t fill %2.2f\% \n",  arena_ind, _local_allocs[arena_ind], _locally_allocated_memory[arena_ind], calculate_local_memory_usage());
        printf("Remote [%d] %d allocs %lu bytes\t fill %2.2f\% \n", arena_ind, _remote_allocs[arena_ind], _remote_allocated_memory[arena_ind], calculate_remote_memory_usage());
        // printf("Allocating Local. Arena:%d Allocation Count %d\n", arena_ind, local_allocs[arena_ind]);

        _jemalloc_metadata_current = (char *)align_up((uint64_t)_jemalloc_metadata_current, alignment);
        ret = _jemalloc_metadata_current;

        if (ret >= _jemalloc_metadata_start + _jemalloc_metadata_size) {
            printf("> Not enough memory\n");
            return NULL;
        }

        if (*zero){
            memset(ret, size, 0);
        }
    }
    return ret;
}


void RMalloc::create_n_arenas(int n, extent_hooks_t *new_hooks) {
	printf("Creating %d arenas\n", n);
    assert(n <= MAX_EXTENTS);
    unsigned arena_ind;
	size_t sz = sizeof(arena_ind);

    arena_ind =0;
    for (int i=0;i<n;i++) {
        int thread_index = i;
        arena_ind++;
        ALERT("RMalloc", "Creating arena %d, on thread %d", arena_ind, thread_index);
        int ret = je_mallctl("arenas.create", (void *)&arena_ind, &sz, (void *)&new_hooks, sizeof(extent_hooks_t *));
        _thread_to_arena_index[thread_index] = arena_ind;
        printf("Thread %d -> Arena %d\n", thread_index, arena_ind);
        if (ret) {
            printf("mallctl error creating arena with new hooks\n");
            exit(1);
        }
    }
}

int RMalloc::test_0_make_n_allocations_then_frees_from_arena(int n, int thread){
    assert(n <= MAX_ALLOCATIONS);
    void *allocs[MAX_ALLOCATIONS];

    int arena = _thread_to_arena_index[thread];
    printf("Allocating on thread %d from arena %d\n", thread, arena);
    for (int i=0;i<n;i++) {
        allocs[i] = je_mallocx(1024, MALLOCX_ARENA(arena) | MALLOCX_TCACHE_NONE);
        if (!allocs[i]) {
            printf("mallocx error\n");
            exit(1);
        }
    }
    for (int i=0;i<n;i++) {
        je_dallocx(allocs[i], MALLOCX_ARENA(arena));
    }
}

// int main(int argc, char *argv[])
// {
// 	size_t ret, sz;
// 	unsigned arena_ind = -1;
// 	extent_hooks_t *new_hooks;
// 	size_t hooks_len, memsize = 4096 * 4096 * 64;
// 	pre_alloc = mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
// 	if (!pre_alloc) {
// 		perror("Could not pre-allocate memory");
// 		exit(1);
// 	}
// 	pre_alloc_end = pre_alloc + memsize;
// 	new_hooks = &hooks;

//     zero_extent_metadata();
//     int threads=1;
//     create_n_arenas(threads, new_hooks);

//     unsigned n_arenas{0};
//     sz = sizeof(n_arenas);
//     int err = je_mallctl("opt.narenas", (void *)&n_arenas, &sz, nullptr, 0);
//     printf("narenas: %u\n", n_arenas);
//     if (err) {
//         printf("mallctl error getting narenas\n");
//         exit(1);
//     }


// 	printf("----------------------------------------------\n");
//     int allocs = MAX_ALLOCATIONS;
//     test_0_make_n_allocations_then_frees_from_arena(allocs, 0);
//     for (int i=0;i<5;i++)printf("----------------------------------------------\n");
//     test_0_make_n_allocations_then_frees_from_arena(allocs, 0);
//     return 0;
// }