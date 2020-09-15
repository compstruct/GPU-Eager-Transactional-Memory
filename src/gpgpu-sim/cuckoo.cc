// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#include <cassert>
#include "cuckoo.h"
#include "../cuda-sim/tm_manager_internal.h"

enum key_constants_t { EMPTY_KEY = -1 };
extern tm_global_statistics g_tm_global_statistics;

cuckoo_model::cuckoo_model(unsigned int height, unsigned int n_hashes, unsigned int max_insert_probes,
                           unsigned int stash_size, bool use_overflow_log,
                           unsigned int cuckoo_access_cost, unsigned int mem_access_cost,
                           bool occupancy_threshold_enabled, double occupancy_threshold,
                           bool serialize_overflow_check)
    : m_tables(n_hashes, table_t(height, EMPTY_KEY)),
      m_num_hashes(n_hashes),
      m_max_insert_probes(max_insert_probes),
      m_num_ways(n_hashes),
      m_height(height),
      m_stash_size(stash_size),
      m_use_overflow_log(use_overflow_log),
      m_cuckoo_access_cost(cuckoo_access_cost),
      m_mem_access_cost(mem_access_cost),
      m_max_stash_size(0),
      m_last_client(0),
      m_num_occupied_entries(0),
      m_occupancy_threshold_enabled(occupancy_threshold_enabled),
      m_occupancy_threshold(occupancy_threshold),
      m_serialize_overflow_check(serialize_overflow_check) {
          assert(height > 0);
          assert(max_insert_probes > 0);
          assert(n_hashes > 0);
          init_h3_hash(height);
          if (n_hashes == 1) {
              m_hash_fns.push_back(h3_hash1);
          } else if (n_hashes == 2) {
              m_hash_fns.push_back(h3_hash1);
              m_hash_fns.push_back(h3_hash2);
          } else if (n_hashes == 3) {
              m_hash_fns.push_back(h3_hash1);
              m_hash_fns.push_back(h3_hash2);
              m_hash_fns.push_back(h3_hash3);
          } else if (n_hashes == 4) {
              m_hash_fns.push_back(h3_hash1);
              m_hash_fns.push_back(h3_hash2);
              m_hash_fns.push_back(h3_hash3);
              m_hash_fns.push_back(h3_hash4);
          } else {
              abort();
          }
      }

cuckoo_model::~cuckoo_model() {}

auto cuckoo_model::lookup(key_t key) -> success_t {
    ticks_t ticks = m_cuckoo_access_cost; // number of references to memory
    if (find_cuckoo(key).first != -1) { // in cuckoo hash
        return std::make_pair(true, ticks);
    } else { // look in the stash
        auto ix = find_stash(key);
        for (int i = 0; i < m_stash.size(); ++i) {
            if (i >= m_stash_size and m_serialize_overflow_check) 
                ticks += m_mem_access_cost;
            if (m_stash[i] == key) {
                if (i >= m_stash_size) {
                    m_stash.erase(m_stash.begin() + i);
                    g_tm_global_statistics.m_num_cuckoo_table_stash_size.add2bin(m_stash.size());
                    if (m_stash.size() > m_stash_size) {
                        g_tm_global_statistics.m_num_cuckoo_table_overflow_entries.add2bin(m_stash.size() - m_stash_size);
                    }
                    ticks += insert(key).second;
                }
                if (!m_serialize_overflow_check and i >= m_stash_size)
                    ticks += m_mem_access_cost;
                return std::make_pair(true, ticks);
            }
        }
        if (!m_serialize_overflow_check and m_stash.size() > m_stash_size)
            ticks += m_mem_access_cost;
        return std::make_pair(false, ticks);
    }
}

// used for 4B cuckoo table
auto cuckoo_model::insert(key_t key, bool &evict, key_t &evict_key) -> success_t {
    for (auto way = 0; way < m_num_ways; ++way) {
        auto hash = m_hash_fns[way](m_height, key);
        if (m_tables[way][hash] == EMPTY_KEY) {
            m_tables[way][hash] = key;
            m_num_occupied_entries++;
            evict = false;
            return std::make_pair(true, m_cuckoo_access_cost);
        } 
    }
    
    for (auto way = 0; way < m_num_ways; ++way) {
        auto hash = m_hash_fns[way](m_height, key);
        key_t key_4B = m_tables[way][hash];
        bool could_replace = logical_temporal_conflict_detector::get_singleton().could_replace_4B(key_4B);
        if (could_replace) {
            evict = true;
            evict_key = key_4B;
            m_tables[way][hash] = key;
            return std::make_pair(true, m_cuckoo_access_cost);
        }
    }
    evict = false;
    return std::make_pair(false, m_cuckoo_access_cost);
}

auto cuckoo_model::insert(key_t key) -> success_t {
    // first check all ways in parallel
    for (auto way = 0; way < m_num_ways; ++way) {
        unsigned next_client = (way + m_last_client) % m_num_ways;
        auto hash = m_hash_fns[next_client](m_height, key);
        if (m_tables[next_client][hash] == EMPTY_KEY) {
            m_tables[next_client][hash] = key;
            m_num_occupied_entries++;
            float occupancy_rate = ((float)m_num_occupied_entries)/(m_height*m_num_ways);
            g_tm_global_statistics.m_cuckoo_table_occupancy_rate = occupancy_rate;
            m_last_client = (next_client + 1) % m_num_ways;
            g_tm_global_statistics.m_num_cuckoo_table_insert_probes.add2bin(1);
            return std::make_pair(true, m_cuckoo_access_cost);
        }
    }
    
    // then play the cuckoo eviction game
    ticks_t ticks = 0; // number of references to memory
    for (auto probe = 0; probe < m_max_insert_probes; ++probe) {
        ticks += m_cuckoo_access_cost; // includes parallel check above
        auto next_client = (m_last_client + probe) % m_num_ways;
        auto hash = m_hash_fns[next_client](m_height, key);
        if (m_tables[next_client][hash] == EMPTY_KEY) {
            m_tables[next_client][hash] = key;
            m_num_occupied_entries++;
            float occupancy_rate = ((float)m_num_occupied_entries)/(m_height*m_num_ways);
            g_tm_global_statistics.m_cuckoo_table_occupancy_rate = occupancy_rate;
            assert(ticks != 0);
            m_last_client = (next_client + 1) % m_num_ways;
            g_tm_global_statistics.m_num_cuckoo_table_insert_probes.add2bin(probe + 1);
            return std::make_pair(true, ticks);
        } else {
            std::swap(key, m_tables[next_client][hash]);
            bool pending = logical_temporal_conflict_detector::get_singleton().something_pending(key);
            bool over_occupancy_threshold = ((double)m_num_occupied_entries)/(m_height*m_num_ways) > m_occupancy_threshold;
            bool last_probe = (probe == m_max_insert_probes - 1);
            if (!pending and ((m_occupancy_threshold_enabled && over_occupancy_threshold) || last_probe)) {
                logical_temporal_conflict_detector::get_singleton().logical_timestamp_replacement(key);
                m_last_client = (next_client + 1) % m_num_ways;
                g_tm_global_statistics.m_num_cuckoo_table_insert_probes.add2bin(probe + 1);
                g_tm_global_statistics.m_tot_cuckoo_table_replacement++;
                return std::make_pair(true, ticks);
            }
        }
        g_tm_global_statistics.m_num_cuckoo_table_insert_probes.add2bin(probe + 1);
    }

    if (m_use_overflow_log or m_stash.size() < m_stash_size) {
        // account for accessing the overflow log in memory if stash full
        if (m_stash.size() >= m_stash_size) {
            for (auto i = 0; i < m_stash_size; i++) {
                if (logical_temporal_conflict_detector::get_singleton().something_pending(m_stash[i]) == false) {
                    std::swap(key, m_stash[i]);
                    logical_temporal_conflict_detector::get_singleton().logical_timestamp_replacement(key);
                    ticks += m_cuckoo_access_cost;
                    g_tm_global_statistics.m_tot_cuckoo_table_replacement++;
                    return std::make_pair(true, ticks);
                }
            }
            ticks += m_mem_access_cost;
        }
        //m_stash.insert(m_stash.begin(), key);
        m_stash.push_back(key);
        g_tm_global_statistics.m_num_cuckoo_table_stash_size.add2bin(m_stash.size());
        if (m_stash.size() > m_stash_size) {
            g_tm_global_statistics.m_num_cuckoo_table_overflow_entries.add2bin(m_stash.size() - m_stash_size);
        } else {
            g_tm_global_statistics.m_num_cuckoo_table_nonoverflow_entries.add2bin(m_stash.size());
        }
        if (m_stash.size() > m_max_stash_size) {
            m_max_stash_size = m_stash.size();
        }
        assert(ticks != 0);
        return std::make_pair(true, ticks);
    } else { // fail insertion
        return std::make_pair(false, ticks);
    }
}

auto cuckoo_model::remove(key_t key) -> ticks_t {
    auto cuckoo_ix = find_cuckoo(key);
    if (cuckoo_ix.first != -1) { // clear entry in cuckoo hash
        m_tables[cuckoo_ix.first][cuckoo_ix.second] = EMPTY_KEY;
        return m_cuckoo_access_cost;
    } else { // clear entry in stash
        auto stash_ix = find_stash(key);
        if (stash_ix != -1) { // delete from stash
            m_stash.erase(m_stash.begin() + stash_ix);
            // 1 tick for cuckoo lookup + 1 for entry 0 + entry ix
            return (m_cuckoo_access_cost +
                    (stash_ix < m_stash_size ?
                        0 : m_mem_access_cost * (1+stash_ix-m_stash_size)));
        } else {
            assert(0); // commit log has phantom entry!
        }
    }
}

int cuckoo_model::max_overflow_size() const {
    return ((m_max_stash_size > m_stash_size) ?
            (m_max_stash_size - m_stash_size) : 0);
}

std::pair<int,int> cuckoo_model::find_cuckoo(key_t key) const {
    for (auto way = 0; way < m_num_ways; ++way) {
        auto hash = m_hash_fns[way](m_height, key);
        if (m_tables[way][hash] == key) {
            return std::make_pair(way, hash);
        }
    }
    return std::make_pair(-1, -1);
}

int cuckoo_model::find_stash(key_t key) const {
    for (auto i = 0; i < m_stash.size(); ++i) {
        if (m_stash[i] == key) return i;
    }
    return -1;
}

bool cuckoo_model::almost_full() {
    return m_num_occupied_entries > ((m_height * m_num_hashes) * 0.8);
}

cuckoo_model_multiple_granularity::cuckoo_model_multiple_granularity(
    unsigned int height, unsigned height_4B, 
    unsigned int n_hashes, unsigned int max_insert_probes,
    unsigned int stash_size, bool use_overflow_log,
    unsigned int cuckoo_access_cost, unsigned int mem_access_cost,
    bool occupancy_threshold_enabled, double occupancy_threshold,
    bool serialize_overflow_check, 
    unsigned num_aborts_limit, unsigned num_aborts_limit_4B,
    unsigned granularity, unsigned granularity_log2) :
m_cuckoo_model(height, n_hashes, max_insert_probes,
               stash_size, use_overflow_log, cuckoo_access_cost, mem_access_cost,
               occupancy_threshold_enabled, occupancy_threshold, serialize_overflow_check),
m_cuckoo_model_4B(height_4B, n_hashes, 1, 0, false, 
                  cuckoo_access_cost, mem_access_cost, 0, 0.0, false)
{
    assert(granularity%4 == 0);
    m_granularity = granularity;
    m_granularity_log2 = granularity_log2;
    m_num_aborts_limit = num_aborts_limit;
    m_num_aborts_limit_4B = num_aborts_limit_4B;
}

cuckoo_model_multiple_granularity::~cuckoo_model_multiple_granularity() {}

auto cuckoo_model_multiple_granularity::lookup(key_t key, std::vector<bool> check_byte_mask, unsigned long long cycle) -> success_t {
    ticks_t ticks = 0;
    success_t cuckoo_model_lookup_success = m_cuckoo_model.lookup(key);
    ticks += cuckoo_model_lookup_success.second;
    unsigned num_aborts = logical_temporal_conflict_detector::get_singleton().get_num_aborts(key);
    if (cuckoo_model_lookup_success.first) {
        bool is_splited = logical_temporal_conflict_detector::get_singleton().is_splited(key);
        bool cuckoo_model_4B_full = m_cuckoo_model_4B.almost_full();
        if (is_splited) {
            for (unsigned i = 0; i < m_granularity; i += 4) {
                bool need_check = check_byte_mask[i];
                bool word_split = logical_temporal_conflict_detector::get_singleton().is_splited(key, i/4);
                if (need_check) {
                   key_t word_key = ((key << m_granularity_log2) + i) >> 2;
                   if (word_split) {
                       success_t cuckoo_model_4B_lookup_success = m_cuckoo_model_4B.lookup(word_key);
                       assert(cuckoo_model_4B_lookup_success.first);
                       ticks += cuckoo_model_4B_lookup_success.second;
                       bool could_replace_4B = logical_temporal_conflict_detector::get_singleton().could_replace_4B(word_key);
                       if (could_replace_4B) {
                           m_cuckoo_model_4B.remove(word_key);
                           logical_temporal_conflict_detector::get_singleton().merge_entry(word_key, i/4);
                       } 
                   } else if (num_aborts > m_num_aborts_limit and !cuckoo_model_4B_full) {
                       bool evict = false;
                       unsigned evict_key = 0;
                       success_t cuckoo_model_4B_insert_success = m_cuckoo_model_4B.insert(word_key, evict, evict_key);
                       ticks += cuckoo_model_4B_insert_success.second;
                       if (cuckoo_model_4B_insert_success.first) {
                           logical_temporal_conflict_detector::get_singleton().alloc_entry(word_key, i/4);
                           if (evict) {
                              key_t evict_chunk_key = evict_key >> (m_granularity_log2 - 2);
                              cuckoo_model_lookup_success = m_cuckoo_model.lookup(evict_chunk_key);
                              assert(cuckoo_model_lookup_success.first);
                              ticks += cuckoo_model_lookup_success.second;
                              unsigned evict_key_index = evict_key & ((1 << (m_granularity_log2 -2)) -1);
                              logical_temporal_conflict_detector::get_singleton().merge_entry(evict_key, evict_key_index);
                           }
                       } 
                   }
                } 
            }
        } else {
            if (num_aborts > m_num_aborts_limit and !cuckoo_model_4B_full) {
                g_tm_global_statistics.m_tot_cuckoo_table_splited_addr++;
                for (unsigned i = 0; i < m_granularity; i += 4) {
                    bool need_check = check_byte_mask[i];
                    if (need_check) {
                       key_t word_key = ((key << m_granularity_log2) + i) >> 2;
                       bool evict = false;
                       unsigned evict_key = 0;
                       success_t cuckoo_model_4B_insert_success = m_cuckoo_model_4B.insert(word_key, evict, evict_key);
                       ticks += cuckoo_model_4B_insert_success.second;
                       if (cuckoo_model_4B_insert_success.first) {
                           logical_temporal_conflict_detector::get_singleton().alloc_entry(word_key, i/4);
                           if (evict) {
                              key_t evict_chunk_key = evict_key >> (m_granularity_log2 - 2);
                              cuckoo_model_lookup_success = m_cuckoo_model.lookup(evict_chunk_key);
                              assert(cuckoo_model_lookup_success.first);
                              ticks += cuckoo_model_lookup_success.second;
                              unsigned evict_key_index = evict_key & ((1 << (m_granularity_log2 -2)) -1);
                              logical_temporal_conflict_detector::get_singleton().merge_entry(evict_key, evict_key_index);
                           }
                       } 
                    } 
                }
            }
        }
        return std::make_pair(true, ticks);
    } else {
        return std::make_pair(false, ticks);
    }
}

auto cuckoo_model_multiple_granularity::insert(key_t key) -> success_t {
    return m_cuckoo_model.insert(key);
}
