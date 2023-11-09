#include "nt.h"
#include "slogger.h"
#include "replicated_log.h"

#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"


using namespace replicated_log;
using namespace slogger;
using namespace rdma_helper;


#define RANDOM_NFIND_TEST_KEYMASK 63
#define RANDOM_NFIND_TEST_ITEMS ((RANDOM_NFIND_TEST_KEYMASK+1)/3)
#define ITERATIONS (1<<16)

/* Include the Mersenne twister */
#if !defined(__cplusplus_cli) && (defined(_M_X64) || defined(__x86_64__) || (defined(_M_IX86) && _M_IX86_FP>=2) || (defined(__i386__) && defined(__SSE2__)))
#define HAVE_SSE2 1
#endif
#define MEXP 19937
#include "../nedtries/SFMT.c"

size_t fookeyfunct(const foo_t *r)
{
  return r->key;
}

NEDTRIE_GENERATE(static, foo_tree_s, foo_s, link, fookeyfunct, NEDTRIE_NOBBLEZEROS(foo_tree_s))


namespace nt {

    NT::NT(unordered_map<string,string> config) : SLogger(config) {
        ALERT("NT", "NT constructor called");
        // _replicated_log = Replicated_Log();
        NEDTRIE_INIT(&footree);
    }

    void NT::put() {
        foo_t a;
        a.key=2;
        NEDTRIE_INSERT(foo_tree_s, &footree, &a);


        #ifdef MEASURE_ESSENTIAL
        uint64_t latency = (_operation_end_time - _operation_start_time).count();
        _completed_insert_count++;
        _current_insert_rtt = 0;
        _sum_insert_latency_ns += latency;
            #ifdef MEASURE_MOST
            _insert_rtt.push_back(_current_insert_rtt);
            _insert_latency_ns.push_back(latency);
            #endif
        #endif
    }

    void NT::get() {
        foo_t b, *r;
        b.key=6;
        NEDTRIE_INSERT(foo_tree_s, &footree, &b);
        r=NEDTRIE_FIND(foo_tree_s, &footree, &b);
        assert(r==&b);

    }

    void NT::del() {
        foo_t a;
        a.key=2;
        NEDTRIE_REMOVE(foo_tree_s, &footree, &a);

    }

    void NT::fsm() {
        ALERT("NT", "NT fsm called");
        // foo_t a, b, c, *r;
        // ALERT(log_id(), "SLogger Starting FSM");
        while(!*_global_start_flag){
            INFO(log_id(), "not globally started");
        };

        int i=0;
        while(!*_global_end_flag){
            Request next_request;
            

            //Pause goes here rather than anywhere else because I don't want
            //To have locks, or any outstanding requests
            if (*_global_prime_flag && !_local_prime_flag){
                _local_prime_flag = true;
                clear_statistics();
            }

            next_request = _workload_driver.next();
            _operation_start_time = get_current_ns();
            // INFO(log_id(), "SLogger FSM iteration %d\n", i);
            // sleep(1);
            if (next_request.op == PUT) {
                put();
                //do a put thing
            } else if (next_request.op == GET) {
                get();
                //do a get thing
            } else if (next_request.op == DELETE) {
                del();
            } else {
                ALERT("FUCK", "Exit because of a bad operation");
                exit(1);
            }

            _operation_end_time = get_current_ns();

        }



        printf("General workout of the C API ...\n");
        // c.key=5;
        // r=NEDTRIE_NFIND(foo_tree_s, &footree, &c);
        // assert(r==&b); /* NFIND finds next largest. Invert the key function (i.e. 1-key) to find next smallest. */
        // NEDTRIE_FOREACH(r, foo_tree_s, &footree)
        // {
        //     printf("%p, %u\n", (void *) r, (unsigned) r->key);
        // }
        // assert(!NEDTRIE_PREV(foo_tree_s, &footree, &b));
        // assert(!NEDTRIE_NEXT(foo_tree_s, &footree, &b));
    }
}