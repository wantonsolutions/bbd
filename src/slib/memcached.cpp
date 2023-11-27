#include "memcached.h"
#include "assert.h"
#include "stdio.h"
#include "config.h"
#include "log.h"

__thread memcached_st *memc = NULL;
char MEMCACHED_IP[64];

memcached_st *memcached_create_memc(void) {

  //todo move this memcached ip somwhere else so it can be set from the config. For now it's just b09-27
  strncpy(MEMCACHED_IP,"137.110.222.47",sizeof(MEMCACHED_IP));
  memcached_server_st *servers = NULL;
  memcached_st *memc = memcached_create(NULL);
  memcached_return rc;

  memc = memcached_create(NULL);
  const char *registry_ip = MEMCACHED_IP;

  /* We run the memcached server on the default memcached port */
  servers = memcached_server_list_append(servers, registry_ip,
                                         MEMCACHED_DEFAULT_PORT, &rc);
  rc = memcached_server_push(memc, servers);
//   CPE(rc != MEMCACHED_SUCCESS, "Couldn't add memcached server.\n", -1);

  return memc;
}

void memcached_publish(const char *key, void *value, int len) {
  assert(key != NULL && value != NULL && len > 0);
  memcached_return rc;

  if (memc == NULL) {
    fprintf(stdout, "creating memcached instance\n");
    memc = memcached_create_memc();
  }

  // fprintf(stderr,"\tPutting Key %s\n",key);
  // fprintf(stderr,"\tPutting Value %s\n",(const char*)value);
  // fprintf(stderr,"\tPutting Value-len %d\n",len);

  rc = memcached_set(memc, key, strlen(key), (const char *)value, len,
                     (time_t)0, (uint32_t)0);
  if (rc != MEMCACHED_SUCCESS) {
    const char *registry_ip = MEMCACHED_IP;
    fprintf(stderr,
            "\tHRD: Failed to publish key %s. Error %s. "
            "Reg IP = %s\n",
            key, memcached_strerror(memc, rc), registry_ip);
    exit(-1);
  }
}

void memcached_pubish_table_config(table_config *config) {
    assert(config != NULL);
    //todo check that each of the fields is valid
    assert(config->table_address > 0);
    assert(config->table_size_bytes > 0);
    assert(config->buckets_per_row > 0);
    assert(config->entry_size_bytes > 0);
    assert(config->lock_table_size_bytes > 0);
    assert(config->lock_table_key > 0);
    //the lock table address is going to be 0 most of the time.
    //We do not want to assert any value to it so it's flexible

    memcached_publish(SERVER_TABLE_CONFIG_KEY.c_str(), (void *)config, sizeof(table_config));
}

table_config * memcached_get_table_config(void) {
    table_config *config;
    int config_len = memcached_get_published(SERVER_TABLE_CONFIG_KEY.c_str(), (void **)&config);
    INFO("Memcached", "about to print the fetched table config of size %d\n",config_len);
    INFO("Memcached", "table config: %s\n", config->to_string().c_str());
    assert(config_len == sizeof(table_config));
    return config;
}

void memcached_publish_slog_config(slog_config *config) {
  assert(config != NULL);
  assert(config->slog_address > 0);
  assert(config->slog_key > 0);
  assert(config->slog_size_bytes > 0);
  memcached_publish(SERVER_SLOG_CONFIG_KEY.c_str(), (void *)config, sizeof(slog_config));
}

slog_config * memcached_get_slog_config(void) {
  slog_config *config;
  int config_len = memcached_get_published(SERVER_SLOG_CONFIG_KEY.c_str(), (void **)&config);
  INFO("Memcached", "about to print the fetched slog config of size %d\n",config_len);
  INFO("Memcached", "slog config: %s\n", config->to_string().c_str());
  assert(config_len == sizeof(slog_config));
  return config;
}


void memcached_publish_corrupter_config(corrupter_config *config) {
  assert(config != NULL);
  assert(config->chunk_address > 0);
  assert(config->chunk_key > 0);
  assert(config->chunk_mem_size > 0);
  assert(config->chunk_size > 0);
  memcached_publish(SERVER_CORRUPTER_CONFIG_KEY.c_str(), (void *)config, sizeof(corrupter_config));

}

corrupter_config * memcached_get_corrupter_config(void) {
  corrupter_config *config;
  int config_len = memcached_get_published(SERVER_CORRUPTER_CONFIG_KEY.c_str(), (void **)&config);
  INFO("Memcached", "about to print the fetched corrupter config of size %d\n",config_len);
  INFO("Memcached", "corrupter config: %s\n", config->to_string().c_str());
  assert(config_len == sizeof(corrupter_config));
  return config;

}

void memcached_publish_experiment_control(experiment_control * ec){
    memcached_publish(EXPERIMENT_CONTROL_KEY.c_str(), (void *)ec, sizeof(table_config));
}

experiment_control * memcached_get_experiment_control(void){
    experiment_control * ec;
    int experiment_control_len = memcached_get_published(EXPERIMENT_CONTROL_KEY.c_str(), (void **)&ec);
    // INFO("Memcached", "about to print the fetched experiment control %d\n",experiment_control_len);
    // INFO("Memcached", "table config: %s\n", ec->to_string().c_str());
    // INFO("Memcached", "fetched %d struct size %d\n",experiment_control_len, sizeof(experiment_control));
    // assert(experiment_control_len == sizeof(experiment_control));
    return ec;
}


void memcached_publish_memory_stats(memory_stats *ms){
  memcached_publish(MEMORY_STATS_KEY.c_str(), (void *)ms, sizeof(memory_stats));
}
memory_stats *memcached_get_memory_stats(void) {
  memory_stats *ms;
  int memory_stats_len = memcached_get_published(MEMORY_STATS_KEY.c_str(), (void **)&ms);
  INFO("Memcached", "about to print the fetched memory stats %d\n",memory_stats_len);
  INFO("Memcached", "memory stats: TODO IMPLEMENT PRINT\n"); //, ms->to_string().c_str());
  INFO("Memcached", "fetched %d struct size %d\n",memory_stats_len, sizeof(memory_stats));
  // assert(memory_stats_len == sizeof(memory_stats));
  return ms;
}

int memcached_get_published(const char *key, void **value) {
  assert(key != NULL);
  if (memc == NULL) {
    memc = memcached_create_memc();
  }
  memcached_return rc;
  size_t value_length;
  uint32_t flags;

  *value = memcached_get(memc, key, strlen(key), &value_length, &flags, &rc);

  if (rc == MEMCACHED_SUCCESS) {
    return (int)value_length;
  } else if (rc == MEMCACHED_NOTFOUND) {
    assert(*value == NULL);
    return -1;
  } else {
    const char *registry_ip = MEMCACHED_IP;
    fprintf(stderr,
            "Error finding value for key \"%s\": %s. "
            "Reg IP = %s\n",
            key, memcached_strerror(memc, rc), registry_ip);
    exit(-1);
  }
  /* Never reached */
  assert(false);
}

void send_inital_experiment_control_to_memcached_server() {
    experiment_control ec;
    ec.experiment_start = false;
    ec.experiment_stop = false;
    ec.priming_complete = false;
    memcached_publish_experiment_control(&ec);
}
void start_distributed_experiment(){
    experiment_control *ec = (experiment_control *)memcached_get_experiment_control();
    ec->experiment_start = true;
    memcached_publish_experiment_control(ec);
}

void end_experiment_globally(){
    experiment_control *ec = (experiment_control *)memcached_get_experiment_control();
    ec->experiment_stop = true;
    memcached_publish_experiment_control(ec);
}

void announce_priming_complete() {
    experiment_control *ec = (experiment_control *)memcached_get_experiment_control();
    ec->priming_complete = true;
    memcached_publish_experiment_control(ec);
}