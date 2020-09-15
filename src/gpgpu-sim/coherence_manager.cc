/*
 * coherence_manager.cc
 *
 *  Created on: Apr 28, 2011
 *      Author: inder
 */

#include "coherence_manager.h"
#include <stdio.h>
#include <stdlib.h>


extern tm_options g_tm_options;

/*
 * Coherence tag array
 */

coherence_tag_array::coherence_tag_array( const cache_config &config, unsigned uid)
    : m_config(config)
{
    m_uid = uid;
   // assert( m_config.m_write_policy == READ_ONLY );
   //m_lines = new coherence_cache_block_t[ config.get_num_lines()];
    m_allocated = false;

   //printf("COH: Initialize coherence tag array for thread uid=%u\n", m_uid);

   m_stats_accesses = 0;
   m_stats_hits = 0;
   m_stats_misses = 0;
   m_stats_evictions = 0;
   m_stats_evictions_modified = 0;
   m_stats_evictions_shared = 0;
   m_stats_invalidations = 0;
}

coherence_tag_array::~coherence_tag_array()
{
   delete[] m_lines;
}

void coherence_tag_array::allocate_space() {
    assert(!m_allocated);
    m_lines = new coherence_cache_block_t[ m_config.get_num_lines()];
    m_allocated = true;
}

void coherence_tag_array::flush()
{
   // No need to flush if not even allocated
   if(!m_allocated) return;

   delete[] m_lines;
   m_lines = new coherence_cache_block_t[ m_config.get_num_lines()];
   /*
   for (unsigned i=0; i < m_config.get_num_lines(); i++)
      m_lines[i].invalidate();
      */

}

coherence_cache_request_status coherence_tag_array::probe( new_addr_type addr, unsigned &idx ) const
{
    assert(m_allocated);

    unsigned set_index = m_config.set_index(addr);
    new_addr_type tag = m_config.tag(addr);

    unsigned invalid_line = (unsigned)-1;
    unsigned valid_line = (unsigned)-1;
    unsigned valid_timestamp = (unsigned)-1;

    // check for hit
    for (unsigned way=0; way<m_config.m_assoc; way++) {
        unsigned index = set_index*m_config.m_assoc+way;
        coherence_cache_block_t *line = &m_lines[index];
        if (line->m_tag == tag) {
            if( line->m_status == COH_SHARED || line->m_status == COH_MODIFIED ) {
                idx = index;
                return COH_HIT;
            } else {
                assert( line->m_status == COH_INVALID );
            }
        }

        if(line->m_status == COH_INVALID) {
            invalid_line = index;
        } else {
            // valid line : keep track of most appropriate replacement candidate
            if( m_config.m_replacement_policy == LRU ) {
                if( line->m_last_access_time < valid_timestamp ) {
                    valid_timestamp = line->m_last_access_time;
                    valid_line = index;
                }
            } else if( m_config.m_replacement_policy == FIFO ) {
                if( line->m_alloc_time < valid_timestamp ) {
                    valid_timestamp = line->m_alloc_time;
                    valid_line = index;
                }
            }
        }
    }

    if( invalid_line != (unsigned)-1 ) {
        idx = invalid_line;
        return COH_MISS;
    } else if( valid_line != (unsigned)-1) {
        idx = valid_line;
        return COH_MISS_EVICTION;
    } else abort();

}


coherence_cache_request_status coherence_tag_array::access( new_addr_type addr, unsigned time, bool write, new_addr_type &evicted_block_addr) {
    assert(m_allocated);

    unsigned idx;
    coherence_cache_request_status status = probe(addr,idx);
    m_stats_accesses++;
    switch (status) {
        case COH_HIT:
            m_lines[idx].access(time);
            if(write) m_lines[idx].mark_modified(time);
            m_stats_hits++;
            break;
        case COH_MISS:
            m_lines[idx].allocate( m_config.tag(addr), m_config.block_addr(addr), (write?COH_MODIFIED:COH_SHARED), time );
            m_stats_misses++;
            break;
        case COH_MISS_EVICTION:
            evicted_block_addr = m_lines[idx].m_block_addr;
            if(m_lines[idx].m_status == COH_MODIFIED) m_stats_evictions_modified++; else m_stats_evictions_shared++;
            m_lines[idx].allocate( m_config.tag(addr), m_config.block_addr(addr), (write?COH_MODIFIED:COH_SHARED), time );
            m_stats_misses++;
            m_stats_evictions++;
            break;
    }
    return status;
}

void coherence_tag_array::invalidate(new_addr_type block_addr) {
    assert(m_allocated);

    unsigned idx;
    coherence_cache_request_status status = probe(block_addr,idx);
    assert(status == COH_HIT);
    m_lines[idx].invalidate();
    m_stats_invalidations++;
}


/*
 * Coherence Manager
 */

coherence_manager::coherence_manager( const shader_core_config *shader_config ) :
        m_stats_hist_sharers_invalidated_on_write(1,"coh_sharers_invalidated_on_write"),
        m_stats_hist_sharers_invalidated_on_write_tm(1,"coh_sharers_invalidated_on_write_tm"),
        m_stats_hist_sharers_invalidated_on_write_samecore(1,"coh_sharers_invalidated_on_write_samecore"),
        m_stats_hist_sharers_invalidated_on_write_diffcore(1,"coh_sharers_invalidated_on_write_diffcore"),
        m_stats_hist_sharers_invalidated_on_write_samecore_tm(1,"coh_sharers_invalidated_on_write_samecore_tm"),
        m_stats_hist_sharers_invalidated_on_write_diffcore_tm(1,"coh_sharers_invalidated_on_write_diffcore_tm")
{
	m_shader_config = shader_config;
	m_nthreads = shader_config->n_simt_clusters * shader_config->n_simt_cores_per_cluster * shader_config->n_thread_per_shader;

	// Allocate a cache for each possible hwtid
	resize_cache_array(m_nthreads);

	m_stats_invalidations_total = 0;
	m_stats_invalidations_samecore = 0;
	m_stats_invalidations_diffcore = 0;
	m_stats_invalidations_total_tm = 0;
	m_stats_invalidations_samecore_tm = 0;
	m_stats_invalidations_diffcore_tm = 0;

	m_stats_invalidations_on_writes_total = 0;


	m_stats_accesses = 0;
    m_stats_accesses_tm = 0;
    m_stats_reads = 0;
    m_stats_writes = 0;
    m_stats_reads_tm = 0;
    m_stats_writes_tm = 0;

	m_stats_misses = 0;
	m_stats_hits = 0;

	m_stats_tm_aborts = 0;
	m_stats_tm_commits = 0;

	m_stats_tm_access_mode_reads = 0;

    FILE *fp = stdout;
    printf("COH: Coherence L1 cache (nthreads=%u): \t", m_nthreads);
    shader_config->m_L1PC_config.print(fp);

    // Initialize statistics dumping file
    stat_out = fopen("gpgpu_coh_stats.txt", "w");
}

coherence_manager::~coherence_manager()
{
    // Clear memory for caches
    for(unsigned i=0; i<m_coherence_L0_caches.size(); i++){
        delete m_coherence_L0_caches[i];
    }
    m_coherence_L0_caches.clear();

    // Clear memory for directory
    std::map<new_addr_type, coherence_directory_block_t*>::iterator it;
    for(it=m_directory_full.begin(); it != m_directory_full.end(); it++)
    {
        delete it->second;
    }
    m_directory_full.clear();

    fclose(stat_out);
}

void coherence_manager::resize_cache_array(unsigned new_size)
{
    assert(m_coherence_L0_caches.size() < new_size);
    for(unsigned i=m_coherence_L0_caches.size(); i<new_size; i++) {
        m_coherence_L0_caches.push_back(new coherence_tag_array(m_shader_config->m_L1PC_config, i));
    }
}

void coherence_manager::flush_cache(unsigned hwtid)
{
    assert(hwtid < m_coherence_L0_caches.size());

    // Flush the cache
    m_coherence_L0_caches[hwtid]->flush();

    //printf("COH: flush cache hwtid=%u\n", hwtid);

    // Remove this sharer from all entries in directory
    std::map<new_addr_type, coherence_directory_block_t*>::iterator it;
    for(it=m_directory_full.begin(); it != m_directory_full.end();)
    {
        bool invalidate_block = false;
        if(it->second->m_sharers.count(hwtid)>0 && it->second->m_status != COH_INVALID)
            invalidate_block = remove_sharer(hwtid, it->second->m_block_addr);

        if(invalidate_block) {
            delete it->second;
            m_directory_full.erase(it++);
        } else {
            ++it;
        }
    }
}

void coherence_manager::mem_access(unsigned hwtid, new_addr_type addr, unsigned time, bool write, ptx_thread_info* thread_state)
{
    assert(hwtid < m_coherence_L0_caches.size());

    tm_manager* tm_manager_ins = (tm_manager*) thread_state->is_in_transaction();
    bool is_tm_inst = (tm_manager_ins != NULL);
    if(m_shader_config->coh_tm_only) assert(is_tm_inst);

    // Ignore access mode reads
    if(is_tm_inst && g_tm_options.m_enable_access_mode && !write && !tm_manager_ins->get_read_conflict_detection()) {
        m_stats_tm_access_mode_reads++;
        return;
    }

    m_stats_accesses++;
    if(is_tm_inst) m_stats_accesses_tm++;
    if(!write) {
        m_stats_reads++;
        if(is_tm_inst) m_stats_reads_tm++;
    } else {
        m_stats_writes++;
        if(is_tm_inst) m_stats_writes_tm++;
    }

    // Allocate cache if it hasn't been
    if(!m_coherence_L0_caches[hwtid]->is_allocated()) m_coherence_L0_caches[hwtid]->allocate_space();

    // Directory access
    // Allocate directory entry if it doesn't exist yet
    new_addr_type block_addr = m_shader_config->m_L1PC_config.block_addr(addr);
    //printf("COH: mem_access (is_tm=%d) wr=%d, hwtid=%u, addr=%llu, block_addr=%llu, time=%u \n", is_tm_inst, write, hwtid, addr, block_addr, time);
    if(m_directory_full.find(block_addr) == m_directory_full.end()){
        m_directory_full[block_addr] = new coherence_directory_block_t();
    }
    m_stats_directory_all_blocks.insert(block_addr);


    m_stats_hw_threads.insert(hwtid);
    m_stats_sw_threads.insert(thread_state->get_uid());
    // Track number of transactions per thread
    if(is_tm_inst) {
        m_stats_txns.insert(tm_manager_ins->uid());
        m_stats_txns_per_hw_thread[hwtid].insert(tm_manager_ins->uid());
        m_stats_txns_per_sw_thread[thread_state->get_uid()].insert(tm_manager_ins->uid());
    }


    // Process the access
    std::set<unsigned> invalidation_vector;
    invalidation_vector.clear();
    unsigned invalidated = add_sharer(hwtid, block_addr, time, write, invalidation_vector);
    m_stats_directory_all_blocks_invalidations[block_addr] += invalidated;
    record_invalidation_stats(hwtid, invalidation_vector, is_tm_inst, write);


    // Cache access
    new_addr_type evicted_block_address;
    coherence_cache_request_status status = m_coherence_L0_caches[hwtid]->access(addr, time, write, evicted_block_address);
    // If insertion resulted in an eviction, update directory
    if(status == COH_MISS_EVICTION) {
        if(remove_sharer(hwtid, evicted_block_address)) {
            delete m_directory_full[evicted_block_address];
            m_directory_full.erase(evicted_block_address);
        }

        // Track # of capacity evictions per thread
        m_stats_capacity_evictions_per_hw_thread[hwtid]++;
        m_stats_capacity_evictions_per_sw_thread[thread_state->get_uid()]++;

        // Track # of capacity evictions per unique txn
        if(is_tm_inst)
            m_stats_capacity_evictions_per_txn[tm_manager_ins->uid()]++;
    }
}

// Add a sharer and return number of invalidations
unsigned coherence_manager::add_sharer(unsigned hwtid, new_addr_type block_addr, unsigned time, bool write, std::set<unsigned> &invalidation_vector) {
    assert(m_directory_full.find(block_addr) != m_directory_full.end());

    coherence_directory_block_t& directory_block = *(m_directory_full[block_addr]);
    assert(directory_block.count_sharers() == directory_block.m_nvalidsharers);

    std::set<unsigned>::iterator it;

    unsigned invalidated = 0;
    if(directory_block.m_status == COH_INVALID) {
        // Was invalid - no invalidations
        assert(directory_block.m_nvalidsharers == 0);
        directory_block.m_sharers.insert(hwtid);
        directory_block.m_nvalidsharers = 1;
        directory_block.m_block_addr = block_addr;
        directory_block.m_alloc_time = time;
        directory_block.m_last_access_time = time;
        directory_block.m_status = write?COH_MODIFIED:COH_SHARED;

        m_stats_misses++;

        invalidated = 0;
    } else if (directory_block.m_status == COH_SHARED) {
        if(directory_block.m_sharers.count(hwtid)>0) m_stats_hits++;
        else m_stats_misses++;

        // Was shared
        directory_block.m_last_access_time = time;
        if(!write){
            // Read - No invalidations if reading
            if(directory_block.m_sharers.count(hwtid)==0) directory_block.m_nvalidsharers++;
            directory_block.m_sharers.insert(hwtid);
            invalidated = 0;
        } else {
            // Write - No invalidations if #sharers=1 and sharer=hwtid
            //       - Otherwise invalidate all sharers
            directory_block. m_status = COH_MODIFIED;
            if(directory_block.m_nvalidsharers == 1 and directory_block.m_sharers.count(hwtid)>0) {
                invalidated = 0;
            } else {
                invalidated = directory_block.m_nvalidsharers;
                if(directory_block.m_sharers.count(hwtid)) invalidated--;

                // Send the invalidations
                for(it=directory_block.m_sharers.begin(); it!=directory_block.m_sharers.end(); it++) {
                    if(*it != hwtid) {
                        m_coherence_L0_caches[*it]->invalidate(block_addr);
                        invalidation_vector.insert(*it);
                    }
                }

                directory_block.reset_sharers();
                directory_block.m_nvalidsharers = 1;
                directory_block.m_sharers.insert(hwtid);
            }
        }
    } else {
        if(directory_block.m_sharers.count(hwtid)>0) m_stats_hits++;
        else m_stats_misses++;

        // Was modified
        assert(directory_block.m_nvalidsharers == 1);
        directory_block.m_last_access_time = time;
        // No invalidations if sharer=hwtid
        // Otherwise one invalidation
        if(directory_block.m_sharers.count(hwtid)>0) {
            invalidated = 0;
        } else {
            // Send the invalidations
            for(it=directory_block.m_sharers.begin(); it!=directory_block.m_sharers.end(); it++) {
                if(*it != hwtid) {
                    m_coherence_L0_caches[*it]->invalidate(block_addr);
                    invalidation_vector.insert(*it);
                }
            }

            directory_block.reset_sharers();
            directory_block.m_nvalidsharers = 1;
            directory_block.m_sharers.insert(hwtid);
            directory_block.m_status = write?COH_MODIFIED:COH_SHARED;
            invalidated = 1;
        }
    }

    // Record max number of sharers for this block
    unsigned nsharers_max = m_stats_directory_all_blocks_nsharersmax[block_addr];
    nsharers_max = (nsharers_max > directory_block.m_nvalidsharers) ? nsharers_max : directory_block.m_nvalidsharers;
    m_stats_directory_all_blocks_nsharersmax[block_addr] = nsharers_max;

    return invalidated;
}

// Remove a sharer - invalidate if it was the only sharer
// Does not send invalidations (used when cache already evicted a block)
bool coherence_manager::remove_sharer(unsigned hwtid, new_addr_type block_addr) {
    assert(m_directory_full.find(block_addr) != m_directory_full.end());

    bool invalidate_block = false;

    coherence_directory_block_t& directory_block = *(m_directory_full[block_addr]);

    assert(directory_block.m_status != COH_INVALID);
    assert(directory_block.m_sharers.count(hwtid)>0);
    directory_block.m_sharers.erase(hwtid);
    directory_block.m_nvalidsharers--;
    if(directory_block.m_nvalidsharers == 0) {
        directory_block.m_status = COH_INVALID;
        invalidate_block = true;
    }
    return invalidate_block;
}


void coherence_manager::record_invalidation_stats(unsigned hwtid_owner, std::set<unsigned> invalidation_vector, bool is_tm_inst, bool write) {
    unsigned n_invalidations = 0;
    unsigned n_invalidations_samecore = 0;
    unsigned n_invalidations_diffcore = 0;
    std::set<unsigned>::iterator it;
    for(it=invalidation_vector.begin(); it!=invalidation_vector.end(); it++) {
        n_invalidations++;
        m_stats_invalidations_total++;
        if(is_tm_inst) m_stats_invalidations_total_tm++;

        unsigned core_owner = hwtid_owner / m_shader_config->n_thread_per_shader;
        unsigned core_invalidatee = *it / m_shader_config->n_thread_per_shader;
        if(core_owner == core_invalidatee) {
            m_stats_invalidations_samecore++;
            if(is_tm_inst) m_stats_invalidations_samecore_tm++;
            n_invalidations_samecore++;
        } else {
            m_stats_invalidations_diffcore++;
            if(is_tm_inst) m_stats_invalidations_diffcore_tm++;
            n_invalidations_diffcore++;
        }
        //printf("COH: hwtid=%u invalidated hwtid=%u \n", hwtid_owner, *it);
    }

    if(write) {
        m_stats_invalidations_on_writes_total += n_invalidations;
        m_stats_hist_sharers_invalidated_on_write.add2bin(n_invalidations);
        m_stats_hist_sharers_invalidated_on_write_samecore.add2bin(n_invalidations_samecore);
        m_stats_hist_sharers_invalidated_on_write_diffcore.add2bin(n_invalidations_diffcore);
        if(is_tm_inst) {
            m_stats_hist_sharers_invalidated_on_write_tm.add2bin(n_invalidations);
            m_stats_hist_sharers_invalidated_on_write_samecore_tm.add2bin(n_invalidations_samecore);
            m_stats_hist_sharers_invalidated_on_write_diffcore_tm.add2bin(n_invalidations_diffcore);
        }
    }
}


void coherence_manager::tm_commited(unsigned hwtid, unsigned hwwarpid, ptx_thread_info* thread_state) {
    assert( thread_state->is_in_transaction() != NULL );

    m_stats_tm_commits++;


    // Keep track of warp based stats
    m_build_txns_warp_group[hwwarpid].insert(thread_state->is_in_transaction()->uid());

    if(m_shader_config->coh_tm_flush_cache) {
        flush_cache(hwtid);
    }
}

void coherence_manager::tm_warp_commited(unsigned hwwarpid) {
    // Move warp group set from building map to storage list
    std::set<unsigned> txns_warp_group = m_build_txns_warp_group[hwwarpid];
    m_stats_txns_warp_group.push_back(txns_warp_group);

    // Count # of evictions in this group
    unsigned group_evictions = 0;
    std::set<unsigned>::iterator it;
    for(it=txns_warp_group.begin(); it!=txns_warp_group.end(); it++) {
        group_evictions += m_stats_capacity_evictions_per_txn[*it];
    }
    m_stats_txns_warp_group_evictions.push_back(group_evictions);

    m_build_txns_warp_group.erase(hwwarpid);
}

void coherence_manager::tm_aborted(unsigned hwtid, ptx_thread_info* thread_state) {
    assert( thread_state->is_in_transaction() != NULL );

    m_stats_tm_aborts++;

    if(m_shader_config->coh_tm_flush_cache) {
        flush_cache(hwtid);
    }
}

void coherence_manager::print_stats(FILE *fp) {
    fprintf(fp, "coh_stats_invalidations_total = %u \n", m_stats_invalidations_total);
    fprintf(fp, "coh_stats_invalidations_samecore = %u \n", m_stats_invalidations_samecore);
    fprintf(fp, "coh_stats_invalidations_diffcore = %u \n", m_stats_invalidations_diffcore);
    fprintf(fp, "coh_stats_invalidations_total_tm = %u \n", m_stats_invalidations_total_tm);
    fprintf(fp, "coh_stats_invalidations_samecore_tm = %u \n", m_stats_invalidations_samecore_tm);
    fprintf(fp, "coh_stats_invalidations_diffcore_tm = %u \n", m_stats_invalidations_diffcore_tm);

    fprintf(fp, "coh_stats_invalidations_on_writes_total = %u \n", m_stats_invalidations_on_writes_total);
    fprintf(fp, "coh_stats_invalidations_on_writes_average = %f \n", (float) m_stats_invalidations_on_writes_total / (float) m_stats_writes);
    m_stats_hist_sharers_invalidated_on_write.fprint(fp); fprintf(fp, "\n");
    m_stats_hist_sharers_invalidated_on_write_tm.fprint(fp); fprintf(fp, "\n");
    m_stats_hist_sharers_invalidated_on_write_samecore.fprint(fp); fprintf(fp, "\n");
    m_stats_hist_sharers_invalidated_on_write_samecore_tm.fprint(fp); fprintf(fp, "\n");
    m_stats_hist_sharers_invalidated_on_write_diffcore.fprint(fp); fprintf(fp, "\n");
    m_stats_hist_sharers_invalidated_on_write_diffcore_tm.fprint(fp); fprintf(fp, "\n");

    fprintf(fp, "coh_stats_accesses = %u \n", m_stats_accesses);
    fprintf(fp, "coh_stats_accesses_tm = %u \n", m_stats_accesses_tm);
    fprintf(fp, "coh_stats_reads = %u \n", m_stats_reads);
    fprintf(fp, "coh_stats_reads_tm = %u \n", m_stats_reads_tm);
    fprintf(fp, "coh_stats_writes = %u \n", m_stats_writes);
    fprintf(fp, "coh_stats_writes_tm = %u \n", m_stats_writes_tm);

    fprintf(fp, "coh_stats_hits = %u \n", m_stats_hits);
    fprintf(fp, "coh_stats_misses = %u \n", m_stats_misses);

    // Directory size related stats
    fprintf(fp, "coh_full_directory_n_blocks = %lu \n", m_stats_directory_all_blocks.size());
    fprintf(fp, "coh_full_directory_size = %luB \n", m_stats_directory_all_blocks.size()*m_nthreads);


    // Histogram of number of invalidations per block
    linear_histogram hist_invalidation_per_block(1, "coh_invalidations_per_block");
    std::map<new_addr_type, unsigned>::iterator it;
    for(it=m_stats_directory_all_blocks_invalidations.begin(); it != m_stats_directory_all_blocks_invalidations.end(); it++)
    {
        hist_invalidation_per_block.add2bin(it->second);
    }
    hist_invalidation_per_block.fprint(fp); fprintf(fp, "\n");

    // Histogram of max number of sharers seen by block
    linear_histogram hist_max_sharers_per_block(1, "coh_max_sharers_per_block");
    for(it=m_stats_directory_all_blocks_nsharersmax.begin(); it != m_stats_directory_all_blocks_nsharersmax.end(); it++)
    {
        hist_max_sharers_per_block.add2bin(it->second);
    }
    hist_max_sharers_per_block.fprint(fp); fprintf(fp, "\n");


    fprintf(fp, "coh_stats_sw_threads = %u\n", (unsigned) m_stats_sw_threads.size());
    fprintf(fp, "coh_stats_hw_threads = %u\n", (unsigned) m_stats_hw_threads.size());
    fprintf(fp, "coh_stats_txns = %u\n", (unsigned) m_stats_txns.size());


    // Histogram of # of txns executed per hw thread
    linear_histogram hist_txns_per_hw_thread(1, "coh_txns_per_hw_thread");
    std::map<unsigned, std::set<unsigned> >::iterator it2;
    for(it2=m_stats_txns_per_hw_thread.begin(); it2!=m_stats_txns_per_hw_thread.end(); it2++) {
        hist_txns_per_hw_thread.add2bin(it2->second.size());
    }
    hist_txns_per_hw_thread.fprint(fp); fprintf(fp, "\n");

    // Histogram of # of evictions per hw thread
    linear_histogram hist_capacity_evictions_per_hw_thread(1, "coh_capacity_evictions_per_hw_thread");
    std::map<unsigned, unsigned>::iterator it3;
    for(it3=m_stats_capacity_evictions_per_hw_thread.begin(); it3!=m_stats_capacity_evictions_per_hw_thread.end(); it3++) {
        hist_capacity_evictions_per_hw_thread.add2bin(it3->second);
    }
    hist_capacity_evictions_per_hw_thread.fprint(fp); fprintf(fp, "\n");
    fprintf(fp, "coh_percent_hw_threads_no_capacity_evictions = %f \n", 100.0*(1.0 - ((float)m_stats_capacity_evictions_per_hw_thread.size() / (float)m_stats_hw_threads.size())) );


    // Histogram of # of txns executed per sw thread
    linear_histogram hist_txns_per_sw_thread(1, "coh_txns_per_sw_thread");
    for(it2=m_stats_txns_per_sw_thread.begin(); it2!=m_stats_txns_per_sw_thread.end(); it2++) {
        hist_txns_per_sw_thread.add2bin(it2->second.size());
    }
    hist_txns_per_sw_thread.fprint(fp); fprintf(fp, "\n");

    // Histogram of # of evictions per sw thread
    linear_histogram hist_capacity_evictions_per_sw_thread(1, "coh_capacity_evictions_per_sw_thread");
    for(it3=m_stats_capacity_evictions_per_sw_thread.begin(); it3!=m_stats_capacity_evictions_per_sw_thread.end(); it3++) {
        hist_capacity_evictions_per_sw_thread.add2bin(it3->second);
    }
    hist_capacity_evictions_per_sw_thread.fprint(fp); fprintf(fp, "\n");
    fprintf(fp, "coh_percent_sw_threads_no_capacity_evictions = %f \n", 100.0*(1.0 - ((float)m_stats_capacity_evictions_per_sw_thread.size() / (float)m_stats_sw_threads.size())) );


    // Histogram of # of evictions per unique txn
    unsigned coh_txns_evicted = 0;
    linear_histogram hist_capacity_evictions_per_txn(1, "coh_capacity_evictions_per_txn");
    for(it3=m_stats_capacity_evictions_per_txn.begin(); it3!=m_stats_capacity_evictions_per_txn.end(); it3++) {
        hist_capacity_evictions_per_txn.add2bin(it3->second);
        if(it3->second > 0) coh_txns_evicted++;
    }
    hist_capacity_evictions_per_txn.fprint(fp); fprintf(fp, "\n");
    fprintf(fp, "coh_percent_txns_no_capacity_evictions = %f \n", 100.0*(1.0 - ((float)coh_txns_evicted / (float)m_stats_txns.size())) );


    // Histogram of # of evictions per txns warp group
    unsigned coh_evictions_per_txn_warp_group_total = 0;
    unsigned coh_txns_warp_group_evicted = 0;
    linear_histogram hist_evictions_per_txn_warp_group(1, "coh_evictions_per_txn_warp_group");
    std::list<unsigned>::iterator it4;
    for(it4=m_stats_txns_warp_group_evictions.begin(); it4!=m_stats_txns_warp_group_evictions.end(); it4++) {
        hist_evictions_per_txn_warp_group.add2bin(*it4);
        coh_evictions_per_txn_warp_group_total += *it4;
        if(*it4 > 0 ) coh_txns_warp_group_evicted++;
    }
    hist_evictions_per_txn_warp_group.fprint(fp); fprintf(fp, "\n");
    fprintf(fp, "coh_evictions_per_txn_warp_group_total = %u\n", coh_evictions_per_txn_warp_group_total);
    fprintf(fp, "coh_percent_txns_warp_group_no_capacity_evictions = %f\n", 100*(1.0 - ((float)coh_txns_warp_group_evicted/(float)m_stats_txns_warp_group_evictions.size())));

    assert(m_build_txns_warp_group.size() == 0);


    //
    // TM related stats
    //
    fprintf(fp, "coh_tm_commits = %u\n", m_stats_tm_commits);
    fprintf(fp, "coh_tm_aborts = %u\n", m_stats_tm_aborts);
    fprintf(fp, "coh_tm_access_mode_reads = %u\n", m_stats_tm_access_mode_reads);

    //
    // Stats from caches
    //

    // Accesses
    fprintf(fp, "coh_cache_stats_accesses: = [");
    unsigned total_cache_accesses = 0;
    for(unsigned i=0; i<m_nthreads; i++) {
        total_cache_accesses += m_coherence_L0_caches[i]->get_stats_accesses();
        //fprintf(fp, "%u ", m_coherence_L0_caches[i]->get_stats_accesses());
    }
    fprintf(fp, "]; (sum = %u) \n", total_cache_accesses);

    // Hits
    fprintf(fp, "coh_cache_stats_hits: = [");
    unsigned total_cache_hits = 0;
    for(unsigned i=0; i<m_nthreads; i++) {
        total_cache_hits += m_coherence_L0_caches[i]->get_stats_hits();
        //fprintf(fp, "%u ", m_coherence_L0_caches[i]->get_stats_hits());
    }
    fprintf(fp, "]; (sum = %u) \n", total_cache_hits);

    // Misses
    fprintf(fp, "coh_cache_stats_misses: = [");
    unsigned total_cache_misses = 0;
    for(unsigned i=0; i<m_nthreads; i++) {
        total_cache_misses += m_coherence_L0_caches[i]->get_stats_misses();
        //fprintf(fp, "%u ", m_coherence_L0_caches[i]->get_stats_misses());
    }
    fprintf(fp, "]; (sum = %u) \n", total_cache_misses);

    // Evictions
    fprintf(fp, "coh_cache_stats_evictions: = [");
    unsigned total_cache_evictions = 0;
    for(unsigned i=0; i<m_nthreads; i++) {
        total_cache_evictions += m_coherence_L0_caches[i]->get_stats_evictions();
        //fprintf(fp, "%u ", m_coherence_L0_caches[i]->get_stats_evictions());
    }
    fprintf(fp, "]; (sum = %u) \n", total_cache_evictions);

    // Evictions of modified
    fprintf(fp, "coh_cache_stats_evictions_modified: = [");
    unsigned total_cache_evictions_modified = 0;
    for(unsigned i=0; i<m_nthreads; i++) {
        total_cache_evictions_modified += m_coherence_L0_caches[i]->get_stats_evictions_modified();
        //fprintf(fp, "%u ", m_coherence_L0_caches[i]->get_stats_evictions_modified());
    }
    fprintf(fp, "]; (sum = %u) \n", total_cache_evictions_modified);

    // Evictions of shared
    fprintf(fp, "coh_cache_stats_evictions_shared: = [");
    unsigned total_cache_evictions_shared = 0;
    for(unsigned i=0; i<m_nthreads; i++) {
        total_cache_evictions_shared += m_coherence_L0_caches[i]->get_stats_evictions_shared();
        //fprintf(fp, "%u ", m_coherence_L0_caches[i]->get_stats_evictions_shared());
    }
    fprintf(fp, "]; (sum = %u) \n", total_cache_evictions_shared);

    // Invalidations
    fprintf(fp, "coh_cache_stats_invalidations: = [");
    unsigned total_cache_invalidations = 0;
    for(unsigned i=0; i<m_nthreads; i++) {
        total_cache_invalidations += m_coherence_L0_caches[i]->get_stats_invalidations();
        //fprintf(fp, "%u ", m_coherence_L0_caches[i]->get_stats_invalidations());
    }
    fprintf(fp, "]; (sum = %u) \n", total_cache_invalidations);

}

void coherence_manager::dump_sharer_histogram(unsigned time) {
    fprintf(stat_out, "cycle %u  ", time);
    get_directory_sharer_histogram().fprint(stat_out);
    fprintf(stat_out, "  n_blocks %u", (unsigned) m_directory_full.size());
    fprintf(stat_out, " \n");
    fflush(stat_out);

}

linear_histogram coherence_manager::get_directory_sharer_histogram() {
    linear_histogram directory_sharer_histogram(1,"directory_sharer_histogram");
    std::map<new_addr_type, coherence_directory_block_t*>::iterator it;
    for(it=m_directory_full.begin(); it != m_directory_full.end(); it++)
    {
        if(it->second->m_nvalidsharers > 0)
            directory_sharer_histogram.add2bin(it->second->m_nvalidsharers);
    }
    return directory_sharer_histogram;
}
