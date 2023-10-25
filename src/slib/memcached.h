#ifndef RSEC_MEMCACHED_HEADER
#define RSEC_MEMCACHED_HEADER

// #include "ibsetup.h"
// #include "mitsume.h"
#include "config.h"
#include <libmemcached/memcached.h>

#define MEMCACHED_MAX_NAME_LEN 256

extern char MEMCACHED_IP[64];

int memcached_get_published(const char *key, void **value);
void memcached_publish(const char *key, void *value, int len);
void memcached_pubish_table_config(table_config *config);
table_config *memcached_get_table_config(void);

void memcached_publish_slog_config(slog_config *config);
slog_config * memcached_get_slog_config(void);

void memcached_publish_experiment_control(experiment_control *control);
experiment_control *memcached_get_experiment_control(void);

void memcached_publish_memory_stats(memory_stats *ms);
memory_stats *memcached_get_memory_stats(void);

//Shared Control Results
void send_inital_experiment_control_to_memcached_server();
void start_distributed_experiment();
void end_experiment_globally();
void announce_priming_complete();

#endif
