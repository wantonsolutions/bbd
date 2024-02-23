#include "rdma_common.h"
#include "rdma_server_lib.h"
#include "../slib/log.h"
#include "../slib/config.h"
#include <mutex>
#include <assert.h>

/* These are the RDMA resources needed to setup an RDMA connection */
/* Event channel, where connection management (cm) related events are relayed */
static struct ibv_context **devices;
static struct rdma_event_channel *cm_event_channel = NULL;
static struct rdma_cm_id *cm_server_qp_id[MAX_QPS] = {NULL}, *cm_client_qp_id[MAX_QPS] = {NULL};
static struct rdma_cm_id *cm_actual_qp_id[MAX_QPS] = {NULL};
static struct ibv_pd *pd = NULL;
static struct ibv_comp_channel *io_completion_channel = NULL;
static struct ibv_cq *cq = NULL;
static struct ibv_qp_init_attr qp_init_attr;
static struct ibv_qp *client_qp[MAX_QPS];
/* RDMA memory resources */
static struct ibv_mr *client_qp_metadata_mr[MAX_QPS], *server_qp_buffer_mr[MAX_QPS], *server_qp_metadata_mr[MAX_QPS];
static struct rdma_buffer_attr client_qp_metadata_attr[MAX_QPS];
static struct ibv_recv_wr client_recv_wr, *bad_client_recv_wr = NULL;
// static struct ibv_send_wr server_send_wr, *bad_server_send_wr = NULL;
static struct ibv_sge client_recv_sge;

static struct rdma_cm_event * event_mailbox[MAX_QPS];

std::mutex event_mailbox_lock;


/* Setup shared resources used for all connections */
int setup_shared_resources()
{
    int ret = -1, i;
    /* Get RDMA devices */
    devices = rdma_get_devices(&ret);
    if (ret == 0) {
        rdma_error("No RDMA devices found\n");
        return -ENODEV;
    }
    printf("%d devices found, using the first one: %s\n", ret, devices[0]->device->name);
    for(i = 0; i < ret; i++)    debug("Device %d: %s\n", i+1, devices[i]->device->name);

    /* Protection Domain (PD) is similar to a "process abstraction" 
     * in the operating system. All resources are tied to a particular PD. 
     * And accessing recourses across PD will result in a protection fault.
     */
    pd = ibv_alloc_pd(devices[0]);
    if (!pd) {
        rdma_error("Failed to allocate a protection domain errno: %d\n",
                -errno);
        return -errno;
    }
    debug("A new protection domain is allocated at %p \n", pd);
    /* Now we need a completion channel, were the I/O completion 
     * notifications are sent. Remember, this is different from connection 
     * management (CM) event notifications.  
     */
    io_completion_channel = ibv_create_comp_channel(devices[0]);
    if (!io_completion_channel) {
        rdma_error("Failed to create an I/O completion event channel, %d\n",
                -errno);
        return -errno;
    }
    debug("An I/O completion event channel is created at %p \n", 
            io_completion_channel);
    /* Now we create a completion queue (CQ) where actual I/O 
     * completion metadata is placed. The metadata is packed into a structure 
     * called struct ibv_wc (wc = work completion). ibv_wc has detailed 
     * information about the work completion. An I/O request in RDMA world 
     * is called "work" ;) 
     */
    cq = ibv_create_cq(devices[0] /* which device*/, 
            CQ_CAPACITY /* maximum capacity*/, 
            NULL /* user context, not used here */,
            io_completion_channel /* which IO completion channel */, 
            0 /* signaling vector, not used here*/);
    if (!cq) {
        rdma_error("Failed to create a completion queue (cq), errno: %d\n", -errno);
        return -errno;
    }
    debug("Completion queue (CQ) is created at %p with %d elements \n", cq, cq->cqe);
    /* Ask for the event for all activities in the completion queue*/
    ret = ibv_req_notify_cq(cq /* on which CQ */, 
            0 /* 0 = all event type, no filter*/);
    if (ret) {
        rdma_error("Failed to request notifications on CQ errno: %d \n", -errno);
        return -errno;
    }

    /*  Open a channel used to report asynchronous communication event */
    cm_event_channel = rdma_create_event_channel();
    if (!cm_event_channel) {
        rdma_error("Creating cm event channel failed with errno : (%d)", -errno);
        return -errno;
    }
    debug("RDMA CM event channel is created successfully at %p \n", cm_event_channel);

    return ret;
}

void add_event_to_mailbox(struct rdma_cm_event * event, int qp_id) {
  event_mailbox_lock.lock();
  if (event_mailbox[qp_id] != NULL) {
    ALERT("RDMA Common", "Mailbox for qp_id %d is not empty. This should not happen", qp_id);
    exit(0);
  }
  event_mailbox[qp_id] = event;
  event_mailbox_lock.unlock();
}

void get_event_from_mailbox(struct rdma_cm_event ** event, int qp_id) {
  event_mailbox_lock.lock();
  *event = event_mailbox[qp_id];
  if (*event != NULL) {
    event_mailbox[qp_id] = NULL;
  }
  event_mailbox_lock.unlock();
  return;
}

void poll_get_event_from_mailbox(struct rdma_cm_event **event, int qp_id) {
  get_event_from_mailbox(event, qp_id);
  while(*event == NULL) {
    get_event_from_mailbox(event, qp_id);
  }
  return;
}

int rdma_cm_event_to_qp_id(struct rdma_cm_event * event) {
  rdma_cm_id * id;
  if (event->listen_id != NULL) {
    id = event->listen_id;
    for(int i=0;i<MAX_QPS;i++) {
        if (cm_server_qp_id[i] == id) {
        return i;
        }
    }
  } 
  //printf("dang, listen id is NULL\n");
  if (event->id != NULL) {
    id = event->id;
    for(int i=0;i<MAX_QPS;i++) {
        if (cm_actual_qp_id[i] == id) {
        return i;
        }
    }
  } 
  //printf("dang, id is NULL\n");
  printf("unable to find qp_id for event\n");
  exit(0);
}

void * connection_event_manager_loop(void *) {
    while(true) {
        struct rdma_cm_event *cm_event = NULL;
        int ret = 1;
        ret = rdma_get_cm_event(cm_event_channel, &cm_event);
        if (ret) {
            rdma_error("Failed to retrieve a cm event, errno: %d \n", -errno);
            return NULL;
        }
        /* lets see, if it was a good event */
        if(0 != cm_event->status){
            rdma_error("CM event has non zero status: %d\n", cm_event->status);
            ret = -(cm_event->status);
            /* important, we acknowledge the event */
            rdma_ack_cm_event(cm_event);
            return NULL;
        }
        int mailbox_id = rdma_cm_event_to_qp_id(cm_event);
        if (mailbox_id == -1) {
          rdma_error("Unknown qp_id for cm_event\n");
          return NULL;
        }
        // printf("event received: %s\n", 
                // rdma_event_str(cm_event->event));
        // printf("adding event to mailbox %d\n", mailbox_id);
        add_event_to_mailbox(cm_event, mailbox_id);
        //Now we need to store the event somewhere so that other threads can get it.

    }
}

/* When we call this function cm_client_qp_id must be set to a valid identifier.
 * This creates a new queue pair for the connection
 */
int setup_client_qp(int qp_num) {
    int ret = -1;
    /* Set up the queue pair (send, recv) queues and their capacity.
     * The capacity here is define statically but this can be probed from the 
     * device. We just use a small number as defined in rdma_common.h */
    if(!cm_client_qp_id[qp_num]){
        rdma_error("Client id %d is still NULL \n", qp_num);
        return -EINVAL;
    }
    bool experimental = false;
    if (experimental) {
        struct ibv_exp_qp_init_attr qp_init_attr_exp;
        bzero(&qp_init_attr_exp, sizeof(qp_init_attr_exp));
        qp_init_attr_exp.comp_mask = IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS | IBV_EXP_QP_INIT_ATTR_PD | IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG;
        qp_init_attr_exp.cap.max_recv_sge = MAX_SGE;    /* Maximum SGE per receive posting;*/
        qp_init_attr_exp.cap.max_recv_wr = MAX_WR;      /* Maximum receive posting capacity; */
        qp_init_attr_exp.cap.max_send_sge = MAX_SGE;    /* Maximum SGE per send posting;*/
        qp_init_attr_exp.cap.max_send_wr = MAX_WR;      /* Maximum send posting capacity; */
        qp_init_attr_exp.cap.max_inline_data = 128;      /* Maximum amount of inline data */
        qp_init_attr_exp.qp_type = IBV_QPT_RC;                  /* QP type, RC = Reliable connection */
        qp_init_attr_exp.exp_create_flags = IBV_EXP_QP_CREATE_ATOMIC_BE_REPLY;
        qp_init_attr_exp.pd = pd;
        qp_init_attr_exp.recv_cq = cq; /* Where should I notify for receive completion operations */
        qp_init_attr_exp.send_cq = cq; /* Where should I notify for send completion operations */
        client_qp[qp_num] = ibv_exp_create_qp(devices[0], &qp_init_attr_exp);
        if (!client_qp[qp_num]) {
            rdma_error("Failed to EXP create QP, errno: %d \n", -errno);
            return -errno;
        }
        // client_qp[qp_num] = cm_client_qp_id[qp_num]->qp;
        printf("EXP QP %d created at %p \n", qp_num, (void *)client_qp[qp_num]);

    } else {
        bzero(&qp_init_attr, sizeof qp_init_attr);
        qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
        qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
        qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
        qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
        qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
        /* We use same completion queue, but one can use different queues */
        qp_init_attr.recv_cq = cq; /* Where should I notify for receive completion operations */
        qp_init_attr.send_cq = cq; /* Where should I notify for send completion operations */
        /*Lets create a QP */
        ret = rdma_create_qp(cm_client_qp_id[qp_num] /* which connection id */,
                pd /* which protection domain*/,
                &qp_init_attr /* Initial attributes */);
        if (ret) {
            rdma_error("Failed to create QP due to errno: %d\n", -errno);
            return -errno;
        }
        /* Save the reference for handy typing but is not required */
        client_qp[qp_num] = cm_client_qp_id[qp_num]->qp;
        debug("Client QP created at %p\n", client_qp[qp_num]);
    }
    return ret;
}

/* Starts an RDMA server by allocating basic connection resources for all QPs */
int start_rdma_server(struct sockaddr_in *server_addr, int qp_num, int port_num) 
{
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    /* rdma_cm_id is the connection identifier (like socket) which is used 
    * to define an RDMA connection. */
    ret = rdma_create_id(cm_event_channel, &cm_server_qp_id[qp_num], devices[0], RDMA_PS_TCP);
    if (ret) {
        rdma_error("Creating server cm id failed with errno: %d ", -errno);
        return -errno;
    }
    debug("A RDMA connection id for the server is created \n");
    /* Explicit binding of rdma cm id to the socket credentials */
    server_addr->sin_port = htons(port_num);
    ret = rdma_bind_addr(cm_server_qp_id[qp_num], (struct sockaddr*) server_addr);
    if (ret) {
        rdma_error("Failed to bind server address, errno: %d \n", -errno);
        return -errno;
    }
    // cm_server_qp_id[qp_num]->port_num = port_num;
    // printf("we are bound to port num %d should be in id also %d\n", port_num, cm_server_qp_id[qp_num]->port_num);
    debug("Server RDMA CM id is successfully binded \n");
    /* Now we start to listen on the passed IP and port. However unlike
    * normal TCP listen, this is a non-blocking call. When a new client is 
    * connected, a new connection management (CM) event is generated on the 
    * RDMA CM event channel from where the listening id was created. Here we
    * have only one channel, so it is easy. */
    ret = rdma_listen(cm_server_qp_id[qp_num], 8); /* backlog = 8 clients, same as TCP, see man listen*/

    if (ret) {
        rdma_error("rdma_listen failed to listen on server address, errno: %d ",
                -errno);
        return -errno;
    }

    /*now, we expect a client to connect and generate a RDMA_CM_EVNET_CONNECT_REQUEST 
    * We wait (block) on the connection management event channel for 
    * the connect event. 
    */


    poll_get_event_from_mailbox(&cm_event, qp_num);
    if (cm_event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
        rdma_error("RDMA connect request event expected but got %d \n", cm_event->event);
        return -1;
    }


    //Keep this part
    if (cm_event) {
        if (cm_event->listen_id == cm_server_qp_id[qp_num]) {
            //make an assignment for the actual id 
            cm_actual_qp_id[qp_num] = cm_event->id;
            // printf("got an event on the channel we wanted\n");
            // printf("CM event port num %d\n", cm_event->id->);
        }

    }
    // printf("CM event id %d qp_id %d", cm_event->id, cm_event->id->qp->qp_num);
    if (ret) {
        rdma_error("Failed to get cm event, ret = %d \n" , ret);
        return ret;
    }
    /* Much like TCP connection, listening returns a new connection identifier 
    * for newly connected client. In the case of RDMA, this is stored in id 
    * field. For more details: man rdma_get_cm_event 
    */
    cm_client_qp_id[qp_num] = cm_event->id;
    /* now we acknowledge the event. Acknowledging the event free the resources 
    * associated with the event structure. Hence any reference to the event 
    * must be made before acknowledgment. Like, we have already saved the 
    * client id from "id" field before acknowledging the event. 
    */
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the cm event errno: %d \n", -errno);
        return -errno;
    }
    debug("A new RDMA client connection id is stored at %p\n", cm_client_qp_id[i]);
    return ret;
}


/* Pre-posts a receive buffer and accepts an RDMA client connection */
int accept_client_connection(int qp_num)
{
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    struct sockaddr_in remote_sockaddr; 
    int ret = -1;
    if(!cm_client_qp_id[qp_num] || !client_qp[qp_num]) {
        rdma_error("Client resources are not properly setup\n");
        return -EINVAL;
    }

    /* we prepare the receive buffer in which we will receive the client metadata*/
    client_qp_metadata_mr[qp_num] = rdma_buffer_register(pd /* which protection domain */, 
        &client_qp_metadata_attr[qp_num] /* what memory */,
        sizeof(client_qp_metadata_attr[qp_num]) /* what length */, 
            (IBV_ACCESS_LOCAL_WRITE) /* access permissions */);
    if(!client_qp_metadata_mr[qp_num]){
        rdma_error("Failed to register client attr buffer\n");
        //we assume ENOMEM
        return -ENOMEM;
    }
    /* We pre-post this receive buffer on the QP. SGE credentials is where we 
     * receive the metadata from the client */
    client_recv_sge.addr = (uint64_t) client_qp_metadata_mr[qp_num]->addr; // same as &client_buffer_attr
    client_recv_sge.length = client_qp_metadata_mr[qp_num]->length;
    client_recv_sge.lkey = client_qp_metadata_mr[qp_num]->lkey;
    /* Now we link this SGE to the work request (WR) */
    bzero(&client_recv_wr, sizeof(client_recv_wr));
    client_recv_wr.sg_list = &client_recv_sge;
    client_recv_wr.num_sge = 1; // only one SGE
    ret = ibv_post_recv(client_qp[qp_num] /* which QP */,
              &client_recv_wr /* receive work request*/,
              &bad_client_recv_wr /* error WRs */);
    if (ret) {
        rdma_error("Failed to pre-post the receive buffer, errno: %d \n", ret);
        return ret;
    }
    debug("Receive buffer pre-posting is successful \n");
    /* Now we accept the connection. Recall we have not accepted the connection 
     * yet because we have to do lots of resource pre-allocation */
    memset(&conn_param, 0, sizeof(conn_param));
    /* this tell how many outstanding requests can we handle */
    conn_param.initiator_depth = MAX_RD_AT_IN_FLIGHT;
    /* This tell how many outstanding requests we expect other side to handle */
    conn_param.responder_resources = MAX_RD_AT_IN_FLIGHT; 
    ret = rdma_accept(cm_client_qp_id[qp_num], &conn_param);
    if (ret) {
        rdma_error("Failed to accept the connection, errno: %d \n", -errno);
        return -errno;
    }
    /* We expect an RDMA_CM_EVNET_ESTABLISHED to indicate that the RDMA  
    * connection has been established and everything is fine on both, server 
    * as well as the client sides.
    */
    debug("Going to wait for : RDMA_CM_EVENT_ESTABLISHED event \n");

    poll_get_event_from_mailbox(&cm_event, qp_num);
    if (cm_event->event != RDMA_CM_EVENT_ESTABLISHED) {
        rdma_error("Unexpected event received: %s, expecting %s \n", 
                rdma_event_str(cm_event->event), 
                rdma_event_str(RDMA_CM_EVENT_ESTABLISHED));
        return -EINVAL;
    }


    // ret = process_rdma_cm_event(cm_event_channel, 
    //         RDMA_CM_EVENT_ESTABLISHED,
    //         &cm_event);

    if (ret) {
        rdma_error("Failed to get the cm event, errnp: %d \n", -errno);
        return -errno;
    }
    /* We acknowledge the event */
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the cm event %d\n", -errno);
        return -errno;
    }
    /* Just FYI: How to extract connection information */
    memcpy(&remote_sockaddr /* where to save */, 
            rdma_get_peer_addr(cm_client_qp_id[qp_num]) /* gives you remote sockaddr */, 
            sizeof(struct sockaddr_in) /* max size */);
    printf("A new connection is accepted from %s for QP %d \n", 
            inet_ntoa(remote_sockaddr.sin_addr), qp_num);
    return ret;
}

/* This is server side logic. Server passively waits for the client to call 
 * rdma_disconnect() and then it will clean up its resources */
int disconnect_and_cleanup(int num_qps)
{
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1, i;

    for (i = 0; i < num_qps; i++) {
        /* Now we wait for the client to send us disconnect events for all QPs */
        debug("Waiting for cm event: RDMA_CM_EVENT_DISCONNECTED\n");

        poll_get_event_from_mailbox(&cm_event, i);
        if (cm_event->event != RDMA_CM_EVENT_DISCONNECTED) {
            rdma_error("RDMA connect request event expected but got %d \n", cm_event->event);
            return -1;
        }
        // ret = process_rdma_cm_event(cm_event_channel, 
        //         RDMA_CM_EVENT_DISCONNECTED, 
        //         &cm_event);
        if (ret) {
            rdma_error("Failed to get disconnect event, ret = %d \n", ret);
            return ret;
        }
        /* We acknowledge the event */
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            rdma_error("Failed to acknowledge the cm event %d\n", -errno);
            return -errno;
        }
        printf("A disconnect event %d is received from the client\n", i);
        
        /* Destroy QP and client cm id */
        /*NOTE: This is necessary befoe moving onto next QP because client 
         * waits on "disconneted" event (that is only generated by destroying 
         * these resources) before moving to disconnect next QP */
        rdma_destroy_qp(cm_client_qp_id[i]);
        ret = rdma_destroy_id(cm_client_qp_id[i]);
        if (ret) {
            rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
            // we continue anyways;
        }
    }
    printf("All QPs disconnected...\n");

    /* We free all the resources */
    for(i = 0; i < num_qps; i++) {

        /* Destroy memory buffers */
        rdma_buffer_free(server_qp_buffer_mr[i]);
        rdma_buffer_deregister(server_qp_metadata_mr[i]);	
        rdma_buffer_deregister(client_qp_metadata_mr[i]);
        /* Destroy rdma server id */
        ret = rdma_destroy_id(cm_server_qp_id[i]);
        if (ret) {
            rdma_error("Failed to destroy server id cleanly for QP %d, err: %d \n", i, -errno);
            // we continue anyways;
        }
    }
    
    /* Destroy CQ */
    ret = ibv_destroy_cq(cq);
    if (ret) {
        rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
        // we continue anyways;
    }
    /* Destroy completion channel */
    ret = ibv_destroy_comp_channel(io_completion_channel);
    if (ret) {
        rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
        // we continue anyways;
    }
    
    /* Destroy protection domain */
    ret = ibv_dealloc_pd(pd);
    if (ret) {
        rdma_error("Failed to destroy client protection domain cleanly, %d \n", -errno);
        // we continue anyways;
    }
    rdma_destroy_event_channel(cm_event_channel);
    printf("Server shut-down is complete \n");
    return 0;
}


//void *connection_setup(rdma_connection_setup_args args){
void *connection_setup(void* void_args){
    rdma_connection_setup_args args = *((rdma_connection_setup_args *)void_args);
    /* Each QP will bind to port numbers starting from base port */
    // ALERT("RDMA memory server", "Starting RDMA server thread %d port %d\n", args.index, args.port);
    int ret = start_rdma_server(args.server_sockaddr, args.index, args.port);
    if (ret) {
        rdma_error("RDMA server failed to start cleanly, ret = %d \n", ret);
        exit(1);
    }
    // ALERT("RDMA memory server", "RDMA server thread %d setting up resources\n", args.index);
    ret = setup_client_qp(args.index);
    if (ret) { 
        rdma_error("Failed to setup client resources, ret = %d \n", ret);
        exit(1);
    }
    // ALERT("RDMA memory server", "RDMA server thread %d accepting client connections\n", args.index);
    ret = accept_client_connection(args.index);
    if (ret) {
        rdma_error("Failed to handle client cleanly, ret = %d \n", ret);
        exit(1);
    }
    pthread_exit(NULL);

}

void multi_threaded_connection_setup(sockaddr_in server_sockaddr, int base_port, int num_qps) {
    pthread_t thread_ids[MAX_QPS];
    rdma_connection_setup_args args[MAX_QPS];
    pthread_t connection_event_manager;

    pthread_create(&connection_event_manager, NULL, &connection_event_manager_loop, NULL);
    
    for (int i = 0; i < num_qps; i++) {
        // ALERT("RDMA memory server", "FORKING %d\n", i);
        args[i].server_sockaddr = &server_sockaddr;
        args[i].index = i;
        args[i].port = base_port + i;
        pthread_create(&thread_ids[i], NULL, &connection_setup, (void *)&args[i]);
        stick_thread_to_core(thread_ids[i], 3);

    }
    ALERT("RDMA memory server", "Done forking\n");
    for (int i=0;i<num_qps;i++){
        INFO("ALERT Memory Server", "Joining Client Connection Thread %d\n", i);
        pthread_join(thread_ids[i],NULL);
    }
    ALERT("RDMA memory server", "Starting Experiment\n");

}

ibv_mr * register_server_object_at_mr_index(void * object_ptr, unsigned int object_size, int index) {
    assert(index < MAX_QPS);
    server_qp_buffer_mr[index] = rdma_buffer_register(pd /* which protection domain */, 
            object_ptr,
            object_size,
            (MEMORY_PERMISSION
            ) /* access permissions */);

    if (!server_qp_buffer_mr[index]) {
        rdma_error("Failed to register server buffer, %d \n", -errno);
        return NULL;
    }
    return server_qp_buffer_mr[index];
}


on_chip_memory_attr register_device_memory(unsigned int starting_address, unsigned int size) {
    on_chip_memory_attr device_memory = createMemoryRegionOnChip(
        starting_address,
        size,
         pd, 
         devices[0]);

    if(!device_memory.mr){
        rdma_error("Server failed to create a buffer for the lock table\n");
        /* we assume that it is due to out of memory error */
        exit(0);
        // return -ENOMEM;
    }
    return device_memory;
}

void copy_device_memory_to_host_object(void * host_object, unsigned int size, on_chip_memory_attr device_memory) {
    //Make sure that the device has been initalized
    assert(device_memory.dm != NULL);
    struct ibv_exp_memcpy_dm_attr cpy_attr;
    memset(&cpy_attr, 0, sizeof(cpy_attr));
    cpy_attr.memcpy_dir = IBV_EXP_DM_CPY_TO_HOST;
    cpy_attr.host_addr = host_object;
    cpy_attr.length = size;
    cpy_attr.dm_offset = 0;
    ibv_exp_memcpy_dm(device_memory.dm, &cpy_attr);
}

vector<string> get_ip_addr() {
    char ip[100];
    FILE *fp;
    fp = popen("hostname -I", "r");
    if (fp == NULL) {
        printf("Failed to run command\n" );
        exit(1);
    }
    fgets(ip, sizeof(ip), fp);
    pclose(fp);
    vector<string> ip_vector = split(ip, ' ');
    return ip_vector;

}


int get_memory_server_index(vector<string> server_addresses){
    vector<string> ip_vector = get_ip_addr();
    for (int i = 0; i < server_addresses.size(); i++) {
        for (int j = 0; j < ip_vector.size(); j++) {
            if (server_addresses[i] == ip_vector[j]) {
                return i;
            }
        }
    }
    ALERT("RDMA memory server", "ERROR: Could not find the IP address in the server_addresses\n");
    exit(1);
}
