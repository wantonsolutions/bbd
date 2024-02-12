#ifndef RDMA_SERVER_LIB
#define RDMA_SERVER_LIB

#include "rdma_common.h"
#include <vector>

typedef struct rdma_connection_setup_args {
    struct sockaddr_in *server_sockaddr;
    int index;
    int port;
} rdma_connection_setup_args;

int setup_shared_resources();
void add_event_to_mailbox(struct rdma_cm_event * event, int qp_id);
void get_event_from_mailbox(struct rdma_cm_event ** event, int qp_id);
void poll_get_event_from_mailbox(struct rdma_cm_event **event, int qp_id);
int rdma_cm_event_to_qp_id(struct rdma_cm_event * event);
void * connection_event_manager_loop(void *);
int setup_client_qp(int qp_num);
int start_rdma_server(struct sockaddr_in *server_addr, int qp_num, int port_num);
int accept_client_connection(int qp_num);
int disconnect_and_cleanup(int num_qps);
void *connection_setup(void* void_args);
void multi_threaded_connection_setup(sockaddr_in server_sockaddr, int base_port, int num_qps);

ibv_mr * register_server_object_at_mr_index(void * object_ptr, unsigned int object_size, int index);
on_chip_memory_attr register_device_memory(unsigned int starting_address, unsigned int size);
void copy_device_memory_to_host_object(void * host_object, unsigned int size, on_chip_memory_attr device_memory);
vector<string> get_ip_addr();
int get_memory_server_index(vector<string> server_addresses);

#endif