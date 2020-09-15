// Copyright (c) 2009-2011, Tor M. Aamodt
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <list>
#include <set>

#include "../option_parser.h"
#include "mem_fetch.h"
#include "dram.h"
#include "gpu-cache.h"
#include "histogram.h"
#include "l2cache.h"
#include "../intersim/statwraper.h"
#include "../abstract_hardware_model.h"
#include "gpu-sim.h"
#include "shader.h"
#include "mem_latency_stat.h"
#include "l2cache_trace.h"


mem_fetch * partition_mf_allocator::alloc(new_addr_type addr, mem_access_type type, unsigned size, bool wr ) const 
{
    assert( wr );
    mem_access_t access( type, addr, size, wr );
    mem_fetch *mf = new mem_fetch( access, 
                                   NULL,
                                   WRITE_PACKET_SIZE, 
                                   -1, 
                                   -1, 
                                   -1,
                                   m_memory_config );
    return mf;
}

memory_partition_unit::memory_partition_unit( unsigned partition_id, 
                                              const struct memory_config *config,
                                              class memory_stats_t *stats, 
                                              const shader_core_config *shader_config )
: m_id(partition_id), m_config(config), m_stats(stats), m_arbitration_metadata(config) 
{
    m_dram = new dram_t(m_id,m_config,m_stats,this);

    m_sub_partition = new memory_sub_partition*[m_config->m_n_sub_partition_per_memory_channel]; 
    for (unsigned p = 0; p < m_config->m_n_sub_partition_per_memory_channel; p++) {
        unsigned sub_partition_id = m_id * m_config->m_n_sub_partition_per_memory_channel + p; 
        m_sub_partition[p] = new memory_sub_partition(sub_partition_id, m_config, stats, shader_config); 
    }
}

memory_partition_unit::~memory_partition_unit() 
{
    delete m_dram; 
    for (unsigned p = 0; p < m_config->m_n_sub_partition_per_memory_channel; p++) {
        delete m_sub_partition[p]; 
    } 
    delete[] m_sub_partition; 
}

memory_partition_unit::arbitration_metadata::arbitration_metadata(const struct memory_config *config) 
: m_last_borrower(config->m_n_sub_partition_per_memory_channel - 1), 
  m_private_credit(config->m_n_sub_partition_per_memory_channel, 0), 
  m_shared_credit(0) 
{
    // each sub partition get at least 1 credit for forward progress 
    // the rest is shared among with other partitions 
    m_private_credit_limit = 1; 
    m_shared_credit_limit = config->gpgpu_frfcfs_dram_sched_queue_size 
                            + config->gpgpu_dram_return_queue_size 
                            - (config->m_n_sub_partition_per_memory_channel - 1); 
    if (config->gpgpu_frfcfs_dram_sched_queue_size == 0 
        or config->gpgpu_dram_return_queue_size == 0) 
    {
        m_shared_credit_limit = 0; // no limit if either of the queue has no limit in size 
    }
    assert(m_shared_credit_limit >= 0); 
}

bool memory_partition_unit::arbitration_metadata::has_credits(int inner_sub_partition_id) const 
{
    int spid = inner_sub_partition_id; 
    if (m_private_credit[spid] < m_private_credit_limit) {
        return true; 
    } else if (m_shared_credit_limit == 0 || m_shared_credit < m_shared_credit_limit) {
        return true; 
    } else {
        return false; 
    }
}

void memory_partition_unit::arbitration_metadata::borrow_credit(int inner_sub_partition_id) 
{
    int spid = inner_sub_partition_id; 
    if (m_private_credit[spid] < m_private_credit_limit) {
        m_private_credit[spid] += 1; 
    } else if (m_shared_credit_limit == 0 || m_shared_credit < m_shared_credit_limit) {
        m_shared_credit += 1; 
    } else {
        assert(0 && "DRAM arbitration error: Borrowing from depleted credit!"); 
    }
    m_last_borrower = spid; 
}

void memory_partition_unit::arbitration_metadata::return_credit(int inner_sub_partition_id) 
{
    int spid = inner_sub_partition_id; 
    if (m_private_credit[spid] > 0) {
        m_private_credit[spid] -= 1; 
    } else {
        m_shared_credit -= 1; 
    } 
    assert((m_shared_credit >= 0) && "DRAM arbitration error: Returning more than available credits!"); 
}

void memory_partition_unit::arbitration_metadata::print( FILE *fp ) const 
{
    fprintf(fp, "private_credit = "); 
    for (unsigned p = 0; p < m_private_credit.size(); p++) {
        fprintf(fp, "%d ", m_private_credit[p]); 
    }
    fprintf(fp, "(limit = %d)\n", m_private_credit_limit); 
    fprintf(fp, "shared_credit = %d (limit = %d)\n", m_shared_credit, m_shared_credit_limit); 
}

bool memory_partition_unit::busy() const 
{
    bool busy = false; 
    for (unsigned p = 0; p < m_config->m_n_sub_partition_per_memory_channel; p++) {
        if (m_sub_partition[p]->busy()) {
            busy = true; 
        }
    }
    return busy; 
}

void memory_partition_unit::cache_cycle(unsigned cycle) 
{
    for (unsigned p = 0; p < m_config->m_n_sub_partition_per_memory_channel; p++) {
        m_sub_partition[p]->cache_cycle(cycle); 
    }
}

void memory_partition_unit::visualizer_print( gzFile visualizer_file ) const 
{
    m_dram->visualizer_print(visualizer_file);
    for (unsigned p = 0; p < m_config->m_n_sub_partition_per_memory_channel; p++) {
        m_sub_partition[p]->visualizer_print(visualizer_file); 
    }
}

// determine whether a given subpartition can issue to DRAM 
bool memory_partition_unit::can_issue_to_dram(int inner_sub_partition_id) 
{
    int spid = inner_sub_partition_id; 
    bool sub_partition_contention = m_sub_partition[spid]->dram_L2_queue_full(); 
    bool has_dram_resource = m_arbitration_metadata.has_credits(spid); 

    MEMPART_DPRINTF("sub partition %d sub_partition_contention=%c has_dram_resource=%c\n", 
                    spid, (sub_partition_contention)? 'T':'F', (has_dram_resource)? 'T':'F'); 

    return (has_dram_resource && !sub_partition_contention); 
}

int memory_partition_unit::global_sub_partition_id_to_local_id(int global_sub_partition_id) const
{
    return (global_sub_partition_id - m_id * m_config->m_n_sub_partition_per_memory_channel); 
}

void memory_partition_unit::dram_cycle() 
{ 
    // pop completed memory request from dram and push it to dram-to-L2 queue 
    // of the original sub partition 
    mem_fetch* mf_return = m_dram->return_queue_top();
    if (mf_return) {
        unsigned dest_global_spid = mf_return->get_sub_partition_id(); 
        int dest_spid = global_sub_partition_id_to_local_id(dest_global_spid); 
        assert(m_sub_partition[dest_spid]->get_id() == dest_global_spid); 
        if (!m_sub_partition[dest_spid]->dram_L2_queue_full()) {
            if( mf_return->get_access_type() == L1_WRBK_ACC ) {
                m_sub_partition[dest_spid]->set_done(mf_return); 
                delete mf_return;
            } else {
                m_sub_partition[dest_spid]->dram_L2_queue_push(mf_return);
                mf_return->set_status(IN_PARTITION_DRAM_TO_L2_QUEUE,gpu_sim_cycle+gpu_tot_sim_cycle);
                m_arbitration_metadata.return_credit(dest_spid); 
                MEMPART_DPRINTF("mem_fetch request %p return from dram to sub partition %d\n", mf_return, dest_spid); 
            }
            m_dram->return_queue_pop(); 
        }
    } else {
        m_dram->return_queue_pop(); 
    }
    
    m_dram->cycle(); 
    m_dram->dram_log(SAMPLELOG);   

    if( !m_dram->full() ) {
        // L2->DRAM queue to DRAM latency queue
        // Arbitrate among multiple L2 subpartitions 
        int last_issued_partition = m_arbitration_metadata.last_borrower(); 
        for (unsigned p = 0; p < m_config->m_n_sub_partition_per_memory_channel; p++) {
            int spid = (p + last_issued_partition + 1) % m_config->m_n_sub_partition_per_memory_channel; 
            if (!m_sub_partition[spid]->L2_dram_queue_empty() && can_issue_to_dram(spid)) {
                mem_fetch *mf = m_sub_partition[spid]->L2_dram_queue_top();
                m_sub_partition[spid]->L2_dram_queue_pop();
                MEMPART_DPRINTF("Issue mem_fetch request %p from sub partition %d to dram\n", mf, spid); 
                dram_delay_t d;
                d.req = mf;
                d.ready_cycle = gpu_sim_cycle+gpu_tot_sim_cycle + m_config->dram_latency;
                m_dram_latency_queue.push_back(d);
                mf->set_status(IN_PARTITION_DRAM_LATENCY_QUEUE,gpu_sim_cycle+gpu_tot_sim_cycle);
                m_arbitration_metadata.borrow_credit(spid); 
                break;  // the DRAM should only accept one request per cycle 
            }
        }
    }

    // DRAM latency queue
    if( !m_dram_latency_queue.empty() && ( (gpu_sim_cycle+gpu_tot_sim_cycle) >= m_dram_latency_queue.front().ready_cycle ) && !m_dram->full() ) {
        mem_fetch* mf = m_dram_latency_queue.front().req;
        m_dram_latency_queue.pop_front();
        m_dram->push(mf);
    }
}

void memory_partition_unit::set_done( mem_fetch *mf )
{
    unsigned global_spid = mf->get_sub_partition_id(); 
    int spid = global_sub_partition_id_to_local_id(global_spid); 
    assert(m_sub_partition[spid]->get_id() == global_spid); 
    if (mf->get_access_type() == L1_WRBK_ACC || mf->get_access_type() == L2_WRBK_ACC) {
        m_arbitration_metadata.return_credit(spid); 
        MEMPART_DPRINTF("mem_fetch request %p return from dram to sub partition %d\n", mf, spid); 
    }
    m_sub_partition[spid]->set_done(mf); 
}

void memory_partition_unit::set_dram_power_stats(unsigned &n_cmd,
                                                 unsigned &n_activity,
                                                 unsigned &n_nop,
                                                 unsigned &n_act,
                                                 unsigned &n_pre,
                                                 unsigned &n_rd,
                                                 unsigned &n_wr,
                                                 unsigned &n_req) const
{
    m_dram->set_dram_power_stats(n_cmd, n_activity, n_nop, n_act, n_pre, n_rd, n_wr, n_req);
}

void memory_partition_unit::print( FILE *fp ) const
{
    fprintf(fp, "Memory Partition %u: \n", m_id); 
    for (unsigned p = 0; p < m_config->m_n_sub_partition_per_memory_channel; p++) {
        m_sub_partition[p]->print(fp); 
    }
    fprintf(fp, "In Dram Latency Queue (total = %zd): \n", m_dram_latency_queue.size()); 
    for (std::list<dram_delay_t>::const_iterator mf_dlq = m_dram_latency_queue.begin(); 
         mf_dlq != m_dram_latency_queue.end(); ++mf_dlq) {
        mem_fetch *mf = mf_dlq->req; 
        fprintf(fp, "Ready @ %llu - ", mf_dlq->ready_cycle); 
        if (mf) 
            mf->print(fp); 
        else 
            fprintf(fp, " <NULL mem_fetch?>\n"); 
    }
    m_dram->print(fp); 
}

memory_sub_partition::memory_sub_partition( unsigned sub_partition_id, 
                                            const struct memory_config *config,
                                            class memory_stats_t *stats,
                                            const shader_core_config *shader_config )
{
    m_id = sub_partition_id;
    m_config=config;
    m_stats=stats;

    assert(m_id < m_config->m_n_mem_sub_partition); 

    char L2c_name[32];
    snprintf(L2c_name, 32, "L2_bank_%03d", m_id);
    m_L2interface = new L2interface(this);
    m_mf_allocator = new partition_mf_allocator(config);

    if(!m_config->m_L2_config.disabled())
       m_L2cache = new l2_cache(L2c_name,m_config->m_L2_config,-1,-1,m_L2interface,m_mf_allocator,IN_PARTITION_L2_MISS_QUEUE);

    n_mem_to_simt_at_pop=0;
    unsigned int icnt_L2;
    unsigned int L2_dram;
    unsigned int dram_L2;
    unsigned int L2_icnt;
    int ntok = sscanf(m_config->gpgpu_L2_queue_config,"%u:%u:%u:%u", &icnt_L2,&L2_dram,&dram_L2,&L2_icnt);
    if (ntok < 4) {
        printf("GPGPU-Sim uArch: error while parsing configuration string gpgpu_L2_queue_config.\n");
	abort();
    }
    m_icnt_L2_queue = new fifo_pipeline<mem_fetch>("icnt-to-L2",0,icnt_L2); 
    m_L2_dram_queue = new fifo_pipeline<mem_fetch>("L2-to-dram",0,L2_dram);
    m_dram_L2_queue = new fifo_pipeline<mem_fetch>("dram-to-L2",0,dram_L2);
    m_L2_icnt_queue = new fifo_pipeline<mem_fetch>("L2-to-icnt",0,L2_icnt);
    wb_addr=-1;

    m_cu_response_iface = new L2icnt_interface(this);
    if (g_tm_options.m_use_logical_timestamp_based_tm) {
        m_commit_unit = new commit_unit_logical(config, shader_config, sub_partition_id, m_cu_response_iface, m_request_tracker, m_rop );
    } else {
        m_commit_unit = new commit_unit(config, shader_config, sub_partition_id, m_cu_response_iface, m_request_tracker, m_rop );
    }

    m_num_total_req_queue = 2;
    m_current_req_queue = 0;
    m_last_req_queue = 0;
}

memory_sub_partition::~memory_sub_partition()
{
    delete m_icnt_L2_queue;
    delete m_L2_dram_queue;
    delete m_dram_L2_queue;
    delete m_L2_icnt_queue;
    delete m_L2cache;
    // delete m_L2interface;
    // delete m_mf_allocator; 

    // delete m_dram; 

    // delete m_cu_response_iface; 
    // delete m_commit_unit; 
}

void memory_sub_partition::cache_cycle( unsigned cycle )
{
    // L2 fill responses
    if( !m_config->m_L2_config.disabled()) {
       if ( m_L2cache->access_ready() && !m_L2_icnt_queue->full() ) {
           mem_fetch *mf = m_L2cache->next_access();
           if(mf->get_access_type() != L2_WR_ALLOC_R){ // Don't pass write allocate read request back to upper level cache
				mf->set_reply();
				mf->set_status(IN_PARTITION_L2_TO_ICNT_QUEUE,gpu_sim_cycle+gpu_tot_sim_cycle);
				m_L2_icnt_queue->push(mf);
           }else{
				m_request_tracker.erase(mf);
				delete mf;
           }
       }
    }

    // DRAM to L2 (texture) and icnt (not texture)
    if ( !m_dram_L2_queue->empty() ) {
        mem_fetch *mf = m_dram_L2_queue->top();
        if ( !m_config->m_L2_config.disabled() && m_L2cache->waiting_for_fill(mf) ) {
            if (m_L2cache->fill_port_free()) {
                mf->set_status(IN_PARTITION_L2_FILL_QUEUE,gpu_sim_cycle+gpu_tot_sim_cycle);
                m_L2cache->fill(mf,gpu_sim_cycle+gpu_tot_sim_cycle);
                m_dram_L2_queue->pop();
            }
        } else if ( !m_L2_icnt_queue->full() ) {
            mf->set_status(IN_PARTITION_L2_TO_ICNT_QUEUE,gpu_sim_cycle+gpu_tot_sim_cycle);
            m_L2_icnt_queue->push(mf);
            m_dram_L2_queue->pop();
        }
    }

    // prior L2 misses inserted into m_L2_dram_queue here
    if( !m_config->m_L2_config.disabled() )
       m_L2cache->cycle();

    m_commit_unit->cycle(cycle);

    // new L2 texture accesses and/or non-texture accesses
    if ( !m_L2_dram_queue->full() && !m_icnt_L2_queue->empty() ) {
        mem_fetch *mf = m_icnt_L2_queue->top();
        if ( !m_config->m_L2_config.disabled() &&
              ( (m_config->m_L2_texure_only && mf->istexture()) || (!m_config->m_L2_texure_only) )
           ) {
            // L2 is enabled and access is for L2
            bool output_full = m_L2_icnt_queue->full(); 
            bool port_free = m_L2cache->data_port_free(); 
            if ( !output_full && port_free ) {
                std::list<cache_event> events;
                enum cache_request_status status;
                bool write_sent;
                bool read_sent;

                enum cu_mem_op cu_op = m_commit_unit->check_cu_access(mf); 
                if (m_commit_unit->ideal_L2_cache(cu_op)) {
                    status = HIT;
                    write_sent = false; 
                    read_sent = false; 
                } else if (mf->is_logical_tm_req() and mf->get_is_write()) {
		    // TM-HACK: write need to go to L2 and grab resource, but data version management is lazy
		    // so write request has no data and don't need to write anything in memory
		    // write just need to check cuckoo table
		    // threrefore, we assume write always hit here  
		    status = HIT;
		    write_sent = false;
		    read_sent = false;
		} else {
                    status = m_L2cache->access(mf->get_addr(),mf,gpu_sim_cycle+gpu_tot_sim_cycle,events);
                    write_sent = was_write_sent(events);
                    read_sent = was_read_sent(events);
                }
                m_commit_unit->record_cache_hit(cu_op, status); 

                if ( status == HIT ) {
                    if( !write_sent ) {
                        // L2 cache replies
                        assert(!read_sent);
                        if( mf->get_access_type() == L1_WRBK_ACC ) {
                            m_request_tracker.erase(mf);
                            delete mf;
                        } else {
                            mf->set_reply();
                            mf->set_status(IN_PARTITION_L2_TO_ICNT_QUEUE,gpu_sim_cycle+gpu_tot_sim_cycle);
                            m_L2_icnt_queue->push(mf);
                        }
                        m_icnt_L2_queue->pop();
                    } else {
                        assert(write_sent);
                        m_icnt_L2_queue->pop();
                    }
                } else if ( status != RESERVATION_FAIL ) {
                    // L2 cache accepted request
                    m_icnt_L2_queue->pop();
                } else {
                    assert(!write_sent);
                    assert(!read_sent);
                    // L2 cache lock-up: will try again next cycle
                }
            }
        } else {
            // L2 is disabled or non-texture access to texture-only L2
            mf->set_status(IN_PARTITION_L2_TO_DRAM_QUEUE,gpu_sim_cycle+gpu_tot_sim_cycle);
            m_L2_dram_queue->push(mf);
            m_icnt_L2_queue->pop();
        }
    }

    // ROP delay queue
    if( !m_rop.empty() && (cycle >= m_rop.front().ready_cycle) && !m_icnt_L2_queue->full() ) {
        mem_fetch* mf = m_rop.front().req;
        m_rop.pop();
        m_icnt_L2_queue->push(mf);
        mf->set_status(IN_PARTITION_ICNT_TO_L2_QUEUE,gpu_sim_cycle+gpu_tot_sim_cycle);
    }

    g_tm_global_statistics.m_icnt_L2_queue_occupancy.add2bin(m_icnt_L2_queue->get_n_element());
    g_tm_global_statistics.m_L2_icnt_queue_occupancy.add2bin(m_L2_icnt_queue->get_n_element());
}

bool memory_sub_partition::full() const
{
    if (m_icnt_L2_queue->full()) {
	g_tm_global_statistics.m_tot_icnt_L2_queue_full++;
    }
    if (m_commit_unit->full()) {
	g_tm_global_statistics.m_tot_commit_unit_full++;
    }

    return (m_icnt_L2_queue->full() || m_commit_unit->full());
}

bool memory_sub_partition::L2_dram_queue_empty() const
{
   return m_L2_dram_queue->empty(); 
}

class mem_fetch* memory_sub_partition::L2_dram_queue_top() const
{
   return m_L2_dram_queue->top(); 
}

void memory_sub_partition::L2_dram_queue_pop() 
{
   m_L2_dram_queue->pop(); 
}

bool memory_sub_partition::dram_L2_queue_full() const
{
   return m_dram_L2_queue->full(); 
}

void memory_sub_partition::dram_L2_queue_push( class mem_fetch* mf )
{
   m_dram_L2_queue->push(mf); 
}

void memory_sub_partition::print_cache_stat(unsigned &accesses, unsigned &misses) const
{
    FILE *fp = stdout;
    if( !m_config->m_L2_config.disabled() )
       m_L2cache->print(fp,accesses,misses);
}

void memory_sub_partition::print( FILE *fp ) const
{
    if ( !m_request_tracker.empty() ) {
        fprintf(fp,"Memory Sub Parition %u: pending memory requests:\n", m_id);
        for ( std::set<mem_fetch*>::const_iterator r=m_request_tracker.begin(); r != m_request_tracker.end(); ++r ) {
            mem_fetch *mf = *r;
            if ( mf )
                mf->print(fp);
            else
                fprintf(fp," <NULL mem_fetch?>\n");
        }
    }
    if (m_rop.size() > 0) {
       fprintf(fp,"ROP.size = %zd \n", m_rop.size()); 
       fprintf(fp,"ROP.front = %llu ", m_rop.front().ready_cycle); 
       m_rop.front().req->print(fp); 
       fprintf(fp,"ROP.back = %llu ", m_rop.back().ready_cycle); 
       m_rop.back().req->print(fp); 
    } else {
       fprintf(fp,"ROP empty\n"); 
    }
    if( !m_config->m_L2_config.disabled() )
       m_L2cache->display_state(fp);
    m_commit_unit->print_sanity_counters(fp); 
}

void memory_stats_t::visualizer_print( gzFile visualizer_file )
{
   // gzprintf(visualizer_file, "Ltwowritemiss: %d\n", L2_write_miss);
   // gzprintf(visualizer_file, "Ltwowritehit: %d\n",  L2_write_access-L2_write_miss);
   // gzprintf(visualizer_file, "Ltworeadmiss: %d\n", L2_read_miss);
   // gzprintf(visualizer_file, "Ltworeadhit: %d\n", L2_read_access-L2_read_miss);
   if (num_mfs)
      gzprintf(visualizer_file, "averagemflatency: %lld\n", mf_total_lat/num_mfs);
}

void gpgpu_sim::print_dram_stats(FILE *fout) const
{
	unsigned cmd=0;
	unsigned activity=0;
	unsigned nop=0;
	unsigned act=0;
	unsigned pre=0;
	unsigned rd=0;
	unsigned wr=0;
	unsigned req=0;
	unsigned tot_cmd=0;
	unsigned tot_nop=0;
	unsigned tot_act=0;
	unsigned tot_pre=0;
	unsigned tot_rd=0;
	unsigned tot_wr=0;
	unsigned tot_req=0;

	for (unsigned i=0;i<m_memory_config->m_n_mem;i++){
		m_memory_partition_unit[i]->set_dram_power_stats(cmd,activity,nop,act,pre,rd,wr,req);
		tot_cmd+=cmd;
		tot_nop+=nop;
		tot_act+=act;
		tot_pre+=pre;
		tot_rd+=rd;
		tot_wr+=wr;
		tot_req+=req;
	}
    fprintf(fout,"gpgpu_n_dram_reads = %d\n",tot_rd );
    fprintf(fout,"gpgpu_n_dram_writes = %d\n",tot_wr );
    fprintf(fout,"gpgpu_n_dram_activate = %d\n",tot_act );
    fprintf(fout,"gpgpu_n_dram_commands = %d\n",tot_cmd);
    fprintf(fout,"gpgpu_n_dram_noops = %d\n",tot_nop );
    fprintf(fout,"gpgpu_n_dram_precharges = %d\n",tot_pre );
    fprintf(fout,"gpgpu_n_dram_requests = %d\n",tot_req );
}

unsigned memory_sub_partition::flushL2() 
{ 
    if (!m_config->m_L2_config.disabled()) {
        m_L2cache->flush(); 
    }
    return 0; // L2 is read only in this version
}

bool memory_sub_partition::busy() const 
{
    return !m_request_tracker.empty() || m_commit_unit->get_busy();
}

void memory_sub_partition::push( mem_fetch* req, unsigned long long cycle ) 
{
    if (req) {
        m_request_tracker.insert(req);
        m_stats->memlatstat_icnt2mem_pop(req);
        if( req->istexture() ) {
            m_icnt_L2_queue->push(req);
            req->set_status(IN_PARTITION_ICNT_TO_L2_QUEUE,gpu_sim_cycle+gpu_tot_sim_cycle);
        } else {
            bool done = m_commit_unit->access(req, gpu_sim_cycle + gpu_tot_sim_cycle); 
            if( !done ) {
                rop_delay_t r;
                r.req = req;
                r.ready_cycle = cycle + m_config->rop_latency; 
                m_rop.push(r);
                req->set_status(IN_PARTITION_ROP_DELAY,gpu_sim_cycle+gpu_tot_sim_cycle);
            } else {
                m_request_tracker.erase(req);
            }
        }
    }
}

mem_fetch* memory_sub_partition::pop() 
{
    mem_fetch *mf;
    if (m_last_req_queue == 0) {
        mf = m_L2_icnt_queue->top();

	if (mf && mf->is_cuckoo_table_checked()) {   // Need more cycles to finish cuckoo table checking
	    assert(mf->get_tm_cuckoo_cycles() > 0); 
	    mf->dec_tm_cuckoo_cycles();
	} else if (mf && mf->is_logical_tm_req()) {  // Need to check cuckoo table
	    mf->do_logical_tm();
	    if (mf->is_cuckoo_table_checked()) {
	        assert(mf->get_tm_cuckoo_cycles() > 0);
	        mf->dec_tm_cuckoo_cycles();
	    } else {
	        assert(mf->get_tm_cuckoo_cycles() == 0);
	    }
	}
       
        if (mf) {	
	    if (mf->get_tm_cuckoo_cycles() == 0) {
	        if (mf->stalled()) {
	            addr_t stall_addr = mf->get_stall_addr();
	            addr_t stall_chunk_addr = logical_temporal_conflict_detector::get_singleton().get_chunk_address(stall_addr);
		    assert(tm_req_stall_queue::get_singleton().full(get_id(), stall_chunk_addr) == false);
		    tm_req_stall_queue::get_singleton().push(get_id(), stall_chunk_addr, std::make_pair(mf->get_mem_fetch_pts(), mf));
		    mf->clear_cuckoo_table_checked();
	            m_L2_icnt_queue->pop();
	            mf = NULL;
	        } else {
		    if (mf->is_cuckoo_table_checked() == true || mf->get_commit_unit_generated() == false) {
                        m_request_tracker.erase(mf);
	                m_L2_icnt_queue->pop();
		    }
	        }
	    } else { 
                mf = NULL;	
	    }
	}
	
	if (mf && mf->isatomic())
            mf->do_atomic();
        if (mf && mf->get_commit_unit_generated() && (mf->is_cuckoo_table_checked() == false)) { 
            bool snoop = m_commit_unit->snoop_mem_fetch_reply(mf);
            assert(snoop == true);
	    if (mf->is_cuckoo_table_checked()) {
	        logical_temporal_conflict_detector::get_singleton().num_tm_cuckoo_cycles(mf);
	        assert(mf->get_tm_cuckoo_cycles() > 0); 
	        mf->dec_tm_cuckoo_cycles();
	    }
	    if (mf->get_tm_cuckoo_cycles() == 0) {
                m_request_tracker.erase(mf);
	        m_L2_icnt_queue->pop();
	    }
        }
        
	if( mf ) {
            if (mf->get_access_type() == L2_WRBK_ACC || mf->get_access_type() == L1_WRBK_ACC) {
                delete mf;
                mf = NULL;
            } else if (mf->get_commit_unit_generated()) {
		if (mf->get_tm_cuckoo_cycles() == 0) {
                    delete mf; 
                    mf = NULL; 
		} else {
		    mf = NULL;
		}
            }
        } 
    } else if (m_last_req_queue == 1) {
	mf = tm_req_stall_queue::get_singleton().get_next_mf(get_id());
	assert(mf);
	assert(mf->is_logical_tm_req());
	
	if (mf->is_cuckoo_table_checked()) {
	    assert(mf->get_tm_cuckoo_cycles() > 0);
	    mf->dec_tm_cuckoo_cycles();
	} else {
	    assert(mf->get_tm_cuckoo_cycles() == 0);
	    mf->do_logical_tm();
	    if (mf->is_cuckoo_table_checked()) {
	        assert(mf->get_tm_cuckoo_cycles() > 0);
	        mf->dec_tm_cuckoo_cycles();
	    }
	}

        if (mf->get_tm_cuckoo_cycles() == 0) {
	    if (mf->stalled()) {
	        addr_t stall_addr = mf->get_stall_addr();
	        addr_t stall_chunk_addr = logical_temporal_conflict_detector::get_singleton().get_chunk_address(stall_addr);
		assert(tm_req_stall_queue::get_singleton().full(get_id(), stall_chunk_addr) == false);
		tm_req_stall_queue::get_singleton().push(get_id(), stall_chunk_addr, std::make_pair(mf->get_mem_fetch_pts(), mf));
	        mf->clear_cuckoo_table_checked();
	        mf = NULL;
	    } else {
                m_request_tracker.erase(mf);
                
                if (g_tm_options.m_use_logical_timestamp_based_tm) {
                    assert(mf->is_stall_cycle_set());
                    g_tm_global_statistics.m_n_tm_req_stall_cycles.add2bin(gpu_sim_cycle + gpu_tot_sim_cycle - mf->get_stall_cycle());
                }	
	    }
	    tm_req_stall_queue::get_singleton().clear_pending_req_at_outport(get_id());
	} else {
	    mf = NULL;
	}	
    } else {
        printf("Invalid L2 request queue %d.\n", m_last_req_queue);
	abort();
    }
    
    if (mf != NULL) 
        n_mem_to_simt_at_pop += mf->get_num_flits(false); 
 
    return mf;
}

mem_fetch* memory_sub_partition::top(bool &internal) 
{  
    mem_fetch *mf = NULL;
    if (g_tm_options.m_use_logical_timestamp_based_tm) {
        mem_fetch *mf_stalled = tm_req_stall_queue::get_singleton().get_next_mf(get_id());
        mem_fetch *mf_non_stalled = m_L2_icnt_queue->top();

	if (mf_stalled) {
	    if (mf_stalled->is_cuckoo_table_checked()) {
	        assert(mf_stalled->get_tm_cuckoo_cycles() > 0);
		if (mf_non_stalled) {
		    assert(mf_non_stalled->is_cuckoo_table_checked() == false);
		    assert(mf_non_stalled->get_tm_cuckoo_cycles() == 0);
		}
		mf = mf_stalled;
		m_last_req_queue = 1;
	    } else if (mf_non_stalled && mf_non_stalled->is_cuckoo_table_checked()) {
		assert(mf_non_stalled->get_tm_cuckoo_cycles() > 0);
		assert(mf_stalled->is_cuckoo_table_checked() == false);
		assert(mf_stalled->get_tm_cuckoo_cycles() == 0);
		mf = mf_non_stalled;
		m_last_req_queue = 0;
	    } else {
	        mf = mf_stalled;
		m_last_req_queue = 1;
	    }
	} else {
	    mf = mf_non_stalled;
	    m_last_req_queue = 0; 
	}
    } else {
	mf = m_L2_icnt_queue->top();
	m_last_req_queue = 0;
    }

    internal = false; 

    if( mf ) { 
        // return NULL for mem_fetch that are internal to partition, will by processed @ pop()
        if (mf->get_access_type() == L2_WRBK_ACC || mf->get_access_type() == L1_WRBK_ACC) {
            // m_L2_icnt_queue->pop();
            // m_request_tracker.erase(mf);
            // delete mf;
            mf = NULL;
            internal = true; 
        } else if (mf->get_commit_unit_generated()) {
            // m_L2_icnt_queue->pop(); 
            // m_request_tracker.erase(mf);
            // delete mf;
            mf = NULL; 
            internal = true; 
        } 
    }
    
    return mf;
}

void memory_sub_partition::set_done( mem_fetch *mf )
{
    m_request_tracker.erase(mf);
}

void memory_sub_partition::accumulate_L2cache_stats(class cache_stats &l2_stats) const {
    if (!m_config->m_L2_config.disabled()) {
        l2_stats += m_L2cache->get_stats();
    }
}

void memory_sub_partition::get_L2cache_sub_stats(struct cache_sub_stats &css) const{
    if (!m_config->m_L2_config.disabled()) {
        m_L2cache->get_sub_stats(css);
    }
}

void memory_sub_partition::visualizer_print( gzFile visualizer_file )
{
    // TODO: Add visualizer stats for L2 cache 
   m_commit_unit->visualizer_print(visualizer_file); 
}

tm_req_stall_queue * tm_req_stall_queue::s_tm_req_stall_queue = NULL;

tm_req_stall_queue& tm_req_stall_queue::get_singleton() {
    if (s_tm_req_stall_queue == NULL)
	s_tm_req_stall_queue = new tm_req_stall_queue(g_tm_options.m_logical_timestamp_tm_stall_queue_size,
			                              g_tm_options.m_logical_timestamp_tm_stall_queue_entry_size);
    return *s_tm_req_stall_queue;
}

mem_fetch * tm_req_stall_queue::get_next_mf(unsigned queue_id) {
    if (m_pending_req_at_outports[queue_id] != NULL) return m_pending_req_at_outports[queue_id];
   
    std::map<addr_t, tm_stall_queue> m_stall_queue = m_stall_queues[queue_id]; 
    for (auto iter = m_stall_queue.begin(); iter != m_stall_queue.end(); iter++) {
        addr_t stall_chunk_addr = iter->first;
	mem_fetch *mf = top(queue_id, stall_chunk_addr);
	assert(mf != NULL);
	addr_t stall_addr = mf->get_stall_addr();
	assert(stall_chunk_addr == logical_temporal_conflict_detector::get_singleton().get_chunk_address(stall_addr));

	// Check whether the number of writing of the stalled address is 0
        unsigned num_writing_threads = logical_temporal_conflict_detector::get_singleton().get_num_writing_threads(stall_addr);

        // Check whether is_owner
	unsigned sid = mf->get_sid();
	unsigned wid = mf->get_wid();
	unsigned long long tx_pts = mf->get_mem_fetch_pts();
        bool is_owner = logical_temporal_conflict_detector::get_singleton().check_owner(stall_addr, tx_pts, sid, wid);
        
	// Check whether this request will be aborted
	unsigned long long stall_addr_wts = logical_temporal_conflict_detector::get_singleton().get_wts(stall_addr);
        unsigned long long stall_addr_rts = logical_temporal_conflict_detector::get_singleton().get_rts(stall_addr);
	bool is_write = mf->is_write();
	bool will_abort = false;
	if (is_write) {
	    if (tx_pts < std::max(stall_addr_wts, stall_addr_rts))
		will_abort = true;
	} else {
	    if (tx_pts < stall_addr_wts)
		will_abort = true;
	}

	if (num_writing_threads == 0 || is_owner || will_abort) {
            m_pending_req_at_outports[queue_id] = top(queue_id, stall_chunk_addr);
	    pop(queue_id, stall_chunk_addr);
	    break;
        }
    }
    return m_pending_req_at_outports[queue_id];
}
