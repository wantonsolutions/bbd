#include "config.h"
#include <jsoncpp/json/json.h>
#include <iostream>
#include <sstream>      // std::istringstream
#include <fstream>
#include "log.h"

#include <unordered_map>

unsigned int get_config_int(unordered_map<string, string> config, string field) {
    try {
        return stoi(config[field]);
    } catch (exception& e) {
        throw logic_error("ERROR: Config missing value " + field);
    }
}

bool get_config_bool(unordered_map<string, string> config, string field){
    try {
        return (config[field] == "true");
    } catch (exception &e) {
        throw logic_error("ERROR: Config missing value " + field);
    }
}

inline bool file_exists (const std::string& name) {
    ifstream f(name.c_str());
    return f.good();
}

bool check_config_variable(unordered_map<string, string> config, string field) {
    if (config.find(field) == config.end()) {
        printf("ERROR: config missing %s\n", field.c_str());
        return false;
    }
    return true;
}

vector<string> split(string s, char delimiter) {
    vector<string> tokens;
    string token;
    istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

bool check_memory_server_config(unordered_map<string,string> config) {
    //Check that all the required variables are in the configuration
    bool has_required_variables = true;
    has_required_variables &= check_config_variable(config, "server_addresses");
    has_required_variables &= check_config_variable(config, "base_ports");
    has_required_variables &= check_config_variable(config, "num_memory_servers");
    if (!has_required_variables) {
        return false;
    }

    //Each server address is listed in a comma seperated string.
    //Check that both the base ports, and the server address are equal to num memory servers
    int num_memory_servers = get_config_int(config, "num_memory_servers");

    vector<string> server_addresses = split(config["server_addresses"], ',');
    vector<string> base_ports = split(config["base_ports"], ',');
    if (server_addresses.size() != num_memory_servers) {
        printf("ERROR: num_memory_servers does not match the number of server addresses\n");
        return false;
    }
    if (base_ports.size() != num_memory_servers) {
        printf("ERROR: num_memory_servers does not match the number of base ports\n");
        return false;
    }
    //Check that all of the base ports are integers
    for (int i = 0; i < base_ports.size(); i++) {
        try {
            stoi(base_ports[i]);
        } catch (exception &e) {
            printf("ERROR: base port %s is not an integer\n", base_ports[i].c_str());
            return false;
        }
    }
    //ensure that each of the IP address are unique
    for (int i = 0; i < server_addresses.size(); i++) {
        for (int j = i+1; j < server_addresses.size(); j++) {
            if (server_addresses[i] == server_addresses[j]) {
                printf("ERROR: server address %s is repeated\n", server_addresses[i].c_str());
                return false;
            }
        }
    }
    return true;
}

unordered_map<string, string> read_config_from_file(string config_filename){

    if(!file_exists(config_filename)){
        printf("ERROR: config file %s does not exist\n", config_filename.c_str());
        exit(1);
    }

    printf("Client Input Config: %s\n",config_filename.c_str());

    ifstream ifs(config_filename);
    Json::Reader reader;
    Json::Value obj;
    reader.parse(ifs, obj); // reader can also read strings

    unordered_map<string, string> config;
    for(Json::Value::iterator it = obj.begin(); it != obj.end(); ++it) {
        printf("%-30s : %s\n", it.key().asString().c_str(), it->asString().c_str());
        // cout << it.key().asString() << "  " << it->asString() <<endl;
        config[it.key().asString()] = it->asString();
    }
    return config;
}

void write_json_statistics_to_file(string filename, Json::Value statistics) {
    try{
        std::ofstream file_id;
        file_id.open(filename);
        Json::StyledWriter styledWriter;
        file_id << styledWriter.write(statistics);
        file_id.close();
    } catch (exception& e) {
        ALERT("WRITE STATS", "ERROR: could not write statistics to file %s\n", filename.c_str());
        ALERT("WRITE STATS", "ERROR: %s\n", e.what());
        exit(1);
    }

}

void write_statistics(
    unordered_map<string, string> config, 
    unordered_map<string,string> system_stats, 
    vector<unordered_map<string,string>> client_stats,
    unordered_map<string, string> memory_statistics
    ) {

    Json::Value client_json;

    //Write the system stats to the json output
    Json::Value system_stats_json;
    for (auto it = system_stats.begin(); it != system_stats.end(); it++) {
        system_stats_json[it->first] = it->second;
    }
    client_json["system"] = system_stats_json;

    //Write the config to the json output
    Json::Value config_json;
    for (auto it = config.begin(); it != config.end(); it++) {
        config_json[it->first] = it->second;
    }
    client_json["config"] = config_json;

    //Take care of the client statistics
    Json::Value client_vector (Json::arrayValue);
    for (long unsigned int i = 0; i < client_stats.size(); i++) {
        Json::Value client_stats_json;
        client_stats_json["client_id"] = client_stats[i]["client_id"];
        for (auto it = client_stats[i].begin(); it != client_stats[i].end(); it++) {
            client_stats_json["stats"][it->first] = it->second;
        }
        client_vector.append(client_stats_json);
        // client_json.append(client_stats_json);
    }
    client_json["clients"] = client_vector;

    Json::Value memory_stats_json;
    for (auto it = memory_statistics.begin(); it != memory_statistics.end(); it++) {
        memory_stats_json[it->first] = it->second;
    }
    client_json["memory"] = memory_stats_json;
    write_json_statistics_to_file("statistics/client_statistics.json", client_json);
}