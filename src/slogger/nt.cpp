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

    void NT::Apply_Ops() {
        nt_op_entry * op = (nt_op_entry *) Next_Operation();
        while(op != NULL) {
            foo_t v;
            v.key = op->key;
            //note we should not have non modifying operations in the log
            switch(op->type) {
                case NT_PUT:
                    // ALERT(log_id(), "NT Apply_Ops called writing %d", op->key);
                    NEDTRIE_INSERT(foo_tree_s, &footree, &v);
                    break;
                // case NT_GET:
                //     ALERT(log_id(), "NT Apply_Ops called reading %d", op->key);
                //     NEDTRIE_CFIND(foo_tree_s, &footree, &v,1);
                //     break;
                case NT_DEL:
                    NEDTRIE_REMOVE(foo_tree_s, &footree, &v);
                    break;
                default:
                    ALERT(log_id(), "Unknown operation type %d", op->type);
                    break;
            }
            op = (nt_op_entry *) Next_Operation();
        }
    }

    void NT::put() {
        foo_t a;
        a.key=2;
        NEDTRIE_INSERT(foo_tree_s, &footree, &a);



        nt_op_entry op;
        op.type = NT_PUT;
        op.key = 2;
        // ALERT(log_id(), "NT put called writing %d", op.key);
        Write_Operation(&op, sizeof(nt_op_entry));
        Sync_To_Last_Write();
        Apply_Ops();

    }

    void NT::get() {

        // Sync_To_Remote_Log();
        Sync_To_Last_Write();
        Apply_Ops();
        foo_t b, *r;
        b.key=2;
        // NEDTRIE_INSERT(foo_tree_s, &footree, &b);
        r=NEDTRIE_CFIND(foo_tree_s, &footree, &b,1);
        assert(r->key == b.key);

    }

    void NT::del() {
        foo_t a;
        a.key=2;
        NEDTRIE_REMOVE(foo_tree_s, &footree, &a);

    }

    void NT::fsm() {
        ALERT(log_id(), "NT fsm called");
        // foo_t a, b, c, *r;
        // ALERT(log_id(), "SLogger Starting FSM");
        while(!*_global_start_flag){
            INFO(log_id(), "not globally started");
        };

        int i=0;
        _workload_driver.set_workload(W);
        while(!*_global_end_flag){
            Request next_request;
            

            //Pause goes here rather than anywhere else because I don't want
            //To have locks, or any outstanding requests
            if (*_global_prime_flag && !_local_prime_flag){
                _local_prime_flag = true;
                _workload_driver.set_workload(_workload);
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


            #ifdef MEASURE_ESSENTIAL
            uint64_t latency = (_operation_end_time - _operation_start_time).count();

            if (next_request.op == PUT) {
                    #ifdef MEASURE_MOST
                    _insert_rtt.push_back(_current_insert_rtt);
                    _insert_latency_ns.push_back(latency);
                    #endif
                _completed_insert_count++;
                _current_insert_rtt = 0;
                _sum_insert_latency_ns += latency;
            } else if (next_request.op == GET) {
                    #ifdef MEASURE_MOST
                    _read_rtt.push_back(current_read_rtt);
                    _read_latency_ns.push_back(latency);
                    #endif
                _completed_read_count++;
                _current_read_rtt =0;
                _sum_read_latency_ns += latency;

            }
            #endif


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