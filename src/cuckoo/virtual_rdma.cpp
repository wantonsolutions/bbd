#include "virtual_rdma.h"
#include <string.h>
#include <algorithm>
#include <iostream>
#include <bits/stdc++.h>
#include "search.h"
#include "../slib/log.h"
#include "../slib/util.h"


using namespace std;
using namespace cuckoo_search;
namespace cuckoo_virtual_rdma {

    Request::Request(operation op, Key key, Value value){
        this->op = op;
        this->key = key;
        this->value = value;
    } 

    Request::Request(){
        this->op = GET;
        memset(key.bytes, 0, KEY_SIZE);
        memset(value.bytes, 0, VALUE_SIZE);
    }

    string Request::to_string(){
        string s = "Request: {";
        s += "Op: ";
        switch (op) {
            case GET:
                s += "GET";
                break;
            case PUT:
                s += "PUT";
                break;
            case DELETE:
                s += "DELETE";
                break;
            case NO_OP:
                s += "NO_OP";
                break;
        }
        s += " Key: " + key.to_string();
        s += " Value: " + value.to_string();
        s += "}";
        return s;
    }

    string VRMaskedCasData::to_string(){
        string s = "VRMaskedCasData: {";
        s += "min_lock_index: " + std::to_string(min_lock_index);
        s += " mask: " + uint64t_to_bin_string(mask);
        s += " new_value: " + uint64t_to_bin_string(new_value);
        s += " old: " + uint64t_to_bin_string(old);
        s += " min_set_lock: " + std::to_string(min_set_lock);
        s += " max_set_lock: " + std::to_string(max_set_lock);
        s += "}";
        return s;
    }

    string VRReadData::to_string() {
        string s = "VRReadData: {";
        s += "row: " + std::to_string(row);
        s += " offset: " + std::to_string(offset);
        s += " size: " + std::to_string(size);
        s += "}";
        return s;
    }

    string VRCasData::to_string() {
        string s = "VRCasData: {";
        s += "row " + std::to_string(row);
        s += " offset" + std::to_string(offset);
        s += " old_value: " + uint64t_to_bin_string(old);
        s += " new_value: " + uint64t_to_bin_string(new_value);
        return s;
    }

    vector<Entry> read_table_entry(Table &table, uint32_t bucket_id, uint32_t bucket_offset, uint32_t size) {
        table.assert_operation_in_table_bound(bucket_id, bucket_offset, size);
        uint32_t total_indexes = size / sizeof(Entry);
        vector<Entry> entries;
        uint32_t base = bucket_id * table.get_buckets_per_row() + bucket_offset;

        for (int i=0; i<total_indexes; i++) {
            uint32_t bucket = table.absolute_index_to_bucket_index(base + i);
            uint32_t offset = table.absolute_index_to_bucket_offset(base + i);
            entries.push_back(table.get_entry(bucket, offset));
        }
        return entries;
    }

    CasOperationReturn cas_table_entry(Table &table, uint32_t bucket_id, uint32_t bucket_offset, uint64_t old, uint64_t new_value) {
        Entry e = table.get_entry(bucket_id, bucket_offset);
        uint64_t ret_val = e.get_as_uint64_t();
        bool success = false;
        Entry new_entry;

        SUCCESS("virtual_rdma", "cas compairision table %08llx == guess %08llx (success=%d -- row %06d, col %06d)\n", ret_val, old, ret_val == old, bucket_id, bucket_offset);

        if (ret_val == old) {
            SUCCESS("virtual_rdma", "success cas compairision table %08llx == guess %08llx (success=%d -- row %06d, col %06d)\n", ret_val, old, ret_val == old, bucket_id, bucket_offset);
            new_entry.set_as_uint64_t(new_value);
            table.set_entry(bucket_id, bucket_offset, new_entry);
            success = true;
        } 
        return CasOperationReturn(success, ret_val);
    }

    CasOperationReturn masked_cas_lock_table(Table &table, uint32_t lock_index, uint64_t old, uint64_t new_value, uint64_t mask) {
        return table.lock_table_masked_cas(lock_index, old, new_value, mask);
    }

    unsigned int lock_message_to_lock_indexes(VRMaskedCasData lock_message, unsigned int * lock_indexes) {
        // uint64_t mask = reverse_uint64_t(lock_message.mask);
        uint64_t mask = __builtin_bswap64(lock_message.mask);
        unsigned int base_index = lock_message.min_lock_index * 8;
        unsigned int total_locks = 0;

        uint64_t one = 1;
        for (uint64_t i=0; i<BITS_IN_MASKED_CAS; i++) {
            if (mask & (one << i)) {
                // lock_indexes.push_back(base_index + i);
                lock_indexes[total_locks] = base_index + i;
                total_locks++;
            }
        }
        // ALERT("lock_message to indexes","appending a total of %d locks\n", lock_indexes.size());
        return total_locks;
    }

    unsigned int single_read_size_bytes(hash_locations buckets, unsigned int row_size_bytes) {
        return (buckets.distance() + 1) * row_size_bytes;
    }
    
    void lock_indexes_to_buckets(vector<unsigned int> &buckets, vector<unsigned int>& lock_indexes, unsigned int buckets_per_lock) {
        buckets.clear();
        // const uint8_t bits_in_byte = 8;
        //Translate locks by multiplying by the buckets per lock
        for (int i=0; i<lock_indexes.size(); i++) {
            unsigned int lock_index = lock_indexes[i];
            unsigned int bucket = lock_index * buckets_per_lock;
            //fill in buckets that are covered by the lock
            //This is every bucket which is covered by the buckets per lock
            for(int j=0; j<buckets_per_lock; j++) {
                buckets.push_back(bucket + j);
            }
        }
    }

    vector<unsigned int> get_unique_lock_indexes(vector<unsigned int> buckets, unsigned int buckets_per_lock) {
        vector<unsigned int> buckets_chunked_by_lock;
        for (int i=0; i<buckets.size(); i++) {
            unsigned int lock_index = buckets[i] / buckets_per_lock;
            buckets_chunked_by_lock.push_back(lock_index);
        }
        std::sort(buckets_chunked_by_lock.begin(), buckets_chunked_by_lock.end());
        buckets_chunked_by_lock.erase(std::unique(buckets_chunked_by_lock.begin(), buckets_chunked_by_lock.end()), buckets_chunked_by_lock.end());
        return buckets_chunked_by_lock;
    }

    unsigned int byte_aligned_index(unsigned int index) {
        return (index / 8) * 8;
    }

    unsigned int sixty_four_aligned_index(unsigned int index) {
        return (index / 64) * 64;
    }

    unsigned int get_min_sixty_four_aligned_index(vector<unsigned int> &indexes) {
        unsigned int min_index = indexes[0];
        for (int i=1; i<indexes.size(); i++) {
            if (indexes[i] < min_index) {
                printf("new min index\n");
                min_index = indexes[i];
            }
        }
        return sixty_four_aligned_index(min_index);
    }

    #define BITS_PER_BYTE 8

    void break_lock_indexes_into_chunks_fast_context(LockingContext &context) {
        unsigned int min_lock_index;
        const unsigned int bits_in_uint64_t = sizeof(uint64_t) * BITS_PER_BYTE;
        unsigned int chunk_index=0;

        assert(context.lock_indexes_size > 0);
        assert(context.lock_indexes_size < MAX_LOCKS);
        assert(context.lock_indexes);
        assert(context.locks_per_message > 0);

        //Make sure that we have enough space to store the chunks
        // context.fast_lock_chunks.resize(context.lock_indexes_size);
        vector<unsigned int> * current_chunk = &context.fast_lock_chunks[chunk_index];
        current_chunk->clear();
        min_lock_index = sixty_four_aligned_index(context.lock_indexes[0]);
        for(int i=0; i<context.lock_indexes_size; i++) {
            unsigned int lock = context.lock_indexes[i];
            if (
            ((lock - min_lock_index) < bits_in_uint64_t) &&
            (current_chunk->size() < context.locks_per_message)) [[likely]]{
                current_chunk->push_back(lock);
            } else {
                //Fit the vector without reallocating it.
                chunk_index++;
                current_chunk = &context.fast_lock_chunks[chunk_index];
                current_chunk->clear();

                min_lock_index = sixty_four_aligned_index(lock);
                current_chunk->push_back(lock);
            }
        }

        context.number_of_chunks=chunk_index + 1;
        return;

    }


    void lock_chunks_to_masked_cas_data(vector<vector<unsigned int>> lock_chunks, vector<VRMaskedCasData> &masked_cas_data) {
        // vector<VRMaskedCasData> masked_cas_data;
        masked_cas_data.clear();
        assert(masked_cas_data.size() == 0);
        for (int i=0; i<lock_chunks.size(); i++) {
            VRMaskedCasData mcd;
            uint64_t lock = 0;
            uint64_t one = 1;
            unsigned int min_index = get_min_sixty_four_aligned_index(lock_chunks[i]);
            for (int j=0; j<lock_chunks[i].size(); j++) {
                unsigned int normal_index = lock_chunks[i][j] - min_index;
                lock |= (uint64_t)(one << normal_index);
                VERBOSE("lock_chunks_to_masked_cas", "normalized index %u\n", normal_index);
            }

            VERBOSE("lock_chunks_to_masked_cas", "lock is %s", uint64t_to_bin_string(lock).c_str());
            lock = reverse_uint64_t(lock);
            mcd.min_set_lock = lock_chunks[i][0] - min_index;
            mcd.max_set_lock = lock_chunks[i][lock_chunks[i].size()-1] - min_index;
            mcd.min_lock_index = min_index / 8;
            mcd.old = 0;
            mcd.new_value = lock;
            mcd.mask = lock;
            VERBOSE("lock_chunks_to_masked_cas", "pushing back masked cas data %s", mcd.to_string().c_str());
            masked_cas_data.push_back(mcd);
        }
        VERBOSE("lock_chunks_to_masked_cas", "returning %d masked cas data", masked_cas_data.size());
        return;
    }

    void lock_chunks_to_masked_cas_data_context(LockingContext &context) {
        // vector<VRMaskedCasData> masked_cas_data;
        context.lock_list.clear();
        for (int i=0; i<context.number_of_chunks; i++) {
            VRMaskedCasData mcd;
            uint64_t lock = 0;
            uint64_t one = 1;
            unsigned int min_index = get_min_sixty_four_aligned_index(context.fast_lock_chunks[i]);
            // unsigned int min_index = sixty_four_aligned_index(context.fast_lock_chunks[i][0]);
            for (int j=0; j<context.fast_lock_chunks[i].size(); j++) {
                unsigned int normal_index = context.fast_lock_chunks[i][j] - min_index;
                lock |= (uint64_t)(one << normal_index);
            }

            //TODO for performance we can probably use bswap instead of the swap I built
            // lock = reverse_uint64_t(lock);
            lock = __builtin_bswap64(lock);
            mcd.min_set_lock = context.fast_lock_chunks[i][0] - min_index;
            mcd.max_set_lock = context.fast_lock_chunks[i][context.fast_lock_chunks[i].size()-1] - min_index;
            mcd.min_lock_index = min_index / 8;
            mcd.old = 0;
            mcd.new_value = lock;
            mcd.mask = lock;
            // printf("pushing mcd %d %s\n", i, mcd.to_string().c_str());
            context.lock_list.push_back(mcd);
        }
        VERBOSE("lock_chunks_to_masked_cas", "returning %d masked cas data", context.lock_list.size());
        return;
    }


    void unlock_chunks_to_masked_cas_data_context(LockingContext &context) { 
        VERBOSE(__func__, "ENTRY");
        lock_chunks_to_masked_cas_data_context(context);
        for (int i=0; i<context.lock_list.size(); i++) {
            context.lock_list[i].old = context.lock_list[i].new_value;
            context.lock_list[i].new_value = 0;
        }
        return;
    }


    void get_lock_or_unlock_list_fast_context(LockingContext & context){
        assert(context.locks_per_message <= 64);
        // vector<vector<unsigned int>> fast_lock_chunks;
        context.lock_list.clear();
        if (context.fast_lock_chunks.size() == 0) [[unlikely]] {
            context.fast_lock_chunks.resize(MAX_LOCKS);
            for (int i=0; i<MAX_LOCKS; i++) {
                context.fast_lock_chunks[i].resize(context.locks_per_message);
            }
        }
        // if(context.buckets.size() > MAX_LOCKS){
        //     ALERT("get-lock-unlock","what the heck, trying to lock %d buckets locking=%d",context.buckets.size(),context.locking);
        // }
        // assert(context.buckets.size() <= MAX_LOCKS);
        unsigned int unique_lock_indexes[MAX_LOCKS];
        unsigned int virtual_lock_indexes[MAX_LOCKS];

        //Lets make sure that all the buckets are allready sorted
        //I'm 90% sure this will get optimized out in the end
        // for (int i=0; i<context.buckets.size()-1; i++) {
        //     assert(context.buckets[i] < context.buckets[i+1]);
        // }

        //This would be a good point to insert the virtual lock layer

        unsigned int unique_lock_count = get_unique_lock_indexes_fast(context.buckets, context.buckets_per_lock, unique_lock_indexes, MAX_LOCKS);
        bool virutal_lock_table = true;

        if (virutal_lock_table) {
            for (int i=0; i<unique_lock_count; i++) {
                virtual_lock_indexes[i] = unique_lock_indexes[i] % (context.total_physical_locks / 16);
                sort(virtual_lock_indexes, virtual_lock_indexes + unique_lock_count);
            }
            //remove duplicates from the virutal lock indexes
            unsigned int unique_virtual_lock_count = unique(virtual_lock_indexes, virtual_lock_indexes + unique_lock_count) - virtual_lock_indexes;
            if (unique_virtual_lock_count != unique_lock_count) {
                ALERT("virutal lock table", "we have a collision in the virtual lock table\n");
                exit(0);
            }

        }
        // break_lock_indexes_into_chunks_fast(unique_lock_indexes, unique_lock_count, context.locks_per_message, context.fast_lock_chunks);
        context.lock_indexes_size = unique_lock_count;
        context.lock_indexes = unique_lock_indexes;
        break_lock_indexes_into_chunks_fast_context(context);

        // #define CHECK_WITH_OLD_UNLOCK 
        #ifdef CHECK_WITH_OLD_UNLOCK
        //Do the same thing as the old method
        vector<unsigned int> unique_lock_indexes_slow = get_unique_lock_indexes(buckets, buckets_per_lock);
        assert(unique_lock_count == unique_lock_indexes_slow.size());
        for (int i=0; i<unique_lock_count; i++) {
            assert(unique_lock_indexes[i] == unique_lock_indexes_slow[i]);
        }
        //Now we need to make sure that everything is okay for testing purposes
        vector<vector<unsigned int>> test_locks_chunked = break_lock_indexes_into_chunks(unique_lock_indexes_slow, locks_per_message);
        //Test one way
        for (int i = 0; i < test_locks_chunked.size(); i++) {
            for (int j = 0; j < test_locks_chunked[i].size(); j++) {
                printf("1) test_locks_chunked[%d][%d] = %u\n", i, j, test_locks_chunked[i][j]);
                printf("1)      locks_chunked[%d][%d] = %u\n", i, j, fast_lock_chunks[i][j]);
                if (test_locks_chunked[i][j] != fast_lock_chunks[i][j]) {
                    printf("ERROR: lock chunking failed\n");
                    exit(1);
                }
            }
        }
        //Test the other way

        for (int i=0; i<fast_lock_chunks.size(); i++) {
            for (int j=0; j<fast_lock_chunks[i].size(); j++) {
                printf("2) test_locks_chunked[%d][%d] = %u\n", i, j, test_locks_chunked[i][j]);
                printf("2)      locks_chunked[%d][%d] = %u\n", i, j, fast_lock_chunks[i][j]);
                if (test_locks_chunked[i][j] != fast_lock_chunks[i][j]) {
                    printf("ERROR: lock chunking failed 2\n");
                    exit(1);
                }
            }
        }
        #endif

        if (context.locking) {
            lock_chunks_to_masked_cas_data_context(context);
            // lock_chunks_to_masked_cas_data(context.fast_lock_chunks, context.lock_list);
        } else {
            unlock_chunks_to_masked_cas_data_context(context);
        }
        //Masked cas data should be filled at this point

    }

    unsigned int get_unique_lock_indexes_fast(vector<unsigned int> &buckets, unsigned int buckets_per_lock, unsigned int *unique_buckets, unsigned int unique_buckets_size)
    {
        // assert(unique_buckets_size >= buckets.size());
        assert(unique_buckets);

        unique_buckets[0] = buckets[0] / buckets_per_lock;
        unsigned int unique_index = 1;
        for (int i=1; i< buckets.size(); i++){
            unsigned int lock_index = buckets[i] / buckets_per_lock;
            if (unique_buckets[unique_index-1] == lock_index) [[unlikely]]{
                continue;
            } 
            unique_buckets[unique_index] = lock_index;
            unique_index++;
            assert(unique_index < MAX_LOCKS);
        }
        VERBOSE("unique locks", "found a total of %d unique lock indexes out of %d buckets\n", unique_index, buckets.size());
        return unique_index;
    }

    void get_lock_list_fast_context(LockingContext &context){
        context.locking=true;
        get_lock_or_unlock_list_fast_context(context);
    }

    void get_unlock_list_fast_context(LockingContext &context){
        context.locking=false;
        get_lock_or_unlock_list_fast_context(context);
    }


    VRReadData read_request_data(unsigned int start_bucket, unsigned int offset, unsigned int size) {
        VRReadData message;
        message.size = size;
        message.row = start_bucket;
        message.offset = offset;
        return message;
    }


    void multi_bucket_read_message(vector<VRReadData> & messages, hash_locations buckets, unsigned int row_size_bytes) {
        messages.clear();
        unsigned int min_bucket = buckets.min_bucket();
        unsigned int size = single_read_size_bytes(buckets, row_size_bytes);
        VRReadData message = read_request_data(min_bucket, 0, size);
        messages.push_back(message);
    }



    void single_bucket_read_messages(vector<VRReadData> & messages, hash_locations buckets, unsigned int row_size_bytes){
        messages.clear();
        messages.push_back(read_request_data(buckets.primary, 0, row_size_bytes));
        messages.push_back(read_request_data(buckets.secondary, 0, row_size_bytes));
    }


    void read_theshold_message(vector<VRReadData> & messages, hash_locations (*location_function)(Key, unsigned int), Key key, unsigned int read_threshold_bytes,unsigned int table_size,unsigned int row_size_bytes){
        hash_locations buckets = location_function(key, table_size);
        VERBOSE("read_threshold_message", "buckets are %s", buckets.to_string().c_str());
        if (single_read_size_bytes(buckets, row_size_bytes) <= read_threshold_bytes) {
            multi_bucket_read_message(messages, buckets, row_size_bytes);
        } else {
            single_bucket_read_messages(messages, buckets, row_size_bytes);
        }
    }


    VRReadData get_covering_read_from_lock(VRMaskedCasData masked_cas, unsigned int buckets_per_lock, unsigned int row_size_bytes) {
        #define BITS_PER_BYTE 8
        VRReadData read_data;
        hash_locations buckets;


        buckets.primary = (masked_cas.min_set_lock) * buckets_per_lock;
        buckets.secondary = (masked_cas.max_set_lock) * buckets_per_lock + (buckets_per_lock - 1);



        read_data.size= single_read_size_bytes(buckets, row_size_bytes);
        read_data.row = (masked_cas.min_set_lock + (BITS_PER_BYTE * masked_cas.min_lock_index)) * buckets_per_lock;
        // read_data.row = buckets.primary;
        read_data.offset = 0;
        // ALERT("get_covering_read_from_lock", "min_bucket: %d, max_bucket: %d size %d\n", buckets.primary, buckets.secondary, read_data.size);
        return read_data;
    }

    void get_covering_reads_from_lock_list(vector<VRMaskedCasData> &masked_cas_list, vector<VRReadData> &read_data_list, unsigned int buckets_per_lock, unsigned int row_size_bytes) {
        read_data_list.clear();
        for (int i=0; i<masked_cas_list.size(); i++) {
            read_data_list.push_back(get_covering_read_from_lock(masked_cas_list[i], buckets_per_lock, row_size_bytes));
        }
        return;
    }

    VRCasData cas_table_entry_cas_data(unsigned int bucket_index, unsigned int bucket_offset, Key old, Key new_value) {
        VRCasData cas_message;
        cas_message.row = bucket_index;
        cas_message.offset = bucket_offset;
        cas_message.old = old.to_uint64_t();
        cas_message.new_value = new_value.to_uint64_t();
        return cas_message;
    }


    VRCasData next_cas_data(vector<path_element> search_path, unsigned int index) {
        path_element insert_pe = search_path[index];
        path_element copy_pe = search_path[index+1];
        return cas_table_entry_cas_data(insert_pe.bucket_index, insert_pe.offset, insert_pe.key, copy_pe.key);
    }

    void gen_cas_data(vector<path_element>& search_path, vector<VRCasData>& cas_messages) {

        cas_messages.clear();
        for (int i=0; i<search_path.size()-1; i++) {
            cas_messages.push_back(next_cas_data(search_path,i));
        }
        INFO("gen_cas_message", "Generated %d cas messages", cas_messages.size());
    }

    /*------------------------------------ Real RDMA functions ---------------------------- */
}