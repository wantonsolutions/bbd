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
LIBRARIES := -L$(XXHASH_INC) -lxxhash $(RDMA_LIBS) -lmemcached -lssl -lcrypto -ljsoncpp


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


CUCKOO_SERVER_CPP := $(SLIB_CPP) $(RDMA_CPP) $(SLOGGER_CPP) $(CORRUPTER_CPP) $(CUCKOO_CPP) $(ENTRY_POINT_DIR)/rdma_memory_server.cpp
CUCKOO_CLIENT_CPP := $(SLIB_CPP) $(RDMA_CPP) $(SLOGGER_CPP) $(CORRUPTER_CPP) $(CUCKOO_CPP) $(ENTRY_POINT_DIR)/cuckoo_client.cpp
SLOGGER_SERVER_CPP := $(SLIB_CPP) $(RDMA_CPP) $(SLOGGER_CPP) $(CORRUPTER_CPP) $(CUCKOO_CPP) $(ENTRY_POINT_DIR)/slogger_server.cpp
SLOGGER_CLIENT_CPP := $(SLIB_CPP) $(RDMA_CPP) $(SLOGGER_CPP) $(CORRUPTER_CPP) $(CUCKOO_CPP) $(ENTRY_POINT_DIR)/slogger_client.cpp
CORRPUTER_CLIENT_CPP := $(SLIB_CPP) $(RDMA_CPP) $(SLOGGER_CPP) $(CORRUPTER_CPP) $(CUCKOO_CPP) $(ENTRY_POINT_DIR)/corrupter_client.cpp
CORRUPTER_SERVER_CPP := $(SLIB_CPP) $(RDMA_CPP) $(SLOGGER_CPP) $(CORRUPTER_CPP) $(CUCKOO_CPP) $(ENTRY_POINT_DIR)/corrupter_server.cpp

CUCKOO_SERVER_OBJ := $(patsubst %.cpp, obj/%.o, $(CUCKOO_SERVER_CPP))
SLOGGER_SERVER_OBJ := $(patsubst %.cpp, obj/%.o, $(SLOGGER_SERVER_CPP))
CUCKOO_CLIENT_OBJ := $(patsubst %.cpp, obj/%.o, $(CUCKOO_CLIENT_CPP))
SLOGGER_CLIENT_OBJ := $(patsubst %.cpp, obj/%.o, $(SLOGGER_CLIENT_CPP))

CORRPUTER_SERVER_OBJ := $(patsubst %.cpp, obj/%.o, $(CORRUPTER_SERVER_CPP))
CORRPUTER_CLIENT_OBJ := $(patsubst %.cpp, obj/%.o, $(CORRPUTER_CLIENT_CPP))


# config.o: config.cpp
# 	$(CC) $(CFLAGS) $(INCLUDES) $(LIBRARIES) -c $< -o $@

# tests: virtual_rdma_test
all: rdma_memory_server slogger_server cuckoo_client slogger_client corrupter_client corrupter_server virtual_rdma_test


obj/%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBRARIES) -c $< -o $@

rdma_memory_server: $(CUCKOO_SERVER_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(CUCKOO_SERVER_OBJ) -o rdma_memory_server $(LIBRARIES) 

slogger_server: $(SLOGGER_SERVER_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(SLOGGER_SERVER_OBJ) -o slogger_server $(LIBRARIES)

cuckoo_client: $(CUCKOO_CLIENT_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(CUCKOO_CLIENT_OBJ) -o cuckoo_client $(LIBRARIES) 

slogger_client: $(SLOGGER_CLIENT_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(SLOGGER_CLIENT_OBJ) -o slogger_client $(LIBRARIES)

corrupter_client: $(CORRPUTER_CLIENT_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(CORRPUTER_CLIENT_OBJ) -o corrupter_client $(LIBRARIES)

corrupter_server: $(CORRPUTER_SERVER_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(CORRPUTER_SERVER_OBJ) -o corrupter_server $(LIBRARIES)


# REPLICATED_LOG_CPP = $(SLIB_CPP) $(RDMA_CPP) $(CUCKOO_CPP) $(CORRUPTER_CPP) $(SLOGGER_CPP) $(TEST_DIR)/test_replicated_log.cpp
# REPLICATED_LOG_OBJ := $(patsubst %.cpp, obj/%.o, $(REPLICATED_LOG_CPP))
# replicated_log_test: $(REPLICATED_LOG_OBJ)
# 	$(CC) $(CFLAGS) $(INCLUDES) $(REPLICATED_LOG_OBJ) -o test_replicated_log $(LIBRARIES)
# 	mv test_replicated_log test/

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
	rm -rf obj/*.o
	rm -rf obj/clients_and_servers/*.o
	rm -rf obj/cuckoo/*.o
	rm -rf obj/rdma/*.o
	rm -rf obj/slib/*.o
	rm -rf obj/corrupter/*.o
	rm -rf obj/slogger/*.o
	rm -rf obj/test/*.o

	rm -f rdma_memory_server
	rm -f cuckoo_client
	rm -f slogger_client
	rm -f slogger_server
	rm -f corrupter_server
	rm -f corrupter_client

	rm -rf build
	rm -rf rcuckoo_wrapper.cpp



.PHONY: all