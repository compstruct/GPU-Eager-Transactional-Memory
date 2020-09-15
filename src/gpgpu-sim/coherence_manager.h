/*
 * coherence_manager.h
 *
 *  Created on: Apr 28, 2011
 *      Author: inder
 */

#ifndef COHERENCE_MANAGER_H_
#define COHERENCE_MANAGER_H_

#include "shader.h"
#include <vector>
#include <map>
#include <set>
#include "histogram.h"
#include "../cuda-sim/tm_manager.h"
#include "../cuda-sim/tm_manager_internal.h"

enum coherence_cache_block_state {
        COH_INVALID,
        COH_SHARED,
        COH_MODIFIED
    };

    enum coherence_cache_request_status {
        COH_HIT,
        COH_MISS,
        COH_MISS_EVICTION
    };

struct coherence_cache_block_t {
    coherence_cache_block_t()
    {
        m_tag=0;
        m_block_addr=0;
        m_last_access_time=0;
        m_alloc_time=0;
        m_status=COH_INVALID;
    }
    void allocate( new_addr_type tag, new_addr_type block_addr, coherence_cache_block_state status, unsigned time )
    {
        m_tag=tag;
        m_block_addr=block_addr;
        m_last_access_time=time;
        m_alloc_time=time;
        assert(status == COH_SHARED || status == COH_MODIFIED);
        m_status=status;
    }
    void access(unsigned time)
    {
        assert( m_status != COH_INVALID );
        m_last_access_time = time;
    }
    void mark_shared( unsigned time )
    {
        assert( m_status != COH_INVALID );
        m_status=COH_SHARED;
        m_last_access_time=time;
    }
    void mark_modified( unsigned time )
    {
        assert( m_status != COH_INVALID );
        m_status=COH_MODIFIED;
        m_last_access_time=time;
    }
    void invalidate()
    {
        m_status = COH_INVALID;
    }
    void print(FILE *fout)
    {
        fprintf(fout, "blk(%#08llx): m_alloc_time=%d, m_last_access_time=%d, ",
                m_block_addr, m_alloc_time, m_last_access_time);
        switch(m_status) {
        case COH_INVALID: fprintf(fout, "m_status=INVALID, "); break;
        case COH_SHARED: fprintf(fout, "m_status=SHARED, "); break;
        case COH_MODIFIED: fprintf(fout, "m_status=MODIFIED, "); break;
        };
    }

    new_addr_type    m_tag;
    new_addr_type    m_block_addr;
    unsigned         m_last_access_time;
    unsigned         m_alloc_time;
    coherence_cache_block_state    m_status;
};

struct coherence_directory_block_t {
    coherence_directory_block_t()
    {
        m_block_addr=0;
        m_last_access_time=0;
        m_alloc_time=0;
        m_status=COH_INVALID;
        m_sharers.clear();
        m_nvalidsharers = 0;
    }

    void reset_sharers() {
        m_sharers.clear();
        m_nvalidsharers = 0;
    }

    unsigned count_sharers() {
        return m_sharers.size();
    }

    new_addr_type    m_block_addr;
    unsigned        m_last_access_time;
    unsigned        m_alloc_time;
    coherence_cache_block_state    m_status;
    std::set<unsigned>      m_sharers;
    unsigned        m_nsharers;
    unsigned        m_nvalidsharers;
};

class coherence_tag_array {
public:
    coherence_tag_array( const cache_config &config, unsigned uid);
    ~coherence_tag_array();

    // Call this to allcoate memory before first use
    void allocate_space();
    bool is_allocated() { return m_allocated; }

    coherence_cache_request_status access( new_addr_type addr, unsigned time, bool write, new_addr_type &evicted_block_address);

    void flush(); // flash invalidate all entries
    void invalidate(new_addr_type block_addr); // invalidate a single block

    unsigned get_stats_accesses() { return m_stats_accesses; }
    unsigned get_stats_hits() { return m_stats_hits; }
    unsigned get_stats_misses() { return m_stats_misses; }
    unsigned get_stats_evictions() { return m_stats_evictions; }
    unsigned get_stats_evictions_modified() { return m_stats_evictions_modified; }
    unsigned get_stats_evictions_shared() { return m_stats_evictions_shared; }
    unsigned get_stats_invalidations() { return m_stats_invalidations; }

private:
    coherence_cache_request_status probe( new_addr_type addr, unsigned &idx ) const;

protected:

    bool m_allocated;

    const cache_config &m_config;

    coherence_cache_block_t *m_lines; /* nset x assoc lines in total */

    unsigned m_uid; // unique thread id of thread using this cache

    // Stats
    unsigned m_stats_accesses;
    unsigned m_stats_hits;
    unsigned m_stats_misses;
    unsigned m_stats_evictions;
    unsigned m_stats_evictions_modified;
    unsigned m_stats_evictions_shared;
    unsigned m_stats_invalidations;
};


class coherence_manager
{
public:
	coherence_manager( const shader_core_config *shader_config );
	~coherence_manager();
	void resize_cache_array(unsigned new_size);
	void mem_access(unsigned hwtid, new_addr_type addr, unsigned time, bool write, ptx_thread_info* thread_state);
	void flush_cache(unsigned hwtid);
	void print_stats(FILE *fp);
	void dump_sharer_histogram(unsigned time);

	// TM callbacks from shader
	void tm_commited(unsigned hwtid, unsigned hwwarpid, ptx_thread_info* thread_state);
    void tm_warp_commited(unsigned hwwarpid);
    void tm_aborted(unsigned hwtid, ptx_thread_info* thread_state);

private:
	// Add a sharer and return number of invalidations
    unsigned add_sharer(unsigned hwtid, new_addr_type block_addr, unsigned time, bool write, std::set<unsigned> &invalidation_vector);

    // Remove a sharer - return true if block was invalidated
    bool remove_sharer(unsigned hwtid, new_addr_type block_addr);

    void record_invalidation_stats(unsigned hwtid_owner, std::set<unsigned> invalidation_vector, bool is_tm_inst, bool write);

    linear_histogram get_directory_sharer_histogram();

    // Members

	std::vector<coherence_tag_array*> m_coherence_L0_caches;
	const shader_core_config *m_shader_config;
	unsigned m_nthreads;

	std::map<new_addr_type, coherence_directory_block_t*> m_directory_full;

	// statistics
	unsigned m_stats_invalidations_total;
	unsigned m_stats_invalidations_samecore;
	unsigned m_stats_invalidations_diffcore;
    unsigned m_stats_invalidations_total_tm;
    unsigned m_stats_invalidations_samecore_tm;
    unsigned m_stats_invalidations_diffcore_tm;

    unsigned m_stats_invalidations_on_writes_total;
    linear_histogram m_stats_hist_sharers_invalidated_on_write;
    linear_histogram m_stats_hist_sharers_invalidated_on_write_tm;
    linear_histogram m_stats_hist_sharers_invalidated_on_write_samecore;
    linear_histogram m_stats_hist_sharers_invalidated_on_write_diffcore;
    linear_histogram m_stats_hist_sharers_invalidated_on_write_samecore_tm;
    linear_histogram m_stats_hist_sharers_invalidated_on_write_diffcore_tm;


    unsigned m_stats_accesses;
    unsigned m_stats_accesses_tm;
    unsigned m_stats_reads;
    unsigned m_stats_writes;
    unsigned m_stats_reads_tm;
    unsigned m_stats_writes_tm;

    std::set<unsigned> m_stats_directory_all_blocks;                                // to keep a list of all blocks allocated
    std::map<new_addr_type,unsigned> m_stats_directory_all_blocks_invalidations;    // keep track of invalidations per block address
    std::map<new_addr_type,unsigned> m_stats_directory_all_blocks_nsharersmax;      // keep track of max sharers per block address

    std::set<unsigned> m_stats_sw_threads;                                   // keep track of sw threads used
    std::set<unsigned> m_stats_hw_threads;                                   // keep track of hw threads used
    std::set<unsigned> m_stats_txns;                                         // keep track of txns used

    std::map<unsigned, std::set<unsigned> > m_stats_txns_per_hw_thread;        // keep track of # of transactions per hw thread
    std::map<unsigned, unsigned> m_stats_capacity_evictions_per_hw_thread;     // keep track of # of L0 evictions per hw thread

    std::map<unsigned, std::set<unsigned> > m_stats_txns_per_sw_thread;        // keep track of # of transactions per sw thread
    std::map<unsigned, unsigned> m_stats_capacity_evictions_per_sw_thread;     // keep track of # of L0 evictions per sw thread

    std::map<unsigned, unsigned> m_stats_capacity_evictions_per_txn;        // keep track of # of L0 evictions per unique txn


    std::map<unsigned, std::set<unsigned> > m_build_txns_warp_group;        // use this to build txns warp groups
    std::list<std::set<unsigned> > m_stats_txns_warp_group;                  // use this to store txns warp groups
    std::list<unsigned> m_stats_txns_warp_group_evictions;                   // keep track of # of evictions in group

    // Directory's view of caches
    unsigned m_stats_hits;
    unsigned m_stats_misses;

    // Directory's view of TM calls
    unsigned m_stats_tm_commits;
    unsigned m_stats_tm_aborts;

    unsigned m_stats_tm_access_mode_reads;


    // Other members
    FILE *stat_out;         // File for dumping snapshot statistics

};

#endif /* COHERENCE_MANAGER_H_ */
