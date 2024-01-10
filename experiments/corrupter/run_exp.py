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


# table_size = 1024 * 1024 * 10
# table_size = 1024 * 1024
#int table_size = 256;
#int table_size = 1024;
#int table_size = 256;
#int table_size = 1024 * 2;
memory_size = 1024


#maditory fields added to prevent breakage
config["description"] = "Corrupter Experiment"
config["name"] = "test_run"
config["state_machine"] = "corrupter"
config['date']=datetime.datetime.now().strftime("%Y-%m-%d")
config['commit']=git.Repo(search_parent_directories=True).head.object.hexsha
# config['hash_factor']=str(999999999)


config["memory_size"] = str(memory_size)

config["id"]=str(0)
config["search_function"]="bfs"
config["location_function"]="dependent"

#Client State Machine Arguements
num_clients = 1
runtime = 5
#num_clinets = 1;
config["num_clients"]=str(num_clients)
config["runtime"]=str(runtime)

#RDMA Engine Arguments
config["server_address"]="192.168.1.12"
config["base_port"] = "20886"

config["trials"] = 1


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

def corrupt_single_buffer(config):
    clients = [1, 2, 4, 8, 16, 24]
    memory_size = 1024
    chunk_size = 1024

    config["memory_size"] = str(memory_size)
    config["chunk_size"] = str(chunk_size)
    config["trials"]=1
    runs = []

    orchestrator.boot(config.copy())
    for c in clients:
        lconfig = config.copy()
        lconfig["num_clients"] = str(c)
        stats = orchestrator.run_trials(lconfig)
        runs.append(stats)
    
    dm.save_statistics(runs, "data/corrupt_single_buffer")


def plot_corrupt_single_buffer():
    stats, directory = dm.load_statistics("data/corrupt_single_buffer")

    fig, ax = plt.subplots(1,1, figsize=(4,3))
    # stats = plot_cuckoo.correct_stat_shape(stats)
    plot_cuckoo.percent_corrupted_reads(ax, stats)

    print(stats)
    print("plot general stats")
    ax.legend()
    ax.set_xlabel("clients")
    ax.set_ylabel("failed read percent")
    # ax.set_ylim(0,15)

    plt.tight_layout()
    plt.savefig("corrupted_buffers.pdf")

def corrupt_single_buffer_size(config):
    clients = [24]
    # chunk_size = [32, 64, 128, 256, 512, 1024, 2048, 4096]
    chunk_size = [4096]
    # chunk_size = [128, 2048, 4096]
    memory_size = 4096

    config["memory_size"] = str(memory_size)
    config["chunk_size"] = str(chunk_size)
    config["runtime"] = str(30)
    config["trials"]=1
    runs = []

    orchestrator.boot(config.copy())
    for cs in chunk_size:
        runs = []
        for c in clients:
            lconfig = config.copy()
            lconfig["chunk_size"] = str(cs)
            lconfig["num_clients"] = str(c)
            stats = orchestrator.run_trials(lconfig)
            runs.append(stats)
        dm.save_statistics(runs,"data/corrupt_size/"+str(cs))

def plot_corrupt_single_buffer_size():
    fig, ax = plt.subplots(1,1, figsize=(4,3))
    # chunk_size = [8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]
    # chunk_size = [32, 64, 128, 256]
    chunk_size = [32, 64, 128, 256, 512, 1024, 2048,4096]
    # chunk_size = [32, 64, 128, 256, 512, 1024]
    # chunk_size = [128]
    # chunk_size = [8, 16, ]

    for cs in chunk_size:
        # print(cs)
        stats, directory = dm.load_statistics("data/corrupt_size/"+str(cs))
        # stats = plot_cuckoo.correct_stat_shape(stats)
        plot_cuckoo.corrupted_reads(ax, stats, label=str(cs), decoration=False)


    # print(stats)
    print("plot general stats")
    ax.set_title("Torn Reads as a function of Write Size")
    ax.legend()
    ax.set_xlabel("clients")
    ax.set_ylabel("corrupted reads per million")
    # ax.set_yscale('log')
    # ax.set_ylim(bottom=0.0001)
    ax.legend(prop={'size': 6})

    plt.tight_layout()
    plt.savefig("corrupted_buffers_size.pdf")


def corrupt_buffers_tech_and_size(config):
    chunk_size = [32, 64, 128, 256, 512, 1024, 2048,4096]
    # chunk_size = [512]
    # chunk_size = [256]
    clients = 24
    lock_alg = ["BASIC_WRITE", "CRC_WRITE", "CAS_WRAPPED_WRITE", "CAS_WRAPPED_FIRST_BYTE"]

    memory_size = 4096
    config["memory_size"] = str(memory_size)
    config["runtime"] = str(30)
    config["trials"]=1
    config["num_clients"] = clients


    for l in lock_alg:
        runs = []
        for c in chunk_size:
            lconfig = config.copy()
            lconfig["chunk_size"] = str(c)
            lconfig["locking_alg"] = l
            stats = orchestrator.run_trials(lconfig)
            runs.append(stats)
        dm.save_statistics(runs,"data/corrupt_alg/"+str(l))


def plot_corrupt_buffers_tech_and_size():
    fig, ax = plt.subplots(1,1, figsize=(4,3))
    # chunk_size = [8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]
    # chunk_size = [32, 64, 128, 256]
    chunk_size = [32, 64, 128, 256, 512, 1024, 2048,4096]
    # chunk_size = [512]
    # chunk_size = [32, 64, 128, 256, 512, 1024]
    # chunk_size = [256]
    # chunk_size = [8, 16, ]

    lock_alg = ["BASIC_WRITE", "CRC_WRITE", "CAS_WRAPPED_WRITE", "CAS_WRAPPED_FIRST_BYTE"]
    for la in lock_alg:
        stats, directory = dm.load_statistics("data/corrupt_alg/"+str(la))
        plot_cuckoo.corrupted_reads(ax, stats, x_axis="chunk size", label=str(la), decoration=False)

    ax.set_xlabel("write size")
    ax.set_ylabel("corrupted reads per million")

    ax.legend(prop={'size': 6})
    ax.set_yscale('log')
    ax.set_ylim(bottom=0.0001)
    plt.tight_layout()
    plt.savefig("corrupted_buffers_alg.pdf")




def plot_static_corrupt_single_buffer_size():
    chunk_size = [32, 64, 128, 256, 512, 1024, 2048, 4096]
    corrupted_reads = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 117, 684]
    fig, ax = plt.subplots(1,1, figsize=(4,3))
    ax.plot(chunk_size, corrupted_reads, label="corrupted reads", marker='o', linestyle='--', color='r')
    ax.set_xlabel("write size")
    ax.set_ylabel("corrupted reads per million")
    ax.set_title("CAS guarded writes")

    plt.tight_layout()
    plt.savefig("corrupted_buffers_size_static.pdf")
    
# corrupt_single_buffer_size(config)
# plot_corrupt_single_buffer_size()

# plot_static_corrupt_single_buffer_size()


corrupt_buffers_tech_and_size(config)
plot_corrupt_buffers_tech_and_size()


    

# corrupt_single_buffer(config)
# plot_corrupt_single_buffer()

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