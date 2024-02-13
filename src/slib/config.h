#pragma once
#ifndef CONFIG_H
#define CONFIG_H

using namespace std;
#include <string>
#include <unordered_map>
#include <vector>
#include <assert.h>
#define MAX_MEMORY_SERVERS 3

const string SERVER_TABLE_CONFIG_KEY = "server_table_config";
const string SERVER_SLOG_CONFIG_KEY = "server_slog_config";
const string EXPERIMENT_CONTROL_KEY = "experiment_control";
const string SERVER_CORRUPTER_CONFIG_KEY = "server_corrupter_config";
const string MEMORY_STATS_KEY = "memory_stats";

typedef struct table_config {
    string to_string() {
        return "table_address: " + std::to_string(table_address) + "\n" +
            "table_size_bytes: " + std::to_string(table_size_bytes) + "\n" +
            "num_rows: " + std::to_string(num_rows) + "\n" +
            "remote_key: " + std::to_string(remote_key) + "\n" +
            "buckets_per_row: " + std::to_string(buckets_per_row) + "\n" +
            "entry_size_bytes: " + std::to_string(entry_size_bytes) + "\n" +
            "lock_table_address: " + std::to_string(lock_table_address) + "\n" +
            "lock_table_size_bytes: " + std::to_string(lock_table_size_bytes) + "\n" +
            "lock_table_key: " + std::to_string(lock_table_key) + "\n" +
            "lease_table_address: " + std::to_string(lease_table_address) + "\n" +
            "lease_table_size_bytes: " +std::to_string(lease_table_size_bytes) + "\n" +
            "lease_table_key" + std::to_string(lease_table_key) + "\n";
    }
    uint64_t table_address;
    int table_size_bytes;
    int num_rows;
    int remote_key;
    int buckets_per_row;
    int entry_size_bytes;
    //lock table
    uint64_t lock_table_address;
    int lock_table_key;
    int lock_table_size_bytes;
    //lease table
    uint64_t lease_table_address;
    int lease_table_size_bytes;
    int lease_table_key;
} table_config;

typedef struct slog_config {
    string to_string() {
        return "slog_address: " + std::to_string(slog_address) + "\n" +
            "slog_key: " + std::to_string(slog_key) + "\n" +
            "slog_size_bytes: " + std::to_string(slog_size_bytes) + "\n" +  
            "tail_pointer_address: " + std::to_string(tail_pointer_address) + "\n" +
            "tail_pointer_key: " + std::to_string(tail_pointer_key) + "\n" +
            "tail_pointer_size_bytes: " + std::to_string(tail_pointer_size_bytes) + "\n" +
            "client_position_table_address: " + std::to_string(client_position_table_address) + "\n" +
            "client_position_table_key: " + std::to_string(client_position_table_key) + "\n" +
            "client_position_table_size_byte: " + std::to_string(client_position_table_size_bytes) + "\n";
    }
    uint64_t slog_address;
    int slog_key;
    int slog_size_bytes;

    uint64_t tail_pointer_address;
    int tail_pointer_key;
    int tail_pointer_size_bytes;

    uint64_t client_position_table_address;
    int client_position_table_key;
    int client_position_table_size_bytes;
} slog_config;

typedef struct client_counter {
    uint64_t count;
} client_counter;

typedef struct corrupter_config {
    string to_string() {
        return "chunk_address: " + std::to_string(chunk_address) + "\n" +
            "chunk_key: " + std::to_string(chunk_key) + "\n" +
            "chunk_mem_size: " + std::to_string(chunk_mem_size) + "\n" +
            "chunk_size: " + std::to_string(chunk_size) + "\n";
    }

    uint64_t chunk_address;
    int chunk_key;
    int chunk_mem_size;
    int chunk_size;
} corrupter_config;

typedef struct experiment_control {
    string to_string(){
        assert(memory_servers <= MAX_MEMORY_SERVERS);
        string start = "start:";
        for (int i = 0; i < memory_servers; i++) {
            start += " " + std::to_string(experiment_start[i]);
        }
        return start + "\n" +
            "experiment_stop: " + std::to_string(experiment_stop) + "\n" +
            "priming_complete: " + std::to_string(priming_complete) + "\n";
    }
    bool is_experiment_running() {
        for (int i = 0; i < memory_servers; i++) {
            if (!experiment_start[i]) {
                return false;
            }
        }
        return true;
    }
    bool is_experiment_stopped() {
        return experiment_stop;
    }
    bool is_priming_complete() {
        return priming_complete;
    }
    int memory_servers;
    bool experiment_start[MAX_MEMORY_SERVERS];
    bool experiment_stop;
    bool priming_complete;
} experiment_control;

typedef struct memory_stats {
    bool finished_run;
    float fill;
} memory_stats;


unordered_map<string, string> read_config_from_file(string config_filename);
void write_statistics(
    unordered_map<string, string> config, 
    unordered_map<string,string> system_stats, 
    vector<unordered_map<string,string>> client_stats,
    unordered_map<string,string> memory_stats
    );

unsigned int get_config_int(unordered_map<string, string> config, string field);
bool get_config_bool(unordered_map<string, string> config, string field);

bool check_config_variable(unordered_map<string, string> config, string field);
vector<string> split(string s, char delimiter);
bool check_memory_server_config(unordered_map<string,string> config);

#endif