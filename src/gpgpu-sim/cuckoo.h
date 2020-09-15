// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#ifndef __CUCKOO_H__
#define __CUCKOO_H__

#include <utility>
#include <vector>
#include <functional>
#include <cstdint>
#include "../abstract_hardware_model.h"
#include "../cuda-sim/hashfunc.h"

// Cuckoo Hash timing model

class cuckoo_model {
public:
    typedef addr_t key_t;
    typedef uint32_t ticks_t;
    typedef std::pair<bool, ticks_t> success_t; // <exists, ncycles>
    typedef std::function<unsigned(unsigned, key_t)> hash_fn_t;
    typedef std::vector<hash_fn_t> hash_fns_t;
    typedef std::vector<key_t> table_t;
    typedef std::vector<table_t> tables_t;

public:
    cuckoo_model(unsigned int height, unsigned int n_hashes, unsigned int num_insert_probes,
                 unsigned int stash_size, bool use_overflow_log,
                 unsigned int cuckoo_access_cost, unsigned int mem_access_cost,
                 bool occupancy_threshold_enabled, double occupancy_threshold,
                 bool serialize_overflow_check);
    virtual ~cuckoo_model();
    success_t insert(key_t, bool &, key_t &);
    success_t insert(key_t key); 
    ticks_t remove(key_t);
    success_t lookup(key_t);
    int max_overflow_size() const;
    bool almost_full();

private:
    // returns <way,index>, and <-1,-1> if not found
    std::pair<int,int> find_cuckoo(key_t key) const;
    // returns index, and -1 if not found
    int find_stash(key_t key) const;

private:
    hash_fns_t m_hash_fns;
    tables_t m_tables;
    table_t m_stash;
    unsigned int m_max_insert_probes;
    unsigned int m_num_ways;
    unsigned int m_height;
    unsigned int m_num_hashes;
    bool m_use_overflow_log;
    unsigned int m_cuckoo_access_cost;
    unsigned int m_mem_access_cost;
    unsigned int m_stash_size;
    unsigned int m_max_stash_size;
    bool m_occupancy_threshold_enabled;
    double m_occupancy_threshold;
    bool m_serialize_overflow_check;
    
    int m_last_client;
    unsigned int m_num_occupied_entries;
};

class cuckoo_model_multiple_granularity {
public:
    typedef addr_t key_t;
    typedef uint32_t ticks_t;
    typedef std::pair<bool, ticks_t> success_t; // <exists, ncycles>

public:
    cuckoo_model_multiple_granularity(unsigned int height, unsigned height_4B,
                                      unsigned int n_hashes, unsigned int num_insert_probes,
                                      unsigned int stash_size, bool use_overflow_log,
                                      unsigned int cuckoo_access_cost, unsigned int mem_access_cost,
                                      bool occupancy_threshold_enabled, double occupancy_threshold,
                                      bool serialize_overflow_check, 
                                      unsigned num_aborts_limit, unsigned num_abort_limit_4B,
                                      unsigned granularity, unsigned granularity_log2);
    ~cuckoo_model_multiple_granularity();
    success_t lookup(key_t, std::vector<bool>, unsigned long long);
    success_t insert(key_t);

private:
    cuckoo_model m_cuckoo_model;
    cuckoo_model m_cuckoo_model_4B;
    unsigned m_granularity;
    unsigned m_granularity_log2;
    unsigned m_num_aborts_limit;
    unsigned m_num_aborts_limit_4B;
}; 

#endif
