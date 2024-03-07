CC := g++ # define the C/C++ compiler to use
MLX5_INC := /usr/include/infiniband
XXHASH_INC := /usr/local/home/ssgrant/RemoteDataStructres/rcuckoo_rdma/xxHash
INCLUDES := -I$(XXHASH_INC) -I$(MLX5_INC)


# DEFINE_MACROS := -DVALUE_SIZE=12
DEFINE_MACROS := -DNOTHING
ifdef VALUE_SIZE
	DEFINE_MACROS := -DVALUE_SIZE=$(VALUE_SIZE)
endif
# -DVALUE_SIZE=28
CFLAGS := -std=c++2a -g -Wall -Wall -Wextra -pedantic -fPIC -msse4.2 $(DEFINE_MACROS)

RDMA_LIBS := -lrdmacm -libverbs -lpthread
LIBRARIES := -L$(XXHASH_INC) -lxxhash $(RDMA_LIBS) -lmemcached -lssl -lcrypto -ljsoncpp -ljemalloc


# SHARED_CPP := config.cpp hash.cpp rdma_client_lib.cpp rdma_helper.cpp state_machines.cpp virtual_rdma.cpp memcached.cpp rdma_common.cpp  tables.cpp cuckoo.cpp rdma_engine.cpp search.cpp util.cpp

SLIB_DIR := slib
RDMA_DIR := rdma
CUCKOO_DIR := cuckoo
SLOGGER_DIR := slogger
CORRUPTER_DIR := corrupter
ENTRY_POINT_DIR := clients_and_servers
TEST_DIR := test

SLIB_CPP := $(wildcard $(SLIB_DIR)/*.cpp)
RDMA_CPP := $(wildcard $(RDMA_DIR)/*.cpp)
CUCKOO_CPP := $(wildcard $(CUCKOO_DIR)/*.cpp)
SLOGGER_CPP := $(wildcard $(SLOGGER_DIR)/*.cpp)
CORRUPTER_CPP := $(wildcard $(CORRUPTER_DIR)/*.cpp)

bins := $(wildcard $(ENTRY_POINT_DIR)/*.cpp)
bins := $(patsubst $(ENTRY_POINT_DIR)/%.cpp, %, $(bins))

CPP_DEPS := $(SLIB_CPP) $(RDMA_CPP) $(SLOGGER_CPP) $(CORRUPTER_CPP) $(CUCKOO_CPP)
OBJ_DEPS := $(patsubst %.cpp, obj/%.o, $(CPP_DEPS))


allbins := $(patsubst %, bin/%, $(bins))
all: $(allbins)
$(info $$bins is [${bins}])
$(info $$allbins is [${allbins}])


obj/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBRARIES) -c $< -o $@

bin/%: $(OBJ_DEPS) $(ENTRY_POINT_DIR)/%.cpp
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJ_DEPS) $(ENTRY_POINT_DIR)/$*.cpp -o $@ $(LIBRARIES)



REPLICATED_LOG_CPP = $(SLIB_CPP) $(RDMA_CPP) $(CUCKOO_CPP) $(CORRUPTER_CPP) $(SLOGGER_CPP) $(TEST_DIR)/test_replicated_log.cpp
REPLICATED_LOG_OBJ := $(patsubst %.cpp, obj/%.o, $(REPLICATED_LOG_CPP))
replicated_log_test: $(REPLICATED_LOG_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(REPLICATED_LOG_OBJ) -o test_replicated_log $(LIBRARIES)
	mv test_replicated_log test/

VIRTUAL_RDMA_CPP = $(SLIB_CPP) $(RDMA_CPP) $(CUCKOO_CPP) $(CORRUPTER_CPP) $(SLOGGER_CPP) $(TEST_DIR)/test_virtual_rdma.cpp
VIRTUAL_RDMA_OBJ := $(patsubst %.cpp, obj/%.o, $(VIRTUAL_RDMA_CPP))
$(info $$VIRTUAL_RDMA_OBJ is [${VIRTUAL_RDMA_OBJ}])
virtual_rdma_test: $(VIRTUAL_RDMA_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(VIRTUAL_RDMA_OBJ) -o test_virtual_rdma $(LIBRARIES)
	mv test_virtual_rdma test/






clean:
	rm -rf test/test_hash
	rm -rf test/test_replicated_log
	rm -rf test/test_virtual_rdma
	rm -rf *.o *.so
	rm -rf obj
	rm -rf bin


.PHONY: all