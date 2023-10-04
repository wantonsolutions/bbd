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


SHARED_CPP := config.cpp hash.cpp rdma_client_lib.cpp rdma_helper.cpp state_machines.cpp virtual_rdma.cpp memcached.cpp rdma_common.cpp  tables.cpp cuckoo.cpp rdma_engine.cpp search.cpp util.cpp

SERVER_CPP := $(SHARED_CPP) rdma_memory_server.cpp
CLIENT_CPP := $(SHARED_CPP) cuckoo_client.cpp

SERVER_OBJ := $(patsubst %.cpp, obj/%.o, $(SERVER_CPP))
CLIENT_OBJ := $(patsubst %.cpp, obj/%.o, $(CLIENT_CPP))

# config.o: config.cpp
# 	$(CC) $(CFLAGS) $(INCLUDES) $(LIBRARIES) -c $< -o $@

all: rdma_memory_server cuckoo_client

obj/%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBRARIES) -c $< -o $@

rdma_memory_server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(SERVER_OBJ) -o rdma_memory_server $(LIBRARIES) 

cuckoo_client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(CLIENT_OBJ) -o cuckoo_client $(LIBRARIES) 




clean:
	rm -rf test/test_hash
	rm -rf *.o *.so
	rm -rf obj/*
	rm -rf build
	rm -rf rcuckoo_wrapper.cpp


.PHONY: all