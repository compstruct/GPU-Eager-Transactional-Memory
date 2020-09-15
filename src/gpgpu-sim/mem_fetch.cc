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

#include "mem_fetch.h"
#include "mem_latency_stat.h"
#include "shader.h"
#include "visualizer.h"
#include "gpu-sim.h"

unsigned mem_fetch::sm_next_mf_request_uid=1;

mem_fetch::mem_fetch( const mem_access_t &access, 
                      const warp_inst_t *inst,
                      unsigned ctrl_size, 
                      unsigned wid,
                      unsigned sid, 
                      unsigned tpc, 
                      const class memory_config *config )
{
   m_last_request_uid=m_request_uid;
   m_request_uid = sm_next_mf_request_uid++;
   m_access = access;
   if( inst ) { 
       m_inst = *inst;
       assert( wid == m_inst.warp_id() );
   }
   m_data_size = access.get_size();
   m_ctrl_size = ctrl_size;
   m_sid = sid;
   m_tpc = tpc;
   m_wid = wid;
   if( config )  {
       config->m_address_mapping.addrdec_tlx(access.get_addr(),&m_raw_addr);
       m_partition_addr = config->m_address_mapping.partition_address(access.get_addr());
   } else{
       m_partition_addr=-1;
   }
   m_type = m_access.is_write()?WRITE_REQUEST:READ_REQUEST;
   m_timestamp = gpu_sim_cycle + gpu_tot_sim_cycle;
   m_timestamp2 = 0;
   m_status = MEM_FETCH_INITIALIZED;
   m_status_change = gpu_sim_cycle + gpu_tot_sim_cycle;
   m_mem_config = config;
   icnt_flit_size = config->icnt_flit_size;

   m_transactional = m_inst.in_transaction; 
   m_transaction_id = 0;

   m_commit_unit_generated = false; 
   m_commit_id = -1; 
   m_coalesced_popped = false; 

   m_tm_manager = NULL;

   m_is_stalled = false;
   m_stall_at_addr = 0;
   m_stalled_uid = -1;
   m_is_aborted = false;
   mem_fetch_pts = 0;
   mem_fetch_stall_cycle_set = false;
   mem_fetch_stall_cycle = 0;
   m_cuckoo_check_byte_mask.reset();
   m_n_tm_cuckoo_cycles = 0;
   m_cuckoo_table_checked = false;

   m_early_abort_read = false;
   m_early_abort_inserted = false;

   extern tm_options g_tm_options;
   if (g_tm_options.m_use_logical_timestamp_based_tm and is_logical_tm_req()) {
       m_stalled_mask = get_access_warp_mask(); 
   } else {
       m_stalled_mask = active_mask_t((unsigned long long)0); 
   }
}

mem_fetch::~mem_fetch()
{
    m_status = MEM_FETCH_DELETED;
    m_stalled_mask.reset();
    assert(m_coalesced_packets.empty()); 
}

#define MF_TUP_BEGIN(X) static const char* Status_str[] = {
#define MF_TUP(X) #X
#define MF_TUP_END(X) };
#include "mem_fetch_status.tup"
#undef MF_TUP_BEGIN
#undef MF_TUP
#undef MF_TUP_END

void mem_fetch::print( FILE *fp, bool print_inst ) const
{
    if( this == NULL ) {
        fprintf(fp," <NULL mem_fetch pointer>\n");
        return;
    }
    fprintf(fp,"  mf: uid=%6u, sid%02u:w%02u, part=%u, ", m_request_uid, m_sid, m_wid, m_raw_addr.chip );
    m_access.print(fp);
    if( (unsigned)m_status < NUM_MEM_REQ_STAT ) 
       fprintf(fp," status = %s (%llu), ", Status_str[m_status], m_status_change );
    else
       fprintf(fp," status = %u??? (%llu), ", m_status, m_status_change );
    if( !m_inst.empty() && print_inst ) m_inst.print(fp);
    else fprintf(fp,"\n");
}

void mem_fetch::set_status( enum mem_fetch_status status, unsigned long long cycle ) 
{
    m_status = status;
    m_status_change = cycle;
}

bool mem_fetch::isatomic() const
{
   if( m_inst.empty() ) return false;
   return m_inst.isatomic();
}

void mem_fetch::do_atomic()
{
    m_inst.do_atomic( m_access.get_warp_mask() );
}

bool mem_fetch::is_logical_tm_req() const
{
   if( m_inst.empty() ) return false;
   return m_inst.is_logical_tm_req() and (m_access.get_type() == GLOBAL_ACC_W || m_access.get_type() == GLOBAL_ACC_R);
}

void mem_fetch::do_logical_tm()
{
    clear_cuckoo_check_byte_mask();
    clear_cuckoo_table_checked();
    set_tm_cuckoo_cycles(0);
    active_mask_t logical_tm_mask = m_access.get_warp_mask()&m_stalled_mask;
    m_inst.do_logical_tm( this, logical_tm_mask );

    if (is_cuckoo_table_checked())
        logical_temporal_conflict_detector::get_singleton().num_tm_cuckoo_cycles(this);
}

bool mem_fetch::istexture() const
{
    if( m_inst.empty() ) return false;
    return m_inst.space.get_type() == tex_space;
}

bool mem_fetch::isconst() const
{ 
    if( m_inst.empty() ) return false;
    return (m_inst.space.get_type() == const_space) || (m_inst.space.get_type() == param_space_kernel);
}

/// Returns number of flits traversing interconnect. simt_to_mem specifies the direction
unsigned mem_fetch::get_num_flits(bool simt_to_mem){
   unsigned sz=0;

   if (simt_to_mem) {
      if (get_access_type() == TX_MSG) {
         // message for KiloTM 
         sz = size(); 
      } else if (isatomic()) { 
         // atomic operation 
         sz = size(); 
      } else if (get_is_write()) { 
         // store operation 
         sz = size(); 
      } else { 
         // load operation 
         sz = get_ctrl_size(); 
      }
   } else { // traffic back to cores
      if (get_access_type() == TX_MSG) {
         // message for KiloTM 
         sz = size(); 
      } else if (isatomic()) { 
         // atomic operation
         sz = size(); 
      } else if (get_is_write()) { 
         // store operation 
         sz = get_ctrl_size(); 
      } else { 
         // load operation 
         sz = size(); 
      }
   }

   return (sz/icnt_flit_size) + ( (sz % icnt_flit_size)? 1:0);
}




bool mem_fetch::has_coalesced_packet() const
{
   return (not m_coalesced_packets.empty()); 
}

void mem_fetch::append_coalesced_packet(mem_fetch *mf)
{
   m_coalesced_packets.push_back(mf); 
}

mem_fetch* mem_fetch::next_coalesced_packet()
{
   return m_coalesced_packets.front(); 
}

void mem_fetch::pop_coalesced_packet() 
{
   m_coalesced_packets.pop_front(); 
   m_coalesced_popped = true; 
}

bool mem_fetch::partial_processed_packet() const 
{
   return m_coalesced_popped; 
}

// retrieve the list of coalesced packet for batch processing 
std::list<mem_fetch*>& mem_fetch::get_coalesced_packet_list() 
{
   return m_coalesced_packets; 
}
