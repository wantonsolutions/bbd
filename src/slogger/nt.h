#ifndef NT_H
#define NT_H

#include "slogger.h"
#include "replicated_log.h"

#include "../slib/log.h"
#include "../slib/util.h"
#include "../slib/memcached.h"
#include "../rdma/rdma_common.h"
#include "../rdma/rdma_helper.h"

#include "../nedtries/nedtrie.h"

using namespace replicated_log;
using namespace slogger;
using namespace rdma_helper;


typedef struct foo_s foo_t;
struct foo_s {
  NEDTRIE_ENTRY(foo_s) link;
  size_t key;
};
NEDTRIE_HEAD(foo_tree_s, foo_s);
typedef struct foo_tree_s foo_tree_t;

enum nt_ops {
    NT_PUT = 1,
    NT_GET = 2,
    NT_DEL = 3
};

typedef struct nt_op_entry {
    enum nt_ops type;
    size_t key;
} nt_op_entry;


namespace nt {
    class NT : public SLogger {
        public:
            NT(unordered_map<string,string> config);
            void Apply_Ops();
            void fsm();
            void put();
            void get();
            void del();

        private:
            // static foo_tree_t footree;
         foo_tree_t footree;
    };
}

#endif