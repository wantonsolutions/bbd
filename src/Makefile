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
CFLAGS := -std=c++2a -g -Wall -Wall -Wextra -pedantic -fPIC  $(DEFINE_MACROS)

RDMA_LIBS := -lrdmacm -libverbs -lpthread
LIBRARIES := -L$(XXHASH_INC) -lxxhash $(RDMA_LIBS) -lmemcached -lssl -lcrypto -ljsoncpp


# SHARED_CPP := config.cpp hash.cpp rdma_client_lib.cpp rdma_helper.cpp state_machines.cpp virtual_rdma.cpp memcached.cpp rdma_common.cpp  tables.cpp cuckoo.cpp rdma_engine.cpp search.cpp util.cpp

SLIB_DIR := slib
RDMA_DIR := rdma
CUCKOO_DIR := cuckoo
SLOGGER_DIR := slogger
ENTRY_POINT_DIR := clients_and_servers

SLIB_CPP := $(wildcard $(SLIB_DIR)/*.cpp)
RDMA_CPP := $(wildcard $(RDMA_DIR)/*.cpp)
CUCKOO_CPP := $(wildcard $(CUCKOO_DIR)/*.cpp)
SLOGGER_CPP := $(wildcard $(SLOGGER_DIR)/*.cpp)


CUCKOO_SERVER_CPP := $(SLIB_CPP) $(RDMA_CPP) $(CUCKOO_CPP) $(ENTRY_POINT_DIR)/rdma_memory_server.cpp
CUCKOO_CLIENT_CPP := $(SLIB_CPP) $(RDMA_CPP) $(CUCKOO_CPP) $(ENTRY_POINT_DIR)/cuckoo_client.cpp
SLOGGER_CLIENT_CPP := $(SLIB_CPP) $(RDMA_CPP) $(SLOGGER_CPP) $(CUCKOO_CPP) $(ENTRY_POINT_DIR)/slogger_client.cpp

CUCKOO_SERVER_OBJ := $(patsubst %.cpp, obj/%.o, $(CUCKOO_SERVER_CPP))
CUCKOO_CLIENT_OBJ := $(patsubst %.cpp, obj/%.o, $(CUCKOO_CLIENT_CPP))
SLOGGER_CLIENT_OBJ := $(patsubst %.cpp, obj/%.o, $(SLOGGER_CLIENT_CPP))


# config.o: config.cpp
# 	$(CC) $(CFLAGS) $(INCLUDES) $(LIBRARIES) -c $< -o $@

all: rdma_memory_server cuckoo_client slogger_client

obj/%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBRARIES) -c $< -o $@

rdma_memory_server: $(CUCKOO_SERVER_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(CUCKOO_SERVER_OBJ) -o rdma_memory_server $(LIBRARIES) 

cuckoo_client: $(CUCKOO_CLIENT_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(CUCKOO_CLIENT_OBJ) -o cuckoo_client $(LIBRARIES) 

slogger_client: $(SLOGGER_CLIENT_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(SLOGGER_CLIENT_OBJ) -o slogger_client $(LIBRARIES)




clean:
	rm -rf test/test_hash
	rm -rf *.o *.so
	rm -rf obj/*.o
	rm -rf obj/clients_and_server/*.o
	rm -rf obj/cuckoo/*.o
	rm -rf obj/rdma/*.o
	rm -rf obj/slib/*.o

	rm -rf build
	rm -rf rcuckoo_wrapper.cpp



.PHONY: all