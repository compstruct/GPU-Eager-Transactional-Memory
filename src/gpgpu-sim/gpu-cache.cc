// Copyright (c) 2009-2011, Tor M. Aamodt, Tayler Hetherington
// The University of British Columbia
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// Redistributions in binary form must reproduce the above copyright notice, this
// list of conditions and the following disclaimer in the documentation and/or
// other materials provided with the distribution.
// Neither the name of The University of British Columbia nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "gpu-cache.h"
#include "stat-tool.h"
#include "gpu-sim.h"
#include "../cuda-sim/tm_manager.h"

#include <assert.h>

#define MAX_DEFAULT_CACHE_SIZE_MULTIBLIER 4
// used to allocate memory that is large enough to adapt the changes in cache size across kernels

const char * cache_request_status_str(enum cache_request_status status) 
{
   static const char * static_cache_request_status_str[] = {
      "HIT",
      "HIT_RESERVED",
      "HIT_PARTIAL",
      "MISS",
      "RESERVATION_FAIL"
   }; 

   assert(sizeof(static_cache_request_status_str) / sizeof(const char*) == NUM_CACHE_REQUEST_STATUS); 
   assert(status < NUM_CACHE_REQUEST_STATUS); 

   return static_cache_request_status_str[status]; 
}

void l2_cache_config::init(linear_to_raw_address_translation *address_mapping){
	cache_config::init(m_config_string,FuncCachePreferNone);
	m_address_mapping = address_mapping;
}

unsigned l2_cache_config::set_index(new_addr_type addr) const{
   if(!m_address_mapping){
      if (m_use_set_index_hash) {
         return set_index_hashed(addr); 
      } else {
         return(addr >> m_line_sz_log2) & (m_nset-1);
      }
   }else{
      // Calculate set index without memory partition bits to reduce set camping
      new_addr_type part_addr = m_address_mapping->partition_address(addr);
      if (m_use_set_index_hash) {
         return set_index_hashed(part_addr); 
      } else {
         return(part_addr >> m_line_sz_log2) & (m_nset -1);
      }
   }
}

tag_array::~tag_array() 
{
    delete[] m_lines;
}

tag_array::tag_array( cache_config &config,
                      int core_id,
                      int type_id,
                      cache_block_t* new_lines)
    : m_config( config ),
      m_lines( new_lines )
{
    init( core_id, type_id );
}

void tag_array::update_cache_parameters(cache_config &config)
{
	m_config=config;
}

tag_array::tag_array( cache_config &config,
                      int core_id,
                      int type_id )
    : m_config( config )
{
    //assert( m_config.m_write_policy == READ_ONLY ); Old assert
    m_lines = new cache_block_t[MAX_DEFAULT_CACHE_SIZE_MULTIBLIER*config.get_num_lines()];
    init( core_id, type_id );
}

void tag_array::init( int core_id, int type_id )
{
    m_access = 0;
    m_miss = 0;
    m_pending_hit = 0;
    m_res_fail = 0;
    // initialize snapshot counters for visualizer
    m_prev_snapshot_access = 0;
    m_prev_snapshot_miss = 0;
    m_prev_snapshot_pending_hit = 0;
    m_core_id = core_id; 
    m_type_id = type_id;
}

enum cache_request_status tag_array::probe( new_addr_type addr, unsigned &idx ) const
{
   //assert( m_config.m_write_policy == READ_ONLY );
   unsigned set_index = m_config.set_index(addr);
   new_addr_type tag = m_config.tag(addr);

   unsigned invalid_line = (unsigned)-1;
   unsigned valid_line = (unsigned)-1;
   unsigned valid_timestamp = (unsigned)-1;

   bool all_reserved = true;
   
   // check for hit or pending hit
   for (unsigned way=0; way<m_config.m_assoc; way++) {
      unsigned index = set_index*m_config.m_assoc+way;
      cache_block_t *line = &m_lines[index];
      if (line->m_tag == tag) {
         if( line->m_status == RESERVED ) {
            idx = index;
            return HIT_RESERVED;
         } else if( line->m_status == VALID ) {
            idx = index;
            return HIT;
         } else if ( line->m_status == MODIFIED ) {
            idx = index;
            return HIT;
         } else {
            assert( line->m_status == INVALID );
         }
      } 
      if(line->m_status != RESERVED) {
         all_reserved = false;
         if(line->m_status == INVALID) {
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
   }
   if( all_reserved ) {
       assert( m_config.m_alloc_policy == ON_MISS ); 
       return RESERVATION_FAIL; // miss and not enough space in cache to allocate on miss
   }

   if( invalid_line != (unsigned)-1 ) {
       idx = invalid_line;
   } else if( valid_line != (unsigned)-1) {
       idx = valid_line;
   } else abort(); // if an unreserved block exists, it is either invalid or replaceable 

   return MISS;
}

enum cache_request_status tag_array::access( new_addr_type addr, unsigned time, unsigned &idx )
{
    bool wb=false;
    cache_block_t evicted;
    enum cache_request_status result = access(addr,time,idx,wb,evicted);
    assert(!wb);
    return result;
}

enum cache_request_status tag_array::access( new_addr_type addr, unsigned time, unsigned &idx, bool &wb, cache_block_t &evicted ) 
{
    m_access++;
    shader_cache_access_log(m_core_id, m_type_id, 0); // log accesses to cache
    enum cache_request_status status = probe(addr,idx);
    switch (status) {
    case HIT_RESERVED: 
        m_pending_hit++;
    case HIT: 
        m_lines[idx].m_last_access_time=time; 
        break;
    case MISS:
        m_miss++;
        shader_cache_access_log(m_core_id, m_type_id, 1); // log cache misses
        if ( m_config.m_alloc_policy == ON_MISS ) {
            if( m_lines[idx].m_status == MODIFIED ) {
                wb = true;
                evicted = m_lines[idx];
            }
            m_lines[idx].allocate( m_config.tag(addr), m_config.block_addr(addr), time );
        }
        break;
    case RESERVATION_FAIL:
        m_res_fail++;
        shader_cache_access_log(m_core_id, m_type_id, 1); // log cache misses
        break;
    case HIT_PARTIAL:
        abort(); 
    default:
        fprintf( stderr, "tag_array::access - Error: Unknown"
            "cache_request_status %d\n", status );
        abort();
    }
    return status;
}

void tag_array::fill( new_addr_type addr, unsigned time, bool wr )
{
    assert( m_config.m_alloc_policy == ON_FILL );
    unsigned idx;
    enum cache_request_status status = probe(addr,idx);
    assert(status==MISS); // MSHR should have prevented redundant memory request
    m_lines[idx].allocate( m_config.tag(addr), m_config.block_addr(addr), time );
    m_lines[idx].fill(time);
}

void tag_array::fill( unsigned index, unsigned time, bool wr ) 
{
    assert( m_config.m_alloc_policy == ON_MISS );
    m_lines[index].fill(time);
}

void tag_array::flush() 
{
   for (unsigned i=0; i < m_config.get_num_lines(); i++) 
      m_lines[i].m_status = INVALID;
}

float tag_array::windowed_miss_rate( ) const
{
    unsigned n_access    = m_access - m_prev_snapshot_access;
    unsigned n_miss      = m_miss - m_prev_snapshot_miss;
    // unsigned n_pending_hit = m_pending_hit - m_prev_snapshot_pending_hit;

    float missrate = 0.0f;
    if (n_access != 0)
        missrate = (float) n_miss / n_access;
    return missrate;
}

void tag_array::new_window()
{
   m_prev_snapshot_access = m_access;
   m_prev_snapshot_miss = m_miss;
   m_prev_snapshot_pending_hit = m_pending_hit;
}

void tag_array::print( FILE *stream, unsigned &total_access, unsigned &total_misses ) const
{
    m_config.print(stream);
    fprintf( stream, "\t\tAccess = %d, Miss = %d (%.3g), PendingHit = %d (%.3g)\n", 
             m_access, m_miss, (float) m_miss / m_access, 
             m_pending_hit, (float) m_pending_hit / m_access);
    total_misses+=m_miss;
    total_access+=m_access;
}

void tag_array::get_stats(unsigned &total_access, unsigned &total_misses, unsigned &total_hit_res, unsigned &total_res_fail) const{
    // Update statistics from the tag array
    total_access    = m_access;
    total_misses    = m_miss;
    total_hit_res   = m_pending_hit;
    total_res_fail  = m_res_fail;
}

void tag_array::display( FILE *fout ) const 
{
    for (unsigned set = 0; set < m_config.m_nset; set++) {
        for (unsigned way = 0; way < m_config.m_assoc; way++) {
            unsigned idx = set * m_config.m_assoc + way; 
            fprintf(fout, "[%#04x,%d] ", set, way); 
            m_lines[idx].print(fout); 
        }
    }
}

bool was_write_sent( const std::list<cache_event> &events )
{
    for( std::list<cache_event>::const_iterator e=events.begin(); e!=events.end(); e++ ) {
        if( *e == WRITE_REQUEST_SENT ) 
            return true;
    }
    return false;
}

bool was_writeback_sent( const std::list<cache_event> &events )
{
    for( std::list<cache_event>::const_iterator e=events.begin(); e!=events.end(); e++ ) {
        if( *e == WRITE_BACK_REQUEST_SENT ) 
            return true;
    }
    return false;
}

bool was_read_sent( const std::list<cache_event> &events )
{
    for( std::list<cache_event>::const_iterator e=events.begin(); e!=events.end(); e++ ) {
        if( *e == READ_REQUEST_SENT ) 
            return true;
    }
    return false;
}
/****************************************************************** MSHR ******************************************************************/

/// Checks if there is a pending request to the lower memory level already
bool mshr_table::probe( new_addr_type block_addr ) const{
    table::const_iterator a = m_data.find(block_addr);
    m_n_probe += 1; 
    if ( a != m_data.end() ) {
        m_n_hit += 1; 
        if ( a->second.m_response_ready ) {
            m_n_hit_ready += 1; 
            return false;  // prevent merging to a MSHR entry with in response queue
        }
    }
    return a != m_data.end();
}

/// Checks if there is space for tracking a new memory access
bool mshr_table::full( new_addr_type block_addr ) const{
    table::const_iterator i=m_data.find(block_addr);
    if ( i != m_data.end() ) {
        if ( i->second.m_response_ready ) {
            return true;  // prevent merging to a MSHR entry with in response queue
        } else {
            return i->second.m_list.size() >= m_max_merged;
        }
    } else {
        return m_data.size() >= m_num_entries; 
    }
}

/// Add or merge this access
void mshr_table::add( new_addr_type block_addr, mem_fetch *mf ){
    m_data[block_addr].m_list.push_back(mf);
    assert( m_data.size() <= m_num_entries );
    assert( m_data[block_addr].m_list.size() <= m_max_merged );
    assert( m_data[block_addr].m_response_ready == false );
    // indicate that this MSHR entry contains an atomic operation
    if ( mf->isatomic() ) {
        m_data[block_addr].m_has_atomic = true;
    }
}

/// Accept a new cache fill response: mark entry ready for processing
void mshr_table::mark_ready( new_addr_type block_addr, bool &has_atomic ){
    assert( !busy() );
    table::iterator a = m_data.find(block_addr);
    assert( a != m_data.end() ); // don't remove same request twice
    m_current_response.push_back( block_addr );
    has_atomic = a->second.m_has_atomic;
    a->second.m_response_ready = true; 
    assert( m_current_response.size() <= m_data.size() );
}

/// Returns next ready access
mem_fetch *mshr_table::next_access(){
    assert( access_ready() );
    new_addr_type block_addr = m_current_response.front();
    assert( !m_data[block_addr].m_list.empty() );
    mem_fetch *result = m_data[block_addr].m_list.front();
    m_data[block_addr].m_list.pop_front();
    if ( m_data[block_addr].m_list.empty() ) {
        // release entry
        m_data.erase(block_addr);
        m_current_response.pop_front();
    }
    return result;
}

void mshr_table::display( FILE *fp ) const{
    fprintf(fp,"MSHR contents\n");
    for ( table::const_iterator e=m_data.begin(); e!=m_data.end(); ++e ) {
        unsigned block_addr = e->first;
        fprintf(fp,"MSHR: tag=0x%06x, atomic=%d %zu entries : ", block_addr, e->second.m_has_atomic, e->second.m_list.size());
        if ( !e->second.m_list.empty() ) {
            mem_fetch *mf = e->second.m_list.front();
            fprintf(fp,"%p :",mf);
            mf->print(fp);
        } else {
            fprintf(fp," no memory requests???\n");
        }
    }
}

void mshr_table::print_stat( FILE *fp ) const 
{
    fprintf(fp, "\t\tmshr_probe = %d; mshr_hit = %d; mshr_hit_ready = %d;\n", m_n_probe, m_n_hit, m_n_hit_ready); 
}

/***************************************************************** Caches *****************************************************************/
cache_stats::cache_stats(const cache_config * config){
    m_stats.resize(NUM_MEM_ACCESS_TYPE);
    for(unsigned i=0; i<NUM_MEM_ACCESS_TYPE; ++i){
        m_stats[i].resize(NUM_CACHE_REQUEST_STATUS, 0);
    }
    m_cache_port_available_cycles = 0; 
    m_cache_data_port_busy_cycles = 0; 
    m_cache_fill_port_busy_cycles = 0; 
    m_local_write_misses = 0;
    m_local_write_misses_full_cacheline = 0;

    if (config != NULL) {
        m_set_utility.resize(config->get_num_sets(), 0); 
    }
}

void cache_stats::clear(){
    ///
    /// Zero out all current cache statistics
    ///
    for(unsigned i=0; i<NUM_MEM_ACCESS_TYPE; ++i){
        std::fill(m_stats[i].begin(), m_stats[i].end(), 0);
    }
    m_cache_port_available_cycles = 0; 
    m_cache_data_port_busy_cycles = 0; 
    m_cache_fill_port_busy_cycles = 0; 
    m_local_write_misses = 0;
    m_local_write_misses_full_cacheline = 0;

    m_set_utility.assign(m_set_utility.size(), 0); 
}

void cache_stats::inc_stats(int access_type, int access_outcome){
    ///
    /// Increment the stat corresponding to (access_type, access_outcome) by 1.
    ///
    if(!check_valid(access_type, access_outcome))
        assert(0 && "Unknown cache access type or access outcome");

    m_stats[access_type][access_outcome]++;
}

enum cache_request_status cache_stats::select_stats_status(enum cache_request_status probe, enum cache_request_status access) const {
	///
	/// This function selects how the cache access outcome should be counted. HIT_RESERVED is considered as a MISS
	/// in the cores, however, it should be counted as a HIT_RESERVED in the caches.
	///
	if(probe == HIT_RESERVED && access != RESERVATION_FAIL)
		return probe;
	else
		return access;
}

unsigned &cache_stats::operator()(int access_type, int access_outcome){
    ///
    /// Simple method to read/modify the stat corresponding to (access_type, access_outcome)
    /// Used overloaded () to avoid the need for separate read/write member functions
    ///
    if(!check_valid(access_type, access_outcome))
        assert(0 && "Unknown cache access type or access outcome");

    return m_stats[access_type][access_outcome];
}

unsigned cache_stats::operator()(int access_type, int access_outcome) const{
    ///
    /// Const accessor into m_stats.
    ///
    if(!check_valid(access_type, access_outcome))
        assert(0 && "Unknown cache access type or access outcome");

    return m_stats[access_type][access_outcome];
}

cache_stats cache_stats::operator+(const cache_stats &cs){
    ///
    /// Overloaded + operator to allow for simple stat accumulation
    ///
    cache_stats ret;
    for(unsigned type=0; type<NUM_MEM_ACCESS_TYPE; ++type){
        for(unsigned status=0; status<NUM_CACHE_REQUEST_STATUS; ++status){
            ret(type, status) = m_stats[type][status] + cs(type, status);
        }
    }
    ret.m_cache_port_available_cycles = m_cache_port_available_cycles + cs.m_cache_port_available_cycles; 
    ret.m_cache_data_port_busy_cycles = m_cache_data_port_busy_cycles + cs.m_cache_data_port_busy_cycles; 
    ret.m_cache_fill_port_busy_cycles = m_cache_fill_port_busy_cycles + cs.m_cache_fill_port_busy_cycles; 
    ret.m_local_write_misses = m_local_write_misses + cs.m_local_write_misses; 
    ret.m_local_write_misses_full_cacheline = m_local_write_misses_full_cacheline + cs.m_local_write_misses_full_cacheline; 
    return ret;
}

cache_stats &cache_stats::operator+=(const cache_stats &cs){
    ///
    /// Overloaded += operator to allow for simple stat accumulation
    ///
    for(unsigned type=0; type<NUM_MEM_ACCESS_TYPE; ++type){
        for(unsigned status=0; status<NUM_CACHE_REQUEST_STATUS; ++status){
            m_stats[type][status] += cs(type, status);
        }
    }
    m_cache_port_available_cycles += cs.m_cache_port_available_cycles; 
    m_cache_data_port_busy_cycles += cs.m_cache_data_port_busy_cycles; 
    m_cache_fill_port_busy_cycles += cs.m_cache_fill_port_busy_cycles; 
    m_local_write_misses += cs.m_local_write_misses; 
    m_local_write_misses_full_cacheline += cs.m_local_write_misses_full_cacheline; 
    return *this;
}

void cache_stats::print_stats(FILE *fout, const char *cache_name) const{
    ///
    /// Print out each non-zero cache statistic for every memory access type and status
    /// "cache_name" defaults to "Cache_stats" when no argument is provided, otherwise
    /// the provided name is used.
    /// The printed format is "<cache_name>[<request_type>][<request_status>] = <stat_value>"
    ///
    std::string m_cache_name = cache_name;
    for (unsigned type = 0; type < NUM_MEM_ACCESS_TYPE; ++type) {
        for (unsigned status = 0; status < NUM_CACHE_REQUEST_STATUS; ++status) {
            if(m_stats[type][status] > 0){
                fprintf(fout, "\t%s[%s][%s] = %u\n",
                    m_cache_name.c_str(),
                    mem_access_type_str((enum mem_access_type)type),
                    cache_request_status_str((enum cache_request_status)status),
                    m_stats[type][status]);
            }
        }
    }
}

void cache_sub_stats::print_port_stats(FILE *fout, const char *cache_name) const
{
    float data_port_util = 0.0f; 
    if (port_available_cycles > 0) {
        data_port_util = (float) data_port_busy_cycles / port_available_cycles; 
    }
    fprintf(fout, "%s_data_port_util = %.3f\n", cache_name, data_port_util); 
    float fill_port_util = 0.0f; 
    if (port_available_cycles > 0) {
        fill_port_util = (float) fill_port_busy_cycles / port_available_cycles; 
    }
    fprintf(fout, "%s_fill_port_util = %.3f\n", cache_name, fill_port_util); 
}

void cache_sub_stats::print_local_write_misses(FILE *fout, const char *cache_name) const
{
    fprintf(fout, "%s_local_write_misses = %u\n", cache_name, local_write_misses); 
    fprintf(fout, "%s_local_write_misses_full_cacheline = %u\n", cache_name, local_write_misses_full_cacheline); 
}

void cache_sub_stats::print_set_util(FILE *fout, const char *cache_name) const
{
    fprintf(fout, "%s_SetUtility = (avg = %u, min = %u, max = %u, min_count = %u, max_count = %u)\n", 
            cache_name, avg_set_util, min_set_util, max_set_util, min_set_util_count, max_set_util_count); 
}

unsigned cache_stats::get_stats(enum mem_access_type *access_type, unsigned num_access_type, enum cache_request_status *access_status, unsigned num_access_status) const{
    ///
    /// Returns a sum of the stats corresponding to each "access_type" and "access_status" pair.
    /// "access_type" is an array of "num_access_type" mem_access_types.
    /// "access_status" is an array of "num_access_status" cache_request_statuses.
    ///
    unsigned total=0;
    for(unsigned type =0; type < num_access_type; ++type){
        for(unsigned status=0; status < num_access_status; ++status){
            if(!check_valid((int)access_type[type], (int)access_status[status]))
                assert(0 && "Unknown cache access type or access outcome");
            total += m_stats[access_type[type]][access_status[status]];
        }
    }
    return total;
}
void cache_stats::get_sub_stats(struct cache_sub_stats &css) const{
    ///
    /// Overwrites "css" with the appropriate statistics from this cache.
    ///
    struct cache_sub_stats t_css;
    t_css.clear();

    for (unsigned type = 0; type < NUM_MEM_ACCESS_TYPE; ++type) {
        for (unsigned status = 0; status < NUM_CACHE_REQUEST_STATUS; ++status) {
            if(status == HIT || status == MISS || status == HIT_RESERVED)
                t_css.accesses += m_stats[type][status];

            if(status == MISS)
                t_css.misses += m_stats[type][status];

            if(status == HIT_RESERVED)
                t_css.pending_hits += m_stats[type][status];

            if(status == RESERVATION_FAIL)
                t_css.res_fails += m_stats[type][status];
        }
    }

    t_css.port_available_cycles = m_cache_port_available_cycles; 
    t_css.data_port_busy_cycles = m_cache_data_port_busy_cycles; 
    t_css.fill_port_busy_cycles = m_cache_fill_port_busy_cycles; 

    t_css.local_write_misses += m_local_write_misses; 
    t_css.local_write_misses_full_cacheline += m_local_write_misses_full_cacheline; 

    if (m_set_utility.size() > 0) {
        unsigned min_set_util = m_set_utility[0]; 
        unsigned max_set_util = m_set_utility[0]; 
        unsigned avg_set_util = m_set_utility[0]; 
        for (unsigned s = 0; s < m_set_utility.size(); s++) {
            min_set_util = std::min(m_set_utility[s], min_set_util); 
            max_set_util = std::max(m_set_utility[s], max_set_util); 
            avg_set_util += m_set_utility[s]; 
        }
        avg_set_util /= m_set_utility.size(); 

        unsigned min_set_util_count = 0; 
        unsigned max_set_util_count = 0; 
        for (unsigned s = 0; s < m_set_utility.size(); s++) {
            if (m_set_utility[s] == min_set_util) {
                min_set_util_count += 1; 
            }
            if (m_set_utility[s] == max_set_util) {
                max_set_util_count += 1; 
            }
        }
       
        t_css.min_set_util = min_set_util; 
        t_css.max_set_util = max_set_util; 
        t_css.avg_set_util = avg_set_util; 
        t_css.min_set_util_count = min_set_util_count; 
        t_css.max_set_util_count = max_set_util_count; 
    } else {
        t_css.min_set_util = 0; 
        t_css.max_set_util = 0; 
        t_css.avg_set_util = 0; 
        t_css.min_set_util_count = 0; 
        t_css.max_set_util_count = 0; 
    }

    css = t_css;
}

bool cache_stats::check_valid(int type, int status) const{
    ///
    /// Verify a valid access_type/access_status
    ///
    if((type >= 0) && (type < NUM_MEM_ACCESS_TYPE) && (status >= 0) && (status < NUM_CACHE_REQUEST_STATUS))
        return true;
    else
        return false;
}

void cache_stats::sample_cache_port_utility(bool data_port_busy, bool fill_port_busy) 
{
    m_cache_port_available_cycles += 1; 
    if (data_port_busy) {
        m_cache_data_port_busy_cycles += 1; 
    } 
    if (fill_port_busy) {
        m_cache_fill_port_busy_cycles += 1; 
    } 
}

void cache_stats::inc_local_write_misses(bool full_cacheline)
{
    m_local_write_misses++; 
    if (full_cacheline) {
        m_local_write_misses_full_cacheline++; 
    }
}

void cache_stats::inc_set_utility(unsigned set_idx, enum cache_request_status access_status)
{
    if (m_set_utility.size() > 0 and access_status == MISS) {
        m_set_utility.at(set_idx) += 1; 
    }
}

baseline_cache::bandwidth_management::bandwidth_management(cache_config &config) 
: m_config(config)
{
    m_data_port_occupied_cycles = 0; 
    m_fill_port_occupied_cycles = 0; 
}

/// use the data port based on the outcome and events generated by the mem_fetch request 
void baseline_cache::bandwidth_management::use_data_port(mem_fetch *mf, enum cache_request_status outcome, const std::list<cache_event> &events)
{
    unsigned data_size = mf->get_data_size(); 
    unsigned port_width = m_config.m_data_port_width; 
    switch (outcome) {
    case HIT: {
        unsigned data_cycles = data_size / port_width + ((data_size % port_width > 0)? 1 : 0); 
        m_data_port_occupied_cycles += data_cycles; 
        } break; 
    case HIT_RESERVED: 
    case MISS: {
        // the data array is accessed to read out the entire line for write-back 
        if (was_writeback_sent(events)) {
            unsigned data_cycles = m_config.m_line_sz / port_width; 
            m_data_port_occupied_cycles += data_cycles; 
        }
        } break; 
    case RESERVATION_FAIL: 
        // Does not consume any port bandwidth 
        break; 
    default: 
        assert(0); 
        break; 
    } 
}

/// use the fill port 
void baseline_cache::bandwidth_management::use_fill_port(mem_fetch *mf)
{
    // assume filling the entire line with the returned request 
    unsigned fill_cycles = m_config.m_line_sz / m_config.m_data_port_width; 
    m_fill_port_occupied_cycles += fill_cycles; 
}

/// called every cache cycle to free up the ports 
void baseline_cache::bandwidth_management::replenish_port_bandwidth()
{
    if (m_data_port_occupied_cycles > 0) {
        m_data_port_occupied_cycles -= 1; 
    }
    assert(m_data_port_occupied_cycles >= 0); 

    if (m_fill_port_occupied_cycles > 0) {
        m_fill_port_occupied_cycles -= 1; 
    }
    assert(m_fill_port_occupied_cycles >= 0); 
}

/// query for data port availability 
bool baseline_cache::bandwidth_management::data_port_free() const
{
    return (m_data_port_occupied_cycles == 0); 
}

/// query for fill port availability 
bool baseline_cache::bandwidth_management::fill_port_free() const
{
    return (m_fill_port_occupied_cycles == 0); 
}

/// Sends next request to lower level of memory
void baseline_cache::cycle(){
    if ( !m_miss_queue.empty() ) {
        mem_fetch *mf = m_miss_queue.front();
        if ( !m_memport->full(mf->get_data_size(),mf->get_is_write()) ) {
            m_miss_queue.pop_front();
            m_memport->push(mf);
        }
    }
    bool data_port_busy = !m_bandwidth_management.data_port_free(); 
    bool fill_port_busy = !m_bandwidth_management.fill_port_free(); 
    m_stats.sample_cache_port_utility(data_port_busy, fill_port_busy); 
    m_bandwidth_management.replenish_port_bandwidth(); 
}

/// Interface for response from lower memory level (model bandwidth restictions in caller)
void baseline_cache::fill(mem_fetch *mf, unsigned time){
    extra_mf_fields_lookup::iterator e = m_extra_mf_fields.find(mf);
    assert( e != m_extra_mf_fields.end() );
    assert( e->second.m_valid );
    mf->set_data_size( e->second.m_data_size );
    if ( m_config.m_alloc_policy == ON_MISS )
        m_tag_array->fill(e->second.m_cache_index,time);
    else if ( m_config.m_alloc_policy == ON_FILL )
        m_tag_array->fill(e->second.m_block_addr,time);
    else abort();
    bool has_atomic = false;
    m_mshrs.mark_ready(e->second.m_block_addr, has_atomic);
    if (has_atomic) {
        assert(m_config.m_alloc_policy == ON_MISS);
        cache_block_t &block = m_tag_array->get_block(e->second.m_cache_index);
        block.m_status = MODIFIED; // mark line as dirty for atomic operation
    }
    m_extra_mf_fields.erase(mf);
    m_bandwidth_management.use_fill_port(mf); 
}

/// Checks if mf is waiting to be filled by lower memory level
bool baseline_cache::waiting_for_fill( mem_fetch *mf ){
    extra_mf_fields_lookup::iterator e = m_extra_mf_fields.find(mf);
    return e != m_extra_mf_fields.end();
}

void baseline_cache::print(FILE *fp, unsigned &accesses, unsigned &misses) const{
    fprintf( fp, "Cache %s:\t", m_name.c_str() );
    m_tag_array->print(fp,accesses,misses);
    m_mshrs.print_stat(fp);
}

void baseline_cache::display_state( FILE *fp, bool detail ) const{
    fprintf(fp,"Cache %s:\n", m_name.c_str() );
    if (detail) {
        m_tag_array->display(fp);
    }
    m_mshrs.display(fp);
    fprintf(fp, "Miss Queue Length = %zu\n", m_miss_queue.size()); 
    for (auto miss_item = m_miss_queue.begin(); miss_item != m_miss_queue.end(); ++miss_item) {
        (*miss_item)->print(fp, false); 
    }
    fprintf(fp,"\n");
}

/// Read miss handler without writeback
void baseline_cache::send_read_request(new_addr_type addr, new_addr_type block_addr, unsigned cache_index, mem_fetch *mf,
		unsigned time, bool &do_miss, std::list<cache_event> &events, bool read_only, bool wa){

	bool wb=false;
	cache_block_t e;
	send_read_request(addr, block_addr, cache_index, mf, time, do_miss, wb, e, events, read_only, wa);
}

/// Read miss handler. Check MSHR hit or MSHR available
void baseline_cache::send_read_request(new_addr_type addr, new_addr_type block_addr, unsigned cache_index, mem_fetch *mf,
		unsigned time, bool &do_miss, bool &wb, cache_block_t &evicted, std::list<cache_event> &events, bool read_only, bool wa){

    bool mshr_hit = m_mshrs.probe(block_addr);
    bool mshr_avail = !m_mshrs.full(block_addr);
    if ( mshr_hit && mshr_avail ) {
    	if(read_only)
    		m_tag_array->access(block_addr,time,cache_index);
    	else
    		m_tag_array->access(block_addr,time,cache_index,wb,evicted);

        m_mshrs.add(block_addr,mf);
        do_miss = true;
    } else if ( !mshr_hit && mshr_avail && (m_miss_queue.size() < m_config.m_miss_queue_size) ) {
    	if(read_only)
    		m_tag_array->access(block_addr,time,cache_index);
    	else
    		m_tag_array->access(block_addr,time,cache_index,wb,evicted);

        m_mshrs.add(block_addr,mf);
        m_extra_mf_fields[mf] = extra_mf_fields(block_addr,cache_index, mf->get_data_size());
        mf->set_data_size( m_config.get_line_sz() );
        m_miss_queue.push_back(mf);
        mf->set_status(m_miss_queue_status,time);
        if(!wa)
        	events.push_back(READ_REQUEST_SENT);
        do_miss = true;
    }
}


/// Sends write request to lower level memory (write or writeback)
void data_cache::send_write_request(mem_fetch *mf, cache_event request, unsigned time, std::list<cache_event> &events){
    events.push_back(request);
    m_miss_queue.push_back(mf);
    mf->set_status(m_miss_queue_status,time);
}


/****** Write-hit functions (Set by config file) ******/

/// Write-back hit: Mark block as modified
cache_request_status data_cache::wr_hit_wb(new_addr_type addr, unsigned cache_index, mem_fetch *mf, unsigned time, std::list<cache_event> &events, enum cache_request_status status ){
	new_addr_type block_addr = m_config.block_addr(addr);
	m_tag_array->access(block_addr,time,cache_index); // update LRU state
	cache_block_t &block = m_tag_array->get_block(cache_index);
	block.m_status = MODIFIED;

	return HIT;
}

/// Write-through hit: Directly send request to lower level memory
cache_request_status data_cache::wr_hit_wt(new_addr_type addr, unsigned cache_index, mem_fetch *mf, unsigned time, std::list<cache_event> &events, enum cache_request_status status ){
	if(miss_queue_full(0))
		return RESERVATION_FAIL; // cannot handle request this cycle

	new_addr_type block_addr = m_config.block_addr(addr);
	m_tag_array->access(block_addr,time,cache_index); // update LRU state
	cache_block_t &block = m_tag_array->get_block(cache_index);
	block.m_status = MODIFIED;

	// generate a write-through
	send_write_request(mf, WRITE_REQUEST_SENT, time, events);

	return HIT;
}

/// Write-evict hit: Send request to lower level memory and invalidate corresponding block
cache_request_status data_cache::wr_hit_we(new_addr_type addr, unsigned cache_index, mem_fetch *mf, unsigned time, std::list<cache_event> &events, enum cache_request_status status ){
	if(miss_queue_full(0))
		return RESERVATION_FAIL; // cannot handle request this cycle

	// generate a write-through/evict
	cache_block_t &block = m_tag_array->get_block(cache_index);
	send_write_request(mf, WRITE_REQUEST_SENT, time, events);

	// Invalidate block
	block.m_status = INVALID;

	return HIT;
}

/// Global write-evict, local write-back: Useful for private caches
enum cache_request_status data_cache::wr_hit_global_we_local_wb(new_addr_type addr, unsigned cache_index, mem_fetch *mf, unsigned time, std::list<cache_event> &events, enum cache_request_status status ){
	bool evict = (mf->get_access_type() == GLOBAL_ACC_W); // evict a line that hits on global memory write
	if(evict)
		return wr_hit_we(addr, cache_index, mf, time, events, status); // Write-evict
	else
		return wr_hit_wb(addr, cache_index, mf, time, events, status); // Write-back
}

/****** Write-miss functions (Set by config file) ******/

/// Write-allocate miss: Send write request to lower level memory
// and send a read request for the same block
enum cache_request_status
data_cache::wr_miss_wa( new_addr_type addr,
                        unsigned cache_index, mem_fetch *mf,
                        unsigned time, std::list<cache_event> &events,
                        enum cache_request_status status )
{
    new_addr_type block_addr = m_config.block_addr(addr);

    // Write allocate, maximum 3 requests (write miss, read request, write back request)
    // Conservatively ensure the worst-case request can be handled this cycle
    bool mshr_hit = m_mshrs.probe(block_addr);
    bool mshr_avail = !m_mshrs.full(block_addr);
    if(miss_queue_full(2) 
        || (!(mshr_hit && mshr_avail) 
        && !(!mshr_hit && mshr_avail 
        && (m_miss_queue.size() < m_config.m_miss_queue_size))))
        return RESERVATION_FAIL;

    send_write_request(mf, WRITE_REQUEST_SENT, time, events);
    // Tries to send write allocate request, returns true on success and false on failure
    //if(!send_write_allocate(mf, addr, block_addr, cache_index, time, events))
    //    return RESERVATION_FAIL;

    const mem_access_t *ma = new  mem_access_t( m_wr_alloc_type,
                        mf->get_addr(),
                        mf->get_data_size(),
                        false, // Now performing a read
                        mf->get_access_warp_mask(),
                        mf->get_access_byte_mask() );

    mem_fetch *n_mf = new mem_fetch( *ma,
                    NULL,
                    mf->get_ctrl_size(),
                    mf->get_wid(),
                    mf->get_sid(),
                    mf->get_tpc(),
                    mf->get_mem_config());

    bool do_miss = false;
    bool wb = false;
    cache_block_t evicted;

    // Send read request resulting from write miss
    send_read_request(addr, block_addr, cache_index, n_mf, time, do_miss, wb,
        evicted, events, false, true);

    if( do_miss ){
        // If evicted block is modified and not a write-through
        // (already modified lower level)
        if( wb && (m_config.m_write_policy != WRITE_THROUGH) ) { 
            mem_fetch *wb = m_memfetch_creator->alloc(evicted.m_block_addr,
                m_wrbk_type,m_config.get_line_sz(),true);
            m_miss_queue.push_back(wb);
            wb->set_status(m_miss_queue_status,time);
            if (m_tracefile) {
                fprintf(m_tracefile, "EVICTION-wm: set_index=%u %u\n", m_config.set_index(addr), m_config.set_index(evicted.m_block_addr)); 
                mf->print(m_tracefile, false); 
                wb->print(m_tracefile, false); 
            }
        }
        return MISS;
    }

    return RESERVATION_FAIL;
}

/// No write-allocate miss: Simply send write request to lower level memory
enum cache_request_status
data_cache::wr_miss_no_wa( new_addr_type addr,
                           unsigned cache_index,
                           mem_fetch *mf,
                           unsigned time,
                           std::list<cache_event> &events,
                           enum cache_request_status status )
{
    if(miss_queue_full(0))
        return RESERVATION_FAIL; // cannot handle request this cycle

    // on miss, generate write through (no write buffering -- too many threads for that)
    send_write_request(mf, WRITE_REQUEST_SENT, time, events);

    return MISS;
}

/// Best-effort write-allocate miss: 
/// If the entire cache line is overwritten, then just allocate a line and write the data; No need to fetch the old data.  
/// Otherwise, simply send write request to lower level memory
enum cache_request_status data_cache::wr_miss_be_wa(new_addr_type addr, unsigned cache_index, mem_fetch *mf, unsigned time, std::list<cache_event> &events, enum cache_request_status status ){
    if(miss_queue_full(0))
        return RESERVATION_FAIL; // cannot handle request this cycle

    enum mem_access_type type = mf->get_access_type();

    // If local memory access and writing to full cache line, do write-allocate (and evict if required)
    // Treat HIT_RESERVED as a miss, and do a writethrough in that case
    bool write_full_cacheline = true;
    if (type != LOCAL_ACC_W or m_config.get_line_sz() > mf->get_byte_mask().size()) {
        write_full_cacheline = false; // the access cannot cover the entire cache line
    } else {
        mem_access_byte_mask_t byte_mask = mf->get_byte_mask();
        for(unsigned i = 0; i < m_config.get_line_sz(); i++) {
            if(!byte_mask.test(i)) {
                write_full_cacheline = false;
                break;
            }
        }
    }

    // write_full_cacheline = false; // here to temporarily disable the optimization

    if( type == LOCAL_ACC_W and write_full_cacheline and status != HIT_RESERVED) {

        bool do_wb = false;
        cache_block_t evicted;
        m_tag_array->access(addr,time,cache_index,do_wb,evicted);

        // allocate a line and generate write-back traffic if the evicted line is dirty 
        cache_block_t &block = m_tag_array->get_block(cache_index);
        block.fill(time);
        block.m_status = MODIFIED;

        if( do_wb ) {
            mem_fetch *wb = m_memfetch_creator->alloc(evicted.m_block_addr,L1_WRBK_ACC,m_config.get_line_sz(),true);
            events.push_back(WRITE_BACK_REQUEST_SENT);
            m_miss_queue.push_back(wb);
            wb->set_status(m_miss_queue_status,time);
            if (m_tracefile) {
                fprintf(m_tracefile, "EVICTION-wmbe: set_index=%u %u\n", m_config.set_index(addr), m_config.set_index(evicted.m_block_addr)); 
                mf->print(m_tracefile, false); 
                wb->print(m_tracefile, false); 
            }
        }
        m_stats.inc_local_write_misses(true); 

        return HIT; // effectively a hit

    } else {
        // on miss, generate write through (no write buffering -- too many threads for that)
        send_write_request(mf, WRITE_REQUEST_SENT, time, events);

        g_debug_dcache_write_miss += 1;
        if (type == LOCAL_ACC_W) 
            m_stats.inc_local_write_misses(false); 

        return MISS;
    }
}

/****** Read hit functions (Set by config file) ******/

/// Baseline read hit: Update LRU status of block.
// Special case for atomic instructions -> Mark block as modified
enum cache_request_status
data_cache::rd_hit_base( new_addr_type addr,
                         unsigned cache_index,
                         mem_fetch *mf,
                         unsigned time,
                         std::list<cache_event> &events,
                         enum cache_request_status status )
{
    new_addr_type block_addr = m_config.block_addr(addr);
    m_tag_array->access(block_addr,time,cache_index);
    // Atomics treated as global read/write requests - Perform read, mark line as
    // MODIFIED
    if(mf->isatomic()){ 
        assert(mf->get_access_type() == GLOBAL_ACC_R);
        cache_block_t &block = m_tag_array->get_block(cache_index);
        block.m_status = MODIFIED;  // mark line as dirty
    }
    return HIT;
}

/****** Read miss functions (Set by config file) ******/

/// Baseline read miss: Send read request to lower level memory,
// perform write-back as necessary
enum cache_request_status
data_cache::rd_miss_base( new_addr_type addr,
                          unsigned cache_index,
                          mem_fetch *mf,
                          unsigned time,
                          std::list<cache_event> &events,
                          enum cache_request_status status ){
    if(miss_queue_full(1))
        // cannot handle request this cycle
        // (might need to generate two requests)
        return RESERVATION_FAIL; 

    new_addr_type block_addr = m_config.block_addr(addr);
    bool do_miss = false;
    bool wb = false;
    cache_block_t evicted;
    send_read_request( addr,
                       block_addr,
                       cache_index,
                       mf, time, do_miss, wb, evicted, events, false, false);

    if( do_miss ){
        // If evicted block is modified and not a write-through
        // (already modified lower level)
        if(wb && (m_config.m_write_policy != WRITE_THROUGH) ){ 
            mem_fetch *wb = m_memfetch_creator->alloc(evicted.m_block_addr,
                m_wrbk_type,m_config.get_line_sz(),true);
            if (m_tracefile) {
                fprintf(m_tracefile, "EVICTION-rm: set_index=%u %u\n", m_config.set_index(addr), m_config.set_index(evicted.m_block_addr)); 
                mf->print(m_tracefile, false); 
                wb->print(m_tracefile, false); 
            }
            send_write_request(wb, WRITE_BACK_REQUEST_SENT, time, events);
        }
        return MISS;
    }
    return RESERVATION_FAIL;
}

/// Access cache for read_only_cache: returns RESERVATION_FAIL if
// request could not be accepted (for any reason)
enum cache_request_status
read_only_cache::access( new_addr_type addr,
                         mem_fetch *mf,
                         unsigned time,
                         std::list<cache_event> &events )
{
    assert( mf->get_data_size() <= m_config.get_line_sz());
    assert(m_config.m_write_policy == READ_ONLY);
    assert(!mf->get_is_write());
    new_addr_type block_addr = m_config.block_addr(addr);
    unsigned cache_index = (unsigned)-1;
    enum cache_request_status status = m_tag_array->probe(block_addr,cache_index);
    enum cache_request_status cache_status = RESERVATION_FAIL;

    if ( status == HIT ) {
        cache_status = m_tag_array->access(block_addr,time,cache_index); // update LRU state
    }else if ( status != RESERVATION_FAIL ) {
        if(!miss_queue_full(0)){
            bool do_miss=false;
            send_read_request(addr, block_addr, cache_index, mf, time, do_miss, events, true, false);
            if(do_miss)
                cache_status = MISS;
            else
                cache_status = RESERVATION_FAIL;
        }else{
            cache_status = RESERVATION_FAIL;
        }
    }

    m_stats.inc_stats(mf->get_access_type(), m_stats.select_stats_status(status, cache_status));
    return cache_status;
}

//! A general function that takes the result of a tag_array probe
//  and performs the correspding functions based on the cache configuration
//  The access fucntion calls this function
enum cache_request_status
data_cache::process_tag_probe( bool wr,
                               enum cache_request_status probe_status,
                               new_addr_type addr,
                               unsigned cache_index,
                               mem_fetch* mf,
                               unsigned time,
                               std::list<cache_event>& events )
{
    // Each function pointer ( m_[rd/wr]_[hit/miss] ) is set in the
    // data_cache constructor to reflect the corresponding cache configuration
    // options. Function pointers were used to avoid many long conditional
    // branches resulting from many cache configuration options.
    cache_request_status access_status = probe_status;
    if(wr){ // Write
        if(probe_status == HIT){
            access_status = (this->*m_wr_hit)( addr,
                                      cache_index,
                                      mf, time, events, probe_status );
        }else if ( probe_status != RESERVATION_FAIL ) {
            access_status = (this->*m_wr_miss)( addr,
                                       cache_index,
                                       mf, time, events, probe_status );
        }
    }else{ // Read
        if(probe_status == HIT){
            access_status = (this->*m_rd_hit)( addr,
                                      cache_index,
                                      mf, time, events, probe_status );
        }else if ( probe_status != RESERVATION_FAIL ) {
            access_status = (this->*m_rd_miss)( addr,
                                       cache_index,
                                       mf, time, events, probe_status );
        }
    }

    m_bandwidth_management.use_data_port(mf, access_status, events); 
    return access_status;
}

// Both the L1 and L2 currently use the same access function.
// Differentiation between the two caches is done through configuration
// of caching policies.
// Both the L1 and L2 override this function to provide a means of
// performing actions specific to each cache when such actions are implemnted.
enum cache_request_status
data_cache::access( new_addr_type addr,
                    mem_fetch *mf,
                    unsigned time,
                    std::list<cache_event> &events )
{
    assert( mf->get_data_size() <= m_config.get_line_sz());
    bool wr = mf->get_is_write();
    new_addr_type block_addr = m_config.block_addr(addr);
    unsigned cache_index = (unsigned)-1;
    enum cache_request_status probe_status
        = m_tag_array->probe( block_addr, cache_index );
    if (m_tracefile) {
        fprintf(m_tracefile, "ACCESS: set_index=%u outcome=%d\n", m_config.set_index(block_addr), probe_status); 
        mf->print(m_tracefile, false); 
    }
    enum cache_request_status access_status
        = process_tag_probe( wr, probe_status, addr, cache_index, mf, time, events );
    m_stats.inc_stats(mf->get_access_type(),
        m_stats.select_stats_status(probe_status, access_status));
    m_stats.inc_set_utility(m_config.set_index(addr), access_status); 
    return access_status;
}

/// This is meant to model the first level data cache in Fermi.
/// It is write-evict (global) or write-back (local) at the
/// granularity of individual blocks (Set by GPGPU-Sim configuration file)
/// (the policy used in fermi according to the CUDA manual)
enum cache_request_status
l1_cache::access( new_addr_type addr,
                  mem_fetch *mf,
                  unsigned time,
                  std::list<cache_event> &events )
{
    // tx load overrides 
    if (m_recognize_tx_load and mf->is_tx_load()) {
        enum cache_request_status access_status = tx_load_access(addr, mf, time, events);
        m_stats.inc_stats(mf->get_access_type(), access_status);
        return access_status; 
    }
    return data_cache::access( addr, mf, time, events );
}

// The l2 cache access function calls the base data_cache access
// implementation.  When the L2 needs to diverge from L1, L2 specific
// changes should be made here.
enum cache_request_status
l2_cache::access( new_addr_type addr,
                  mem_fetch *mf,
                  unsigned time,
                  std::list<cache_event> &events )
{
    return data_cache::access( addr, mf, time, events );
}

/// Access function for tex_cache
/// return values: RESERVATION_FAIL if request could not be accepted
/// otherwise returns HIT_RESERVED or MISS; NOTE: *never* returns HIT
/// since unlike a normal CPU cache, a "HIT" in texture cache does not
/// mean the data is ready (still need to get through fragment fifo)
enum cache_request_status tex_cache::access( new_addr_type addr, mem_fetch *mf,
    unsigned time, std::list<cache_event> &events )
{
    if ( m_fragment_fifo.full() || m_request_fifo.full() || m_rob.full() )
        return RESERVATION_FAIL;

    assert( mf->get_data_size() <= m_config.get_line_sz());

    // at this point, we will accept the request : access tags and immediately allocate line
    new_addr_type block_addr = m_config.block_addr(addr);
    unsigned cache_index = (unsigned)-1;
    enum cache_request_status status = m_tags.access(block_addr,time,cache_index);
    enum cache_request_status cache_status = RESERVATION_FAIL;
    assert( status != RESERVATION_FAIL );
    assert( status != HIT_RESERVED ); // as far as tags are concerned: HIT or MISS
    m_fragment_fifo.push( fragment_entry(mf,cache_index,status==MISS,mf->get_data_size()) );
    if ( status == MISS ) {
        // we need to send a memory request...
        unsigned rob_index = m_rob.push( rob_entry(cache_index, mf, block_addr) );
        m_extra_mf_fields[mf] = extra_mf_fields(rob_index);
        mf->set_data_size(m_config.get_line_sz());
        m_tags.fill(cache_index,time); // mark block as valid
        m_request_fifo.push(mf);
        mf->set_status(m_request_queue_status,time);
        events.push_back(READ_REQUEST_SENT);
        cache_status = MISS;
    } else {
        // the value *will* *be* in the cache already
        cache_status = HIT_RESERVED;
    }
    m_stats.inc_stats(mf->get_access_type(), m_stats.select_stats_status(status, cache_status));
    return cache_status;
}

void tex_cache::cycle(){
    // send next request to lower level of memory
    if ( !m_request_fifo.empty() ) {
        mem_fetch *mf = m_request_fifo.peek();
        if ( !m_memport->full(mf->get_ctrl_size(),false) ) {
            m_request_fifo.pop();
            m_memport->push(mf);
        }
    }
    // read ready lines from cache
    if ( !m_fragment_fifo.empty() && !m_result_fifo.full() ) {
        const fragment_entry &e = m_fragment_fifo.peek();
        if ( e.m_miss ) {
            // check head of reorder buffer to see if data is back from memory
            unsigned rob_index = m_rob.next_pop_index();
            const rob_entry &r = m_rob.peek(rob_index);
            assert( r.m_request == e.m_request );
            assert( r.m_block_addr == m_config.block_addr(e.m_request->get_addr()) );
            if ( r.m_ready ) {
                assert( r.m_index == e.m_cache_index );
                m_cache[r.m_index].m_valid = true;
                m_cache[r.m_index].m_block_addr = r.m_block_addr;
                m_result_fifo.push(e.m_request);
                m_rob.pop();
                m_fragment_fifo.pop();
            }
        } else {
            // hit:
            assert( m_cache[e.m_cache_index].m_valid );
            assert( m_cache[e.m_cache_index].m_block_addr
                == m_config.block_addr(e.m_request->get_addr()) );
            m_result_fifo.push( e.m_request );
            m_fragment_fifo.pop();
        }
    }
}

/// Place returning cache block into reorder buffer
void tex_cache::fill( mem_fetch *mf, unsigned time )
{
    extra_mf_fields_lookup::iterator e = m_extra_mf_fields.find(mf);
    assert( e != m_extra_mf_fields.end() );
    assert( e->second.m_valid );
    assert( !m_rob.empty() );
    mf->set_status(m_rob_status,time);

    unsigned rob_index = e->second.m_rob_index;
    rob_entry &r = m_rob.peek(rob_index);
    assert( !r.m_ready );
    r.m_ready = true;
    r.m_time = time;
    assert( r.m_block_addr == m_config.block_addr(mf->get_addr()) );
}

void tex_cache::display_state( FILE *fp ) const
{
    fprintf(fp,"%s (texture cache) state:\n", m_name.c_str() );
    fprintf(fp,"fragment fifo entries  = %u / %u\n",
        m_fragment_fifo.size(), m_fragment_fifo.capacity() );
    fprintf(fp,"reorder buffer entries = %u / %u\n",
        m_rob.size(), m_rob.capacity() );
    fprintf(fp,"request fifo entries   = %u / %u\n",
        m_request_fifo.size(), m_request_fifo.capacity() );
    if ( !m_rob.empty() )
        fprintf(fp,"reorder buffer contents:\n");
    for ( int n=m_rob.size()-1; n>=0; n-- ) {
        unsigned index = (m_rob.next_pop_index() + n)%m_rob.capacity();
        const rob_entry &r = m_rob.peek(index);
        fprintf(fp, "tex rob[%3d] : %s ",
            index, (r.m_ready?"ready  ":"pending") );
        if ( r.m_ready )
            fprintf(fp,"@%6u", r.m_time );
        else
            fprintf(fp,"       ");
        fprintf(fp,"[idx=%4u]",r.m_index);
        r.m_request->print(fp,false);
    }
    if ( !m_fragment_fifo.empty() ) {
        fprintf(fp,"fragment fifo (oldest) :");
        fragment_entry &f = m_fragment_fifo.peek();
        fprintf(fp,"%s:          ", f.m_miss?"miss":"hit ");
        f.m_request->print(fp,false);
    }
}
/******************************************************************************************************************************************/



unsigned g_debug_dcache_write_miss = 0;

// special access routine for tx load: does not read from the cache for data, but writes data to specified cache line
enum cache_request_status l1_cache::tx_load_access( new_addr_type addr, mem_fetch *mf, unsigned time, std::list<cache_event> &events )
{
   assert(mf->get_is_write() == false);
   enum mem_access_type type = mf->get_access_type();
   assert(type == GLOBAL_ACC_R); 

   new_addr_type fill_addr = mf->get_fill_addr(); // fill address = read-log buffer for value
   new_addr_type block_addr = m_config.block_addr(fill_addr); 
   unsigned cache_index = (unsigned)-1;
   enum cache_request_status status = m_tag_array->probe(block_addr,cache_index);

   if (m_tracefile) {
      fprintf(m_tracefile, "TX_LOAD: set_index=%u outcome=%d\n", m_config.set_index(block_addr), status); 
      mf->print(m_tracefile, false); 
   }

   bool wb = false;
   cache_block_t evicted;
   if ( status == RESERVATION_FAIL) {
      // has to ensure that there is space to write the data prior to sending fetch request
      return RESERVATION_FAIL; 
   } else if ( status == HIT ) {
      // no eviction needed, check for one miss queue slot
      if ( m_miss_queue.size() >= m_config.m_miss_queue_size )
         return RESERVATION_FAIL; 

      // HIT is possible if this read-log buffer was written in a previous transaction (or retry)
      m_tag_array->access(fill_addr, time, cache_index, wb, evicted); // update LRU state 

      // change the block to reserved as it will be overwritten 
      cache_block_t &block = m_tag_array->get_block(cache_index); 
      block.allocate( m_config.tag(block_addr), block_addr, time );
   } else if ( status == HIT_RESERVED ) {
      // no eviction needed, check for one miss queue slot
      if ( m_miss_queue.size() >= m_config.m_miss_queue_size )
         return RESERVATION_FAIL; 

      m_tag_array->access(fill_addr, time, cache_index, wb, evicted);
   } else {
      assert( status == MISS );
      if ( (m_miss_queue.size() + 1) >= m_config.m_miss_queue_size )
         return RESERVATION_FAIL; // cannot handle request this cycle (might need to generate two requests)

      m_tag_array->access(fill_addr, time, cache_index, wb, evicted);
      if (wb) {
         // writeback the evicted line (if it is dirty)
         mem_fetch *wb_mf = m_memfetch_creator->alloc(evicted.m_block_addr,L1_WRBK_ACC,m_config.get_line_sz(),true);
         events.push_back(WRITE_BACK_REQUEST_SENT);
         m_miss_queue.push_back(wb_mf);
         wb_mf->set_status(m_miss_queue_status, time); 
         if (m_tracefile) {
            fprintf(m_tracefile, "EVICTION-tx: set_index=%u %u\n", m_config.set_index(block_addr), m_config.set_index(evicted.m_block_addr)); 
            mf->print(m_tracefile, false); 
            wb_mf->print(m_tracefile, false); 
         }
      }
   }

   // send request after ensuring the read-log buffer is allocated 
   m_extra_mf_fields[mf] = extra_mf_fields(block_addr,cache_index, mf->get_data_size());
   m_miss_queue.push_back(mf);
   mf->set_status(m_miss_queue_status,time); 
   events.push_back(READ_REQUEST_SENT);

   cache_block_t &block = m_tag_array->get_block(cache_index); 
   block.add_partial_fill(); 

   return HIT_RESERVED; 
}

void l1_cache::tx_load_fill( mem_fetch *mf, unsigned time )
{
   extra_mf_fields_lookup::iterator e = m_extra_mf_fields.find(mf); 
   assert( e != m_extra_mf_fields.end() );
   assert( e->second.m_valid );
   // mf->set_data_size( e->second.m_data_size ); // WF: don't know what this is for
   assert( m_config.m_alloc_policy == ON_MISS );
   cache_block_t &block = m_tag_array->get_block(e->second.m_cache_index); 
   block.partial_fill(time);
   assert( block.m_block_addr == m_config.block_addr(mf->get_fill_addr()) ); // ensure it is filling the right line 
   if (m_tracefile) {
      fprintf(m_tracefile, "FILL-tx: set_index=%u pending_fills=%d\n", m_config.set_index(block.m_block_addr), block.m_pending_fills); 
      mf->print(m_tracefile, false); 
   }
   m_extra_mf_fields.erase(mf);
}

// marks the line as non-dirty - used to specify when the content of the transaction log is no longer needed 
void l1_cache::mark_last_use( new_addr_type addr, bool check_hit )
{
   new_addr_type block_addr = m_config.block_addr(addr); 
   unsigned cache_index = (unsigned)-1;
   enum cache_request_status status = m_tag_array->probe(block_addr,cache_index);
   if (check_hit) 
      assert ( status == HIT ); 
   
   if (status == HIT) {
      cache_block_t &block = m_tag_array->get_block(cache_index); 

      block.mark_last_use(); 
   }
}

void cache_config::init(char * config, FuncCache status)
{
    cache_status = status;
    assert( config );
    char rp, wp, ap, mshr_type, wap;
    char set_idx_hash = 'B';

    int ntok = sscanf(config,"%u:%u:%u,%c:%c:%c:%c,%c:%u:%u,%u:%u,%u,%c",
                      &m_nset, &m_line_sz, &m_assoc, &rp, &wp, &ap, &wap,
                      &mshr_type, &m_mshr_entries,&m_mshr_max_merge,
                      &m_miss_queue_size,&m_result_fifo_entries,
                      &m_data_port_width,&set_idx_hash);

    if ( ntok < 11 ) {
        if ( !strcmp(config,"none") ) {
            m_disabled = true;
            return;
        }
        exit_parse_error();
    }
    switch (rp) {
    case 'L': m_replacement_policy = LRU; break;
    case 'F': m_replacement_policy = FIFO; break;
    default: exit_parse_error();
    }
    switch (wp) {
    case 'R': m_write_policy = READ_ONLY; break;
    case 'B': m_write_policy = WRITE_BACK; break;
    case 'T': m_write_policy = WRITE_THROUGH; break;
    case 'E': m_write_policy = WRITE_EVICT; break;
    case 'L': m_write_policy = LOCAL_WB_GLOBAL_WT; break;
    default: exit_parse_error();
    }
    switch (ap) {
    case 'm': m_alloc_policy = ON_MISS; break;
    case 'f': m_alloc_policy = ON_FILL; break;
    default: exit_parse_error();
    }
    switch(wap){
    case 'W': m_write_alloc_policy = WRITE_ALLOCATE; break;
    case 'N': m_write_alloc_policy = NO_WRITE_ALLOCATE; break;
    case 'L': m_write_alloc_policy = LOCAL_ONLY_WRITE_ALLOCATE; break;
    default: exit_parse_error();
    }
    switch (mshr_type) {
    case 'F': m_mshr_type = TEX_FIFO; assert(ntok==12); break;
    case 'A': m_mshr_type = ASSOC; break;
    default: exit_parse_error();
    }
    switch (set_idx_hash) {
    case 'H': m_use_set_index_hash = true; break;
    default: break; 
    }
    m_line_sz_log2 = LOGB2(m_line_sz);
    m_full_line_mask.reset(); 
    for (unsigned b = 0; b < m_line_sz && b < m_full_line_mask.size(); b++) m_full_line_mask.set(b); 
    m_nset_log2 = LOGB2(m_nset);

    // detect invalid configuration 
    if (m_alloc_policy == ON_FILL and m_write_policy == WRITE_BACK) {
        // A writeback cache with allocate-on-fill policy will inevitably lead to deadlock:  
        // The deadlock happens when an incoming cache-fill evicts a dirty
        // line, generating a writeback request.  If the memory subsystem
        // is congested, the interconnection network may not have
        // sufficient buffer for the writeback request.  This stalls the
        // incoming cache-fill.  The stall may propagate through the memory
        // subsystem back to the output port of the same core, creating a
        // deadlock where the wrtieback request and the incoming cache-fill
        // are stalling each other.  
        assert(0 && "Invalid cache configuration: Writeback cache cannot allocate new line on fill. "); 
    }

    m_h3mask.resize(m_nset_log2); 
    srand(0x01010101); 
    for (unsigned s = 0; s < m_nset_log2; s++) {
        m_h3mask[s] = rand(); 
    }

    // default: port to data array width and granularity = line size 
    if (m_data_port_width == 0) {
        m_data_port_width = m_line_sz; 
    }
    assert(m_line_sz % m_data_port_width == 0); 

    m_valid = true;
}

#define ODD_HASH_MULTIPLE 73
// TODO make this a runtime option
#define USE_SET_INDEX_HASH 0

unsigned cache_config::set_index( new_addr_type addr ) const
{
   unsigned set;
   if (m_use_set_index_hash) {
      set = set_index_hashed( addr );
   } else {
      set = (addr >> m_line_sz_log2) & (m_nset-1);
   }
   assert( set < m_nset );
   return set;
}

unsigned cache_config::set_index_hashed( new_addr_type addr ) const
{
   #if 1
   new_addr_type set = (addr >> m_line_sz_log2) & (m_nset - 1);
   new_addr_type tag = (addr >> (m_line_sz_log2+m_nset_log2));
   tag /= ODD_HASH_MULTIPLE;

   set = (set + tag) & (m_nset - 1);
   return set;

   #else
   new_addr_type block_addr = this->block_addr(addr); 
   std::bitset<32> output; 
   output.reset(); 
   for (unsigned n = 0; n < m_nset_log2; n++) {
      std::bitset<32> qnx = block_addr & m_h3mask[n]; 
      bool bit = (qnx.count() % 2); // xor odd #1s = 1, even #1s = 0 
      output.set(n, bit); 
   }
   return output.to_ulong(); 
   #endif
}


