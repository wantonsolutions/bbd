#include "tables.h"
#include <cstddef>
#include <cassert>
#include <string>
#include <iostream>
#include <vector>
#include <memory>
#include <stdexcept>
#include <bitset>
#include <string.h>
#include "../slib/log.h"
#include "../slib/crc64.h"
#include "../slib/util.h"

using namespace std;

template<typename ... Args>
std::string string_format( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

namespace cuckoo_tables {

    string Key::to_string(){
        string s = "";
        for (int i = 0; i < KEY_SIZE; i++){
            s += string_format("%02x", bytes[i]);
        }
        return s;
    }

    uint64_t Key::to_uint64_t(){
        uint64_t val = 0;
        assert(KEY_SIZE <= 8);
        for (int i = 0; i < KEY_SIZE; i++){
            val |= (uint64_t) bytes[i] << (8 * i);
        }
        return val;
    }

    bool Key::is_empty(){
        uint32_t b_val = *(uint32_t *)bytes;
        return !(b_val || 0x00000000);
    }

    string Value::to_string(){
        string s = "";
        for (int i = 0; i < VALUE_SIZE; i++){
            s += std::to_string(bytes[i]);
        }
        return s;
    }

    bool Value::is_empty(){
        for (int i = 0; i < VALUE_SIZE; i++){
            if (bytes[i] != 0){
                return false;
            }
        }
        return true;
    }

    string Repair_Lease::to_string() {
        string s = "";
        s+= "Lock: " + std::to_string(lock);
        s+= " Meta: " + std::to_string(meta);
        s+= " ID:" + std::to_string(id);
        s+= " Counter:"+ std::to_string(counter);
        return s;
    }

    string Entry::to_string(){
        return key.to_string() + ":" + value.to_string();
    }

    bool Entry::is_empty(){
        return key.is_empty();// && value.is_empty();
    }

    void * Lock_Table::get_lock_table_address() {
        return (void*) _locks;
    }

    unsigned int Lock_Table::get_lock_table_size_bytes(){
        return _total_lock_entries;
    }

    void * Lock_Table::get_lock_pointer(unsigned int lock_index) {
        return (void*) &(_locks[lock_index]);
    }

    void Lock_Table::set_lock_table_address(void * address) {
        _locks = (uint8_t*) address;
    }

    unsigned int Lock_Table::get_total_locks(){
        return _total_locks;
    }

    /*lock table functions*/
    Lock_Table::Lock_Table(){
        _total_locks = 0;
        _total_lock_entries = 0;
        _locks = NULL;

    }

    Lock_Table::Lock_Table(unsigned int memory_size, unsigned int bucket_size, unsigned int buckets_per_lock){

        INFO("Lock Table", "memory_size: %d\n", memory_size);
        assert(memory_size % sizeof(Entry) == 0 );
        unsigned int total_entries = memory_size / sizeof(Entry);
        INFO("Lock Table", "total_entries: %d\n", total_entries);
        assert(total_entries % bucket_size == 0);
        unsigned int table_rows = total_entries / bucket_size;
        INFO("Lock Table", "table_rows: %d\n", table_rows);
        assert(table_rows % buckets_per_lock == 0);
        _total_locks = table_rows / buckets_per_lock;
        INFO("Lock Table", "_total_locks: %d\n", _total_locks);
        const int cas_size = 8;
        const int bits_per_byte=8;
        _total_lock_entries = (_total_locks / bits_per_byte) + cas_size;

        //round lock entries to the nearest value divisible by 8 so everything is cache line alligned
        if (_total_lock_entries % cas_size != 0){
            _total_lock_entries += cas_size - (_total_lock_entries % cas_size);
        }

        INFO("Lock Table", "_total_lock_entries: %d\n", _total_lock_entries);
        _locks = new uint8_t[_total_lock_entries];
        this->unlock_all();
    }

    void Lock_Table::unlock_all(){
        for (unsigned int i = 0; i < _total_lock_entries; i++){
            _locks[i] = 0;
        }
    }

    string pad_string(string s, unsigned int length){
        while (s.length() < length){
            s = " " + s;
        }
        return s;
    }

    string Lock_Table::to_string(){
        string output_string = "";
        for (unsigned int i = 0; i < _total_lock_entries; i++){
            std::bitset<8> x(_locks[i]);
            // printf("bitset: %s\n", x.to_string().c_str());
            string index_string = std::to_string(i*8);
            index_string = pad_string(index_string, 5);
            output_string += index_string + ": " + x.to_string().c_str() + " ";
            if (i % 8 == 7){
                output_string += "\n";
            }
        }
        printf("\n");
        return output_string;
    }

    Repair_Lease_Table::Repair_Lease_Table(){
        _leases = NULL;
        _total_leases = 0;
    } 

    void * Repair_Lease_Table::get_repair_lease_pointer(unsigned int repair_lease_index) {
        return (void*) &(_leases[repair_lease_index]);
    }

    Repair_Lease_Table::Repair_Lease_Table(unsigned int leases){
        _total_leases = leases;
        _leases = new Repair_Lease[_total_leases];
        memset(_leases,0, _total_leases * sizeof(Repair_Lease));
    }

    void * Repair_Lease_Table::get_lease_table_address() {
        return (void *) _leases;
    }

    unsigned int Repair_Lease_Table::get_lease_table_size_bytes() {
        return _total_leases * sizeof(Repair_Lease);
    }

    string Repair_Lease_Table::to_string() {
        string output_string ="";
        for (unsigned int i=0;i<_total_leases;i++){
            output_string += _leases[i].to_string();
        }
        return output_string;
    }

    Table::Table(){
        _memory_size = 0;
        _bucket_size = 0;
        _buckets_per_lock = 0;
        _fill = 0;
        _table_size = 0;
        _table = NULL;
        _lock_table = Lock_Table();
    }

    Table::Table(unsigned int memory_size, unsigned int bucket_size, unsigned int buckets_per_lock){
        assert(memory_size > 0);
        assert(bucket_size > 0);
        assert(memory_size >= bucket_size);
        assert(memory_size >= bucket_size * sizeof(Entry));
        assert(memory_size % sizeof(Entry) == 0);

        _memory_size = memory_size;
        _bucket_size = bucket_size;
        _buckets_per_lock = buckets_per_lock;
        _fill = 0;
        unsigned int total_entries = memory_size / sizeof(Entry);
        assert(total_entries % bucket_size == 0);
        _table_size = int(memory_size / bucket_size) / sizeof(Entry);
        _table = this->generate_bucket_cuckoo_hash_index(memory_size, bucket_size);

        //write zero to all table memory
        memset(_table[0],0, memory_size);
        
        _lock_table = Lock_Table(memory_size, bucket_size, buckets_per_lock);
        _repair_lease_table = Repair_Lease_Table(TOTAL_REPAIR_LEASES);

        #ifdef ROW_CRC
            _entries_per_row= _bucket_size - 1;
        #else
            _entries_per_row = _bucket_size;
        #endif

    }

    // bool Table::operator==(const Table& rhs) const {
    //     if (get_table_size_bytes() != rhs.get_table_size_bytes()){
    //         return false;
    //     }
    //     if (this->get_buckets_per_row() != rhs.get_buckets_per_row()){
    //         return false;
    //     }
    //     for (unsigned int i=0;i<this->get_row_count();i++) {
    //         for (unsigned int j=0;j<this->get_buckets_per_row();j++) {
    //             if (this->get_entry(i,j) != rhs.get_entry(i,j)){
    //                 return false;
    //             }
    //         }
    //     }
    //     return true;
    // }

    void * Table::get_lock_pointer(unsigned int lock_index) {
        return _lock_table.get_lock_pointer(lock_index);
    }

    void * Table::get_repair_lease_pointer(unsigned int repair_lease_index){
        //for now we are only going to work with a single lease, I'm just making sure that we don't do something dumb
        assert(repair_lease_index == 0);
        return _repair_lease_table.get_repair_lease_pointer(repair_lease_index);
    }

    Entry ** Table::get_underlying_table(){
        return _table;
    }

    void Table::set_underlying_table(Entry ** table){
        _table = table;
    }


    void * Table::get_underlying_lock_table_address() {
        return _lock_table.get_lock_table_address();
    }

    void Table::set_underlying_lock_table_address(void * address) {
        _lock_table.set_lock_table_address(address);
    }
    
    unsigned int Table::get_underlying_lock_table_size_bytes() {
        return _lock_table.get_lock_table_size_bytes();
    }


    void * Table::get_underlying_repair_lease_table_address() {
        return get_repair_lease_pointer(0);
        // return _repair_lease_table.get_lease_table_address();
    }

    unsigned int Table::get_underlying_repair_lease_table_size_bytes() {
        return _repair_lease_table.get_lease_table_size_bytes();
    }

    void Table::unlock_all(){
        _lock_table.unlock_all();
        return;
    }

    unsigned int Table::get_total_locks(){
        return _lock_table.get_total_locks();
    }

    string Table::to_string(){
        string output_string = "";
        output_string += "Table\n";
        for (unsigned int i = 0; i < _table_size; i++){
            output_string += row_to_string(i);
            output_string += "\n";
        }
        output_string += "\nLock Table\n";
        output_string += _lock_table.to_string() + "\n";
        output_string += _repair_lease_table.to_string() + "\n";
        output_string += std::to_string(get_fill_percentage()) + "% full\n";
        return output_string;
    }

    string Table::row_to_string(unsigned int row) {
        assert(row < _table_size);
        string output_string = "";
        string index_string = std::to_string(row);
        index_string = pad_string(index_string, 5);
        output_string += index_string  +  ") ";
        for (unsigned int j = 0; j < _bucket_size; j++){
            output_string += "[" + _table[row][j].to_string() += "]";
        }
        return output_string;
    }

    void Table::print_row(unsigned int row){
        cout << row_to_string(row) << endl;
        return;
    }

    void Table::print_table(){
        cout << to_string() << endl;
        return;
    }

    void Table::print_lock_table(){
        cout << _lock_table.to_string() << endl;
        return;
    }


    unsigned int Table::get_table_size_bytes() const {
        return _memory_size;
    }

    unsigned int Table::get_row_count() const{
        return _table_size;
    }

    unsigned int Table::get_buckets_per_row() const{
        return _bucket_size;
    }

    unsigned int Table::get_entries_per_row() const {
        return _entries_per_row;
    }

    unsigned int Table::get_bucket_size(){
        return _bucket_size * sizeof(Entry);
    }

    unsigned int Table::row_size_bytes(){
        return this->n_buckets_size(_bucket_size);
    }

    unsigned int Table::n_buckets_size(unsigned int n_buckets) {
        return sizeof(Entry) * n_buckets;
    }

    unsigned int Table::get_entry_size_bytes(){
        return sizeof(Entry);
    }

    Entry Table::get_entry(unsigned int bucket_index, unsigned int offset) const{
        return _table[bucket_index][offset];
    }

    Entry * Table::get_entry_pointer(unsigned int bucket_index, unsigned int offset){
        // printf("get_entry_pointer: bucket_index: %d, offset: %d\n", bucket_index, offset);
        return _table[bucket_index] + offset;
    }

    void Table::set_entry(unsigned int bucket_index, unsigned int offset, Entry entry){
        Entry old = _table[bucket_index][offset];
        _table[bucket_index][offset] = entry;

        if (old.is_empty()){
            _fill++;
        }
        if (entry.is_empty()){
            _fill--;
        }
    }

    void Table::set_entry_with_crc(unsigned int bucket_index, unsigned int offset, Entry &entry){
        #ifndef ROW_CRC
        printf("not doing row crc, exiting\n");
        exit(1);
        #endif

        assert(offset != _entries_per_row);
        _table[bucket_index][offset] = entry;
        _table[bucket_index][_entries_per_row].set_as_uint64_t(crc64_row(bucket_index));
        assert(_table[bucket_index][offset] == entry);
    }

    uint64_t Table::crc64_row(unsigned int row) {
        unsigned char * row_pointer = (unsigned char *) &(_table[row][0]);
        return  crc64(0,row_pointer, n_buckets_size(_entries_per_row));
    }

    bool Table:: crc_valid_row(unsigned int row) {
        if (bucket_is_empty(row)){
            return true;
        }
        uint64_t crc_row = crc64_row(row);
        Entry e = get_entry(row,get_entries_per_row());

        uint64_t existing_crc = e.get_as_uint64_t();
        if (crc_row == existing_crc) {
            return true;
        } 

        // ALERT("crc_valid_row", "FAILED row %d current %lX calculated %lX (e = %s) entries per row (%d)", row, existing_crc,crc_row,e.to_string().c_str(), get_entries_per_row());
        // e = get_entry(row,get_entries_per_row());
        // ALERT("e again", "e = %s",e.to_string().c_str());
        // print_row(row);
        return false;
    }

    bool Table::bucket_has_empty(unsigned int bucket_index){
        for (int i = _entries_per_row-1; i >= 0; i--){
            if (_table[bucket_index][i].is_empty()){
                return true;
            }
        }
        return false;
    }

    int Table::crc_valid() {
        for (int i = 0; i < _table_size; i++){
            if (!crc_valid_row(i)){
                return i;
            }
        }
        return -1;
    }

    bool Table::bucket_is_empty(unsigned int row) {
        for(int i=0;i<_bucket_size;i++){
            if(!_table[row][i].is_empty()) {
                return false;
            }
        }
        return true;
    }

    unsigned int Table::get_first_empty_index(unsigned int bucket_index){
        unsigned int empty_index = -1;
        for (unsigned int i = 0; i < _entries_per_row; i++){
            if (_table[bucket_index][i].is_empty()){
                empty_index = i;
                break;
            }
        }
        return empty_index;
    }

    bool Table::bucket_contains(unsigned int bucket_index, Key &key){
        for (unsigned int i = 0; i < _entries_per_row; i++){
            if (_table[bucket_index][i].key == key){
                return true;
            }
        }
        return false;
    }

    bool Table::key_is_at(unsigned int bucket_index, unsigned int offset, Key &key){
        return _table[bucket_index][offset].key == key;
    }


    int Table::get_keys_offset_in_row(unsigned int row, Key &key) {
        unsigned int index;
        for (int i=0; i < _entries_per_row; i++) {
            if(_table[row][i].key == key) {
                return i;
            }
        }
        ALERT("DEATH!", "Key not found in row, don't call this function like this.");
        assert(false);
        return -1;
    }

    bool Table::get_index_and_offset(Key &key, unsigned int row1, unsigned int row2, unsigned int *index, unsigned int *offset){
        bool b1 = bucket_contains(row1, key);
        if (b1) {
            *index = row1;
            *offset = get_keys_offset_in_row(row1, key);
            return true;
        }
        bool b2 = bucket_contains(row2, key);
        if (b2) {
            *index = row2;
            *offset = get_keys_offset_in_row(row2, key);
            return true;
        }
        return false;
    }

    bool Table::contains(Key key){
        for (unsigned int i = 0; i < _table_size; i++){
            if (bucket_contains(i, key)){
                return true;
            }
        }
        return false;
    }

    float Table::get_fill_percentage(){
        unsigned int current_fill = 0;
        unsigned int max_fill = _table_size * _entries_per_row;
        for (unsigned int i = 0; i < _table_size; i++){
            for (unsigned int j = 0; j < _entries_per_row; j++){
                if (!_table[i][j].is_empty()){
                    current_fill++;
                }
            }
        } 
        return float(current_fill) / float(max_fill);
    }

    float Table::get_fill_percentage_fast() {
        unsigned int max_fill = _table_size * _entries_per_row;
        return float(_fill) / float(max_fill);
    }

    bool Table::full(){
        return _fill == _table_size * _entries_per_row;
    }

    Entry ** Table::generate_bucket_cuckoo_hash_index(unsigned int memory_size, unsigned int bucket_size){
        VERBOSE("Table", "Table::generate_bucket_cuckoo_hash_index");
        //Sanity checking asserts
        assert(memory_size > 0);
        assert(bucket_size > 0);
        assert(memory_size >= bucket_size);
        assert(memory_size >= bucket_size * sizeof(Entry));
        assert(memory_size % sizeof(Entry) == 0);
        unsigned int total_entries = memory_size / sizeof(Entry);
        assert(total_entries % bucket_size == 0);
        unsigned int total_rows = int(memory_size / bucket_size) / sizeof(Entry);
        INFO("Table", "memory_size: %d",memory_size);
        INFO("Table", "bucket_size: %d", bucket_size);
        INFO("Table", "total_entries: %d",total_entries);

        //Allocate memory for the table
        unsigned int nrows = total_rows;
        unsigned int ncols = bucket_size;
        Entry* pool = NULL;


        try {
            Entry ** ptr = new Entry*[nrows];
            pool = new Entry[nrows * ncols]{Entry()};

            for (unsigned i = 0; i < nrows; ++i, pool += ncols ){
                // printf("pool [%d] is %p\n", i, pool);
                ptr[i] = pool;
            }
            return ptr;
        } catch (std::bad_alloc& ba) {
            ALERT("Table", "bad_alloc caught: %s", ba.what());
            return NULL;
        }
        return NULL;

    }
    unsigned int Table::absolute_index_to_bucket_index(unsigned int absolute_index){
        return absolute_index / _bucket_size;
    }
    unsigned int Table::absolute_index_to_bucket_offset(unsigned int absolute_index){
        return absolute_index % _bucket_size;
    }
    void Table::assert_operation_in_table_bound(unsigned int bucket_index, unsigned int offset, unsigned int read_size){
        unsigned int total_indexes = read_size / sizeof(Entry);
        unsigned int max_read = bucket_index * offset + total_indexes;
        unsigned int table_bound = _table_size * _bucket_size;
        assert (max_read <= table_bound);
        return;
    }
    bool Table::contains_duplicates(){
        cout << "Table::contains_duplicates NOT IMPLEMENTED" << endl;
        //todo implement
        return true;
    }

    vector<Duplicate_Entry> Table::get_duplicates(){
        cout << "Table::get_duplicates NOT IMPLEMENTED" << endl;
        //todo implement
        return vector<Duplicate_Entry>();
    }
}