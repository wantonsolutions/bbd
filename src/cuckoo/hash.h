#ifndef HASH_H
#define HASH_H

#include "xxhash.h"
#include "tables.h"
#include <string>

using namespace std;

typedef struct hash_locations{
    unsigned int primary;
    unsigned int secondary;
    unsigned int distance() {
        return max_bucket() - min_bucket();
    }
    unsigned int min_bucket() {
        if (primary < secondary){
            return primary;
        } else {
            return secondary;
        }
    }
    unsigned int max_bucket() {
        if (primary > secondary){
            return primary;
        } else {
            return secondary;
        }
    }
    string to_string() {
        return "primary: " + std::to_string(primary) + "\n" +
            "secondary: " + std::to_string(secondary) + "\n" +
            "distance: " + std::to_string(distance()) + "\n" +
            "min_bucket: " + std::to_string(min_bucket()) + "\n" +
            "max_bucket: " + std::to_string(max_bucket()) + "\n";
    }
} hash_locations;



void set_factor(float factor);
float get_factor();
// unsigned int get_table_id_from_index(unsigned int index);
unsigned int distance_to_bytes(unsigned int a, unsigned int b, unsigned int bucket_size, unsigned int entry_size);

XXH64_hash_t h1(Key &key);
XXH64_hash_t h2(Key &key);
XXH64_hash_t h3(Key &key);


unsigned int rcuckoo_primary_location(Key &key, unsigned int table_size);
unsigned int h3_suffix_base_two(Key &key);
unsigned int rcuckoo_secondary_location(Key &key, float factor, unsigned int table_size);
unsigned int rcuckoo_secondary_location_independent(Key &key, unsigned int table_size);
hash_locations rcuckoo_hash_locations(Key& key, unsigned int table_size);
hash_locations rcuckoo_hash_locations_independent(Key &key, unsigned int table_size);

#endif