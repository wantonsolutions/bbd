import orchestrator
import data_management as dm
import plot_cuckoo as plot_cuckoo
import datetime
import git

import matplotlib.pyplot as plt
import numpy as np

# def write_config_to_json_file(config, filename):
#     with open(filename, 'w') as outfile:
#         json.dump(config, outfile)

def plot_general_stats_last_run(dirname=""):
    stats, directory = dm.load_statistics(dirname=dirname)
    print("plot general stats")
    print(stats)
    plot_names = [
        "general_stats",
        "cas_success_rate",
        "read_write_ratio",
        # #dont uncomment this one ####"request_success_rate",
        "rtt_per_operation",
        "bytes_per_operation",
        "messages_per_operation",
        # "fill_factor",
        # "throughput_approximation",
        "throughput",
        "bandwidth",
        "latency_per_operation",
        "retry_breakdown",
        ]
    plot_cuckoo.multi_plot_runs(stats, plot_names, directory)

config=dict()


table_size = 1024 * 1024 * 100
# table_size = 1024 * 1024 * 10
# table_size = 1024 * 1024
#int table_size = 256;
#int table_size = 1024;
#int table_size = 256;
#int table_size = 1024 * 2;
entry_size = 8
bucket_size = 8
memory_size = entry_size * table_size
buckets_per_lock = 16
locks_per_message = 64
read_threshold_bytes = 256


#maditory fields added to prevent breakage
config["description"] = "This is a test run and a manditory field"
config["name"] = "test_run"
config["state_machine"] = "remote log"
config['date']=datetime.datetime.now().strftime("%Y-%m-%d")
config['commit']=git.Repo(search_parent_directories=True).head.object.hexsha
# config['hash_factor']=str(999999999)

config["bucket_size"] = str(bucket_size)
config["entry_size"] = str(entry_size)
config["batch_size"] = str(1)
config["bits_per_client_position"]=str(3)

config["indexes"] = str(table_size)
config["memory_size"] = str(memory_size)
config["hash_factor"] = str(2.3)

config["read_threshold_bytes"] = str(read_threshold_bytes)
config["buckets_per_lock"] = str(buckets_per_lock)
config["locks_per_message"] = str(locks_per_message)
config["deterministic"]="True"
config["workload"]="ycsb-w"
config["id"]=str(0)
config["search_function"]="bfs"
config["location_function"]="dependent"
config["max_fill"]=0

#Client State Machine Arguements
total_inserts = 1
max_epochs = 128
prime_epochs = 0
num_clients = 1
runtime = 10
#num_clinets = 1;
config["total_inserts"]=str(total_inserts)
config["total_requests"]=str(total_inserts)
config["max_epochs"]=str(max_epochs)
config["prime"]="true"
config["prime_epochs"]=str(prime_epochs)
config["num_clients"]=str(num_clients)
config["runtime"]=str(runtime)
config["use_mask"]="true"

#RDMA Engine Arguments
config["server_address"]="192.168.1.12"
config["base_port"] = "20886"

config["trials"] = 1


def run_hero_ycsb_update():
    memory_size = memory_size = 1024 * 1024 * 10
    # clients = [4, 8, 16, 32, 64, 128, 160]
    clients = [1,2,4,8,16,24]
    # clients = [160]
    # clients = [400]
    config["indexes"] = str(table_size)
    config["memory_size"] = str(memory_size)
    config["search_function"]="bfs"

    config["prime"]="true"
    config["prime_fill"]="10"
    config["max_fill"]="90"

    config["allocate_function"]="FAA"



    config['trials'] = 1
    workloads = ["ycsb-a", "ycsb-b", "ycsb-c", "ycsb-w"]
    # workloads = ["ycsb-w"]
    # workloads=["ycsb-a"]

    orchestrator.boot(config.copy())
    for workload in workloads:
        runs=[]
        for c in clients:
            lconfig = config.copy()
            lconfig['num_clients'] = str(c)
            lconfig['workload']=workload
            runs.append(orchestrator.run_trials(lconfig))
        dirname="data/hero-update-"+workload
        dm.save_statistics(runs, dirname=dirname)
        # plot_general_stats_last_run(dirname=dirname)


def plot_hero_ycsb_update():
    workloads = ["ycsb-a", "ycsb-b", "ycsb-c", "ycsb-w"]
    # workloads = ["ycsb-a"]
    fig, axs = plt.subplots(1,len(workloads), figsize=(12,3))

    for i in range(len(workloads)):
        dirname="data/hero-update-"+workloads[i]
        ax = axs[i]
        stats = dm.load_statistics(dirname=dirname)
        stats=stats[0]
        plot_cuckoo.throughput(ax, stats, decoration=False, label="nedtrie")
        ax.legend()
        ax.set_xlabel("clients")
        ax.set_title(workloads[i])
        ax.set_ylabel("throughput (MOPS)")

def debug_exp(config):
    clients = [1, 2, 4, 8, 16, 24]
    runs = []
    for c in clients:
        lconfig = config.copy()
        lconfig["num_clients"] = str(c)
        stats = orchestrator.run_trials(lconfig)
        runs.append(stats)
    # print(runs)
    dm.save_statistics(runs)

def client_fill_to_50_exp(config):
    # clients = [1, 2, 4, 8, 16, 24]
    # clients = [4, 8, 16, 32, 64, 128, 160]
    # clients = [10,20,40,80,160]
    # clients = [400]
    # clients = [4, 8]
    # clients = [16,32]
    # clients = [4]
    clients = [1,2,4,8,16,24]
    # clients = [8]
    # clients = [160]
    memory_size = 1024 * 1024
    config["indexes"] = str(table_size)
    config["memory_size"] = str(memory_size)
    config["trials"]=1

    config["prime"]="true"
    config["prime_fill"]="10"
    config["max_fill"]="50"
    config["workload"]="ycsb-w"
    config["allocate_function"]="FAA"
    orchestrator.boot(config.copy())

    runs = []
    for c in clients:
        lconfig = config.copy()
        print(lconfig["state_machine"])
        lconfig["num_clients"] = str(c)
        stats = orchestrator.run_trials(lconfig)
        runs.append(stats)
    # print(runs)
    dm.save_statistics(runs)

def entry_size(config):
    clients = 24
    memory_size = 1024 * 1024 * 10
    entry_size = [1,2,4,8,16,32,64,128,256, 512]
    config["memory_size"] = str(memory_size)
    config["trials"]=3

    config["prime"]="true"
    config["prime_fill"]="10"
    config["max_fill"]="90"
    config["workload"]="ycsb-w"
    config["allocate_function"]="FAA"
    config["num_clients"] = str(clients)
    orchestrator.boot(config.copy())

    runs = []
    for es in entry_size:
        lconfig = config.copy()
        lconfig["entry_size"] = str(es)
        stats = orchestrator.run_trials(lconfig)
        runs.append(stats)
    dirname="data/entry_size"
    dm.save_statistics(runs)


def cas_vs_faa_alocate(config):
    clients = [1, 2, 4, 8, 16, 24]
    # clients = [1]
    # allocate_functions = ["FAA", "MFAA", "CAS"]
    allocate_functions = ["FAA", "MFAA", "CAS"]

    memory_size = 1024 * 1024
    config["memory_size"] = str(memory_size)
    config["trials"]=1

    config["prime"]="true"
    config["prime_fill"]="10"
    config["max_fill"]="50"
    config["workload"]="ycsb-w"
    config["runtime"]="5"
    orchestrator.boot(config.copy())
    for af in allocate_functions:
        runs = []
        for c in clients:
            print("running "+af+" with "+str(c)+" clients")
            lconfig = config.copy()
            lconfig["allocate_function"] = af
            lconfig["num_clients"] = str(c)
            stats = orchestrator.run_trials(lconfig)
            runs.append(stats)
        dirname="data/allocate-"+af
        dm.save_statistics(runs, dirname=dirname)

def plot_cas_vs_faa_allocate():
    allocate_functions = ["FAA", "MFAA", "CAS"]
    fig, axs = plt.subplots(2,1, figsize=(4,5))
    for i, af in enumerate(allocate_functions):
        dirname="data/allocate-"+af
        # ax = axs
        stats = dm.load_statistics(dirname=dirname)
        stats=stats[0]
        plot_cuckoo.throughput(axs[0], stats, decoration=False, label=af)
        plot_cuckoo.cas_success_rate(axs[1], stats, decoration=False, label=af)
    axs[0].legend()
    axs[0].set_xlabel("clients")
    axs[0].set_ylabel("MOPS")

    axs[1].legend()
    axs[1].set_xlabel("clients")
    axs[1].set_ylabel("Atomic Success Rate")

    plt.tight_layout()
    plt.savefig("allocate_tput.pdf")

def batch_size(config):
    memory_size = 16777216
    batch_sizes = [1,2,4,8,16,32,64,128]
    # batch_sizes = [1,2,4,8,16,32,64,128,256]
    # batch_sizes = [16,32,64,128,256]
    num_clients = [6,12,24]
    # num_clients = [16,24]
    config["memory_size"] = str(memory_size)
    config["trials"]=3
    config["allocate_function"]="FAA"
    config["runtime"]="30"
    orchestrator.boot(config.copy())
    for client in num_clients:
        runs=[]
        for bs in batch_sizes:
            lconfig = config.copy()
            lconfig["num_clients"] = str(client)
            lconfig["batch_size"] = str(bs)
            stats = orchestrator.run_trials(lconfig)
            runs.append(stats)
        dirname="data/batch_size-"+str(client)
        dm.save_statistics(runs, dirname=dirname)

def plot_batch_size():
    batch_sizes = [1,2,4,8,16,32,64,128]
    # num_clients = [1,2,4,8,16,24]
    num_clients = [1,3,6,12,24]
    # num_clients = [8,16,24]
    fig, axs = plt.subplots(1,1, figsize=(5,4))
    for client in num_clients:
        dirname="data/batch_size-"+str(client)
        stats = dm.load_statistics(dirname=dirname)
        stats=stats[0]
        plot_cuckoo.throughput(axs, stats, decoration=False, label=str(client), x_axis="batch size")
    axs.legend()
    axs.set_title("Client Count vs Batch Size")
    axs.set_xlabel("batch size")
    axs.set_ylabel("MOPS")
    plt.tight_layout()
    plt.savefig("batch_size.pdf")





    plt.tight_layout()
    plt.savefig("hero_ycsb-update.pdf")


batch_size(config)
plot_batch_size()

# run_hero_ycsb_update()
# plot_hero_ycsb_update()

# entry_size(config)

# client_fill_to_50_exp(config)
# plot_general_stats_last_run()


# cas_vs_faa_alocate(config)
# plot_cas_vs_faa_allocate()
    

# run_hero_ycsb_update()
# plot_hero_ycsb_update()


# search_success_lock_size()
# plot_search_success_lock_size()


# run_hero_ycsb_fill_tput()
# plot_hero_ycsb_fill_tput()


# masked_cas_vs_lock_size()
# plot_masked_cas_vs_lock_size()

# masked_cas_sensitivity()
# plot_masked_cas_sensitivity()


# read_size_sensitivity()
# plot_read_size_sensitivity()

# factor_exp()
# plot_factor_exp()

# run_entry_size_exp()
# plot_entry_size_exp()





# debug_exp(config)
# fill_factor(config)
# table_size_contention(config)
# run_hero_ycsb()
# plot_hero_ycsb()

# run_hero_ycsb_fill()
# plot_hero_ycsb_fill()
# locks_per_message_test(config)
# plot_buckets_per_lock_vs_locks_per_message_experiment()

# independent_search(config)
# plot_round_trips_per_insert_operation()

# search_fill_throughput()
# plot_search_fill_tput()