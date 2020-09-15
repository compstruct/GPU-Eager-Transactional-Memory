// Copyright (c) 2009-2011, Tor M. Aamodt, Inderpreet Singh, Timothy Rogers,
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



#include "abstract_hardware_model.h"
#include "cuda-sim/memory.h"
#include "cuda-sim/ptx_ir.h"
#include "cuda-sim/ptx-stats.h"
#include "cuda-sim/cuda-sim.h"
#include "gpgpu-sim/gpu-sim.h"
#include "gpgpu-sim/shader.h"
#include "option_parser.h"
#include <algorithm>

unsigned mem_access_t::sm_next_access_uid = 0;   
unsigned warp_inst_t::sm_next_uid = 0;

void move_warp( warp_inst_t *&dst, warp_inst_t *&src )
{
   assert( dst->empty() );
   warp_inst_t* temp = dst;
   dst = src;
   src = temp;
   src->clear();
}


void gpgpu_functional_sim_config::reg_options(class OptionParser * opp)
{
	option_parser_register(opp, "-gpgpu_ptx_use_cuobjdump", OPT_BOOL,
                 &m_ptx_use_cuobjdump,
                 "Use cuobjdump to extract ptx and sass from binaries",
#if (CUDART_VERSION >= 4000)
                 "1"
#else
                 "0"
#endif
                 );
	option_parser_register(opp, "-gpgpu_experimental_lib_support", OPT_BOOL,
	                 &m_experimental_lib_support,
	                 "Try to extract code from cuda libraries [Broken because of unknown cudaGetExportTable]",
	                 "0");
    option_parser_register(opp, "-gpgpu_ptx_convert_to_ptxplus", OPT_BOOL,
                 &m_ptx_convert_to_ptxplus,
                 "Convert SASS (native ISA) to ptxplus and run ptxplus",
                 "0");
    option_parser_register(opp, "-gpgpu_ptx_force_max_capability", OPT_UINT32,
                 &m_ptx_force_max_capability,
                 "Force maximum compute capability",
                 "0");
   option_parser_register(opp, "-gpgpu_ptx_inst_debug_to_file", OPT_BOOL, 
                &g_ptx_inst_debug_to_file, 
                "Dump executed instructions' debug information to file", 
                "0");
   option_parser_register(opp, "-gpgpu_ptx_inst_debug_file", OPT_CSTR, &g_ptx_inst_debug_file, 
                  "Executed instructions' debug output file",
                  "inst_debug.txt");
   option_parser_register(opp, "-gpgpu_ptx_inst_debug_thread_uid", OPT_INT32, &g_ptx_inst_debug_thread_uid, 
               "Thread UID for executed instructions' debug output", 
               "1");
}

void gpgpu_functional_sim_config::ptx_set_tex_cache_linesize(unsigned linesize)
{
   m_texcache_linesize = linesize;
}

gpgpu_t::gpgpu_t( const gpgpu_functional_sim_config &config )
    : m_function_model_config(config)
{
   m_global_mem = new memory_space_impl<8192>("global",64*1024);
   m_tex_mem = new memory_space_impl<8192>("tex",64*1024);
   m_surf_mem = new memory_space_impl<8192>("surf",64*1024);

   m_dev_malloc=GLOBAL_HEAP_START; 

   if(m_function_model_config.get_ptx_inst_debug_to_file() != 0) 
      ptx_inst_debug_file = fopen(m_function_model_config.get_ptx_inst_debug_file(), "w");
}

address_type line_size_based_tag_func(new_addr_type address, new_addr_type line_size)
{
   //gives the tag for an address based on a given line size
   return address & ~(line_size-1);
}

const char * mem_access_type_str(enum mem_access_type access_type)
{
   #define MA_TUP_BEGIN(X) static const char* access_type_str[] = {
   #define MA_TUP(X) #X
   #define MA_TUP_END(X) };
   MEM_ACCESS_TYPE_TUP_DEF
   #undef MA_TUP_BEGIN
   #undef MA_TUP
   #undef MA_TUP_END

   assert(access_type < NUM_MEM_ACCESS_TYPE); 

   return access_type_str[access_type]; 
}

void warp_inst_t::check_active_mask() const {
   // if a warp instruction active mask cleared due to effects other than predication
   if (m_warp_active_mask.any() == false and m_warp_predicate_mask.any() == false) {
      if (g_debug_execution >= 4)
         printf("[GPGPU-Sim] Active mask of the instruction from warp %d cleared\n", m_warp_id); 
      // assert(0); 
   }
}


void warp_inst_t::clear_active( const active_mask_t &inactive ) {
    active_mask_t test = m_warp_active_mask;
    test &= inactive;
    assert( test == inactive ); // verify threads being disabled were active
    m_warp_active_mask &= ~inactive;
    check_active_mask(); 
}

void warp_inst_t::predicate_off( unsigned lane_id ) {
    assert(m_warp_predicate_mask.test(lane_id) == false); 
    m_warp_predicate_mask.set(lane_id); 
    set_not_active(lane_id); 
}

void warp_inst_t::set_not_active( unsigned lane_id ) {
    m_warp_active_mask.reset(lane_id);
    std::list<mem_access_t>::iterator iAcc; 
    for (iAcc = m_accessq.begin(); iAcc != m_accessq.end(); ++iAcc) {
        iAcc->reset_lane(lane_id);
    }
    check_active_mask(); 
}

void warp_inst_t::set_active( const active_mask_t &active ) {
   m_warp_active_mask = active;
   if( m_isatomic ) {
      for( unsigned i=0; i < m_config->warp_size; i++ ) {
         if( !m_warp_active_mask.test(i) ) {
             m_per_scalar_thread[i].callback.function = NULL;
             m_per_scalar_thread[i].callback.instruction = NULL;
             m_per_scalar_thread[i].callback.thread = NULL;
         }
      }
   }
   if( m_is_logical_tm_req ) {
      for( unsigned i=0; i < m_config->warp_size; i++ ) {
         if( !m_warp_active_mask.test(i) ) {
             m_per_scalar_thread[i].logical_tm_callback.function = NULL;
             m_per_scalar_thread[i].logical_tm_callback.instruction = NULL;
             m_per_scalar_thread[i].logical_tm_callback.thread = NULL;
             m_per_scalar_thread[i].logical_tm_callback.mem = NULL;
             m_per_scalar_thread[i].logical_tm_callback.space = memory_space_t();
         }
      }
   }
   check_active_mask(); 
}

void warp_inst_t::do_atomic(bool forceDo) {
    do_atomic( m_warp_active_mask,forceDo );
}

void warp_inst_t::do_atomic( const active_mask_t& access_mask,bool forceDo ) {
    assert( m_isatomic && (!m_empty||forceDo) );
    for( unsigned i=0; i < m_config->warp_size; i++ )
    {
        if( access_mask.test(i) )
        {
            dram_callback_t &cb = m_per_scalar_thread[i].callback;
            if( cb.thread )
                cb.function(cb.instruction, cb.thread);
        }
    }
}

void warp_inst_t::do_logical_tm(mem_fetch *mf, bool forceDo) {
    do_logical_tm( mf, m_warp_active_mask,forceDo );
}

void warp_inst_t::do_logical_tm( mem_fetch *mf, const active_mask_t& access_mask,bool forceDo ) {
    assert( m_is_logical_tm_req && (!m_empty||forceDo) );
    for( unsigned i=0; i < m_config->warp_size; i++ )
    {
        if( access_mask.test(i) )
        {
            logical_tm_dram_callback_t &logical_tm_cb = m_per_scalar_thread[i].logical_tm_callback;
	    if( logical_tm_cb.thread ) {
                assert(logical_tm_cb.function != NULL);
	        assert(logical_tm_cb.instruction != NULL);
		ptx_reg_t data;
                
                logical_tm_cb.function( logical_tm_cb.instruction, logical_tm_cb.thread, 
				        logical_tm_cb.addr, logical_tm_cb.size, logical_tm_cb.vector_spec, logical_tm_cb.type,
				        logical_tm_cb.instruction->dst(), logical_tm_cb.instruction->src1(), data, 
				        logical_tm_cb.mem, logical_tm_cb.space,	mf);
                
	    }
        }
    }
}

// print out all accesses generated by this warp instruction 
void warp_inst_t::dump_access( FILE *fout ) const
{
   for (std::list<mem_access_t>::const_iterator i_access = m_accessq.begin(); i_access != m_accessq.end(); i_access++) {
      i_access->print(fout); 
      fprintf(fout, "\n");
   }
}

void warp_inst_t::generate_mem_accesses(unsigned shader_id, tm_warp_info& warp_info)
{
    if( empty() || op == MEMORY_BARRIER_OP || m_mem_accesses_created ) 
        return;
    if ( !((op == LOAD_OP) || (op == STORE_OP)) )
        return; 
    if( m_warp_active_mask.count() == 0 ) 
        return; // predicated off

    const size_t starting_queue_size = m_accessq.size();

    assert( is_load() || is_store() );
    assert( m_per_scalar_thread_valid ); // need address information per thread

    bool is_write = is_store();

    mem_access_type access_type;
    switch (space.get_type()) {
    case const_space:
    case param_space_kernel: 
        access_type = CONST_ACC_R; 
        break;
    case tex_space: 
        access_type = TEXTURE_ACC_R;   
        break;
    case global_space:       
        access_type = is_write? GLOBAL_ACC_W: GLOBAL_ACC_R;   
        break;
    case local_space:
    case param_space_local:  
        access_type = is_write? LOCAL_ACC_W: LOCAL_ACC_R;   
        break;
    case shared_space: break;
    default: assert(0); break; 
    }

    if (in_transaction) {
        // printf("TM Access Info: %s %d %d\n", m_tm_access_info.m_writelog_access.to_string().c_str(), 
        //        m_tm_access_info.m_conflict_detect, m_tm_access_info.m_version_managed);
    }

    // Calculate memory accesses generated by this warp
    new_addr_type cache_block_size = 0; // in bytes 

    switch( space.get_type() ) {
    case shared_space: {
        unsigned subwarp_size = m_config->warp_size / m_config->mem_warp_parts;
        unsigned total_accesses=0;
        for( unsigned subwarp=0; subwarp <  m_config->mem_warp_parts; subwarp++ ) {

            // data structures used per part warp 
            std::map<unsigned,std::map<new_addr_type,unsigned> > bank_accs; // bank -> word address -> access count

            // step 1: compute accesses to words in banks
            for( unsigned thread=subwarp*subwarp_size; thread < (subwarp+1)*subwarp_size; thread++ ) {
                if( !active(thread) ) 
                    continue;
                new_addr_type addr = m_per_scalar_thread[thread].memreqaddr[0];
                //FIXME: deferred allocation of shared memory should not accumulate across kernel launches
                //assert( addr < m_config->gpgpu_shmem_size ); 
                unsigned bank = m_config->shmem_bank_func(addr);
                new_addr_type word = line_size_based_tag_func(addr,m_config->WORD_SIZE);
                bank_accs[bank][word]++;
            }

            if (m_config->shmem_limited_broadcast) {
                // step 2: look for and select a broadcast bank/word if one occurs
                bool broadcast_detected = false;
                new_addr_type broadcast_word=(new_addr_type)-1;
                unsigned broadcast_bank=(unsigned)-1;
                std::map<unsigned,std::map<new_addr_type,unsigned> >::iterator b;
                for( b=bank_accs.begin(); b != bank_accs.end(); b++ ) {
                    unsigned bank = b->first;
                    std::map<new_addr_type,unsigned> &access_set = b->second;
                    std::map<new_addr_type,unsigned>::iterator w;
                    for( w=access_set.begin(); w != access_set.end(); ++w ) {
                        if( w->second > 1 ) {
                            // found a broadcast
                            broadcast_detected=true;
                            broadcast_bank=bank;
                            broadcast_word=w->first;
                            break;
                        }
                    }
                    if( broadcast_detected ) 
                        break;
                }
            
                // step 3: figure out max bank accesses performed, taking account of broadcast case
                unsigned max_bank_accesses=0;
                for( b=bank_accs.begin(); b != bank_accs.end(); b++ ) {
                    unsigned bank_accesses=0;
                    std::map<new_addr_type,unsigned> &access_set = b->second;
                    std::map<new_addr_type,unsigned>::iterator w;
                    for( w=access_set.begin(); w != access_set.end(); ++w ) 
                        bank_accesses += w->second;
                    if( broadcast_detected && broadcast_bank == b->first ) {
                        for( w=access_set.begin(); w != access_set.end(); ++w ) {
                            if( w->first == broadcast_word ) {
                                unsigned n = w->second;
                                assert(n > 1); // or this wasn't a broadcast
                                assert(bank_accesses >= (n-1));
                                bank_accesses -= (n-1);
                                break;
                            }
                        }
                    }
                    if( bank_accesses > max_bank_accesses ) 
                        max_bank_accesses = bank_accesses;
                }

                // step 4: accumulate
                total_accesses+= max_bank_accesses;
            } else {
                // step 2: look for the bank with the maximum number of access to different words 
                unsigned max_bank_accesses=0;
                std::map<unsigned,std::map<new_addr_type,unsigned> >::iterator b;
                for( b=bank_accs.begin(); b != bank_accs.end(); b++ ) {
                    max_bank_accesses = std::max(max_bank_accesses, (unsigned)b->second.size());
                }

                // step 3: accumulate
                total_accesses+= max_bank_accesses;
            }
        }
        assert( total_accesses > 0 && total_accesses <= m_config->warp_size );
        cycles = total_accesses; // shared memory conflicts modeled as larger initiation interval 
        ptx_file_line_stats_add_smem_bank_conflict( pc, total_accesses );
        break;
    }

    case tex_space: 
        cache_block_size = m_config->gpgpu_cache_texl1_linesize;
        break;
    case const_space:  case param_space_kernel:
        cache_block_size = m_config->gpgpu_cache_constl1_linesize; 
        break;

    case global_space: case local_space: case param_space_local:
        if( space.get_type() == global_space and in_transaction and not m_config->no_tx_log_gen) {
            // detect cases where the access can be treated as normal, otherwise process it as transactional 
            bool generate_normal_accesses = false;
            if (is_write) {
                if (m_tm_access_info.m_version_managed) {
                    generate_access_tm(shader_id, is_write, access_type, warp_info); 
                } else {
                    generate_normal_accesses = true; 
                }
            } else {
                if (m_tm_access_info.m_conflict_detect) {
                    generate_access_tm(shader_id, is_write, access_type, warp_info); 
                } else {
                    generate_normal_accesses = true; 
                }
            }
            if (generate_normal_accesses) {
                if(isatomic())
                    memory_coalescing_arch_13_atomic(is_write, access_type);
                else
                    memory_coalescing_arch_13(is_write, access_type);
            }
        } else if( m_config->gpgpu_coalesce_arch == 13 ) {
           if(isatomic())
               memory_coalescing_arch_13_atomic(is_write, access_type);
           else
               memory_coalescing_arch_13(is_write, access_type);
        } else abort();
        break;

    default:
        abort();
    }

    if( cache_block_size ) {
        assert( m_accessq.empty() );
        mem_access_byte_mask_t byte_mask; 
        std::map<new_addr_type,active_mask_t> accesses; // block address -> set of thread offsets in warp
        std::map<new_addr_type,active_mask_t>::iterator a;
        for( unsigned thread=0; thread < m_config->warp_size; thread++ ) {
            if( !active(thread) ) 
                continue;
            new_addr_type addr = m_per_scalar_thread[thread].memreqaddr[0];
            unsigned block_address = line_size_based_tag_func(addr,cache_block_size);
            accesses[block_address].set(thread);
            unsigned idx = addr-block_address; 
            for( unsigned i=0; i < data_size; i++ ) 
                byte_mask.set(idx+i);
        }
        for( a=accesses.begin(); a != accesses.end(); ++a ) 
            m_accessq.push_back( mem_access_t(access_type,a->first,cache_block_size,is_write,a->second,byte_mask) );
    }

    if ( space.get_type() == global_space ) {
        ptx_file_line_stats_add_uncoalesced_gmem( pc, m_accessq.size() - starting_queue_size );
    }
    m_mem_accesses_created=true;
}

#include "gpgpu-sim/shader.h"

unsigned g_debug_tm_write_log_entry_written = 0;

void tm_warp_info::print_log(unsigned int lane_id, FILE *fout) 
{
   for (unsigned a = 0; a < m_read_log.size(); a++) {
      fprintf(fout, "R[%2u] = [%#08x]%c ", a, m_read_log[a].m_addr[lane_id], ((m_read_log[a].m_raw.test(a))? 'L':' '));
   }
   for (unsigned a = 0; a < m_write_log.size(); a++) {
      fprintf(fout, "W[%2u] = [%#08x]%c ", a, m_write_log[a].m_addr[lane_id], ((m_write_log[a].m_raw.test(a))? 'L':' '));
   }
   fprintf(fout, "\n");
}

void tm_warp_info::reset_lane(unsigned int lane_id)
{
   for (unsigned a = 0; a < m_read_log.size(); a++) {
      m_read_log[a].m_addr[lane_id] = 0;
      m_read_log[a].m_raw.reset(lane_id); 
      m_read_log[a].m_active.reset(lane_id); 
   }

   for (unsigned a = 0; a < m_write_log.size(); a++) {
      m_write_log[a].m_addr[lane_id] = 0;
      m_write_log[a].m_raw.reset(lane_id); 
      m_write_log[a].m_active.reset(lane_id); 
   }
}

// return the number of access generated for filling the transaction read log 
unsigned warp_inst_t::n_txlog_fill() const 
{
    if (not in_transaction) 
        assert (m_txlog_fill_accesses == 0); 
    return m_txlog_fill_accesses; 
}

// add a entry to write/read log visible to uarch model 
void warp_inst_t::append_log(unsigned shader_id, tm_warp_info::tx_log_t &log, addr_t offset, unsigned warp_size, std::bitset<32> &raw_access, unsigned log_size)
{
   extern tm_options g_tm_options;
   if (g_tm_options.m_pause_and_go_enabled) {
       if (log.size() > log_size) {
	   tm_warp_info::tx_acc_entry_t *tx_entry = &(log[log_size]);
	   for (unsigned t = 0; t < warp_size; t++) {
	       if (active(t)) {
	           (tx_entry->m_addr)[t] = m_per_scalar_thread[t].memreqaddr[0] + offset;
	       } else {
	           (tx_entry->m_addr)[t] = 0;
	       }
	       tx_entry->m_raw = raw_access;
	       tx_entry->m_active = m_warp_active_mask;
	   }
           return;
       }
   }

   tm_warp_info::tx_acc_entry_t tx_entry(warp_size); 
   for (unsigned t = 0; t < warp_size; t++) {
      if (active(t)) {
         tx_entry.m_addr[t] = m_per_scalar_thread[t].memreqaddr[0] + offset; // Assuming 4B chunk for access log
      } else {
         tx_entry.m_addr[t] = 0;
      }
   }
   tx_entry.m_raw = raw_access; // save the writelog access vector
   tx_entry.m_active = m_warp_active_mask; 
   log.push_back(tx_entry); 
}

const addr_t tm_warp_info::write_log_offset = 8192; // WF: Is this large enough?
const addr_t tm_warp_info::read_log_offset = 4096; 

void warp_inst_t::generate_access_tm( unsigned shader_id, bool is_write, mem_access_type access_type, tm_warp_info &warp_info )
{
   const unsigned addr_size = 4; 
   const unsigned word_size = 4; 
   const active_mask_t empty_mask;
   mem_access_byte_mask_t full_byte_mask; 
   full_byte_mask.set(); 
   unsigned access_data_size = data_size * ldst_vector_elm; 
   m_txlog_fill_accesses = 0;
   if ( is_write ) {
      assert(m_tm_access_info.m_version_managed == true);
   
      //In logical timestamp based tm manager, generate normal mem request to update timestamp and number of writing threads
      extern tm_options g_tm_options;
      if (g_tm_options.m_use_logical_timestamp_based_tm) {
          if(isatomic())
              memory_coalescing_arch_13_atomic(is_write, access_type);
          else
              memory_coalescing_arch_13(is_write, access_type);
      }

      unsigned wtid = m_warp_id * m_config->warp_size; 
      unsigned atag_block_size = addr_size * m_config->warp_size; 
      unsigned data_block_size = word_size * m_config->warp_size; 
      for (unsigned w = 0; w * word_size < access_data_size; w++) {
         // for each word written, generate two stores <Address, Value> to local memory 
         unsigned int next_write_entry = warp_info.m_write_log_size++; 
         addr_t next_data_block = 
            warp_info.m_shader->translate_local_memaddr((next_write_entry*2+1) * word_size + tm_warp_info::write_log_offset,
                                                        wtid, word_size); 
         m_accessq.push_back( mem_access_t(LOCAL_ACC_W,next_data_block,data_block_size,true,m_warp_active_mask,full_byte_mask) );
         addr_t next_atag_block = 
            warp_info.m_shader->translate_local_memaddr(next_write_entry*2 * word_size + tm_warp_info::write_log_offset,
                                                        wtid, addr_size); 
         m_accessq.push_back( mem_access_t(LOCAL_ACC_W,next_atag_block,atag_block_size,true,empty_mask,full_byte_mask) );

         // printf("tm_store: wtlog=%d atag_block=%#08x data_block=%#08x m_accessqsize=%zd\n", next_write_entry, next_atag_block, next_data_block, m_accessq.size());
         g_debug_tm_write_log_entry_written += 2; 
         
         // add a entry to write log visible to uarch model 
         append_log(shader_id, warp_info.m_write_log, w * word_size, m_config->warp_size, m_tm_access_info.m_writelog_access, next_write_entry);
         #if 0
         if (warp_info.m_shader->get_sid() == 14 and m_warp_id == 4) {
            printf("activemask = %08x\n", m_warp_active_mask.to_ulong()); 
            printf("timeout_validation_fail = %08x\n", m_tm_access_info.m_timeout_validation_fail.to_ulong()); 
            for (unsigned t = 0; t < m_config->warp_size; t++) {
               printf("lane %u\n", t); 
               warp_info.print_log(t, stdout); 
            }
         }
         #endif
      }
   } else {
      assert(m_tm_access_info.m_conflict_detect == true);
     
      // detect RAW access and walk write log if needed, remove those RAW access from the memory coalescing
      active_mask_t original_active_mask = m_warp_active_mask; 
      if (m_tm_access_info.m_writelog_access.any()) {
         assert(warp_info.m_write_log_size > 0); 
      }
      
      extern tm_options g_tm_options;
      if (g_tm_options.m_use_logical_timestamp_based_tm) {
      // TM-HACK: I think this is an ugly hack
      // In logical TM, all global tm request will be processed in L2, so we generate global requst here
      // We cannot generate request like other tm, becasue writelog_access may mask off all warp_active_mask
      // Then no global tm request will be generated, benchmark will be crashed
          if(isatomic())
              memory_coalescing_arch_13_atomic(is_write, access_type);
          else
              memory_coalescing_arch_13(is_write, access_type);
      }

      m_warp_active_mask &= ~m_tm_access_info.m_writelog_access; // clear bit for any thread that have RAW hit

      #if 0
      if (warp_info.m_shader->get_sid() == 7 and m_warp_id == 6) {
         printf("access_data_size = %u\n", access_data_size); 
         for (unsigned t = 0; t < m_config->warp_size; t++) {
            if (active(t)) {
               printf("t%02u ", t); 
               for (unsigned x = 0; x < 4; x++) 
                  printf("[%#08x] ", (unsigned int)m_per_scalar_thread[t].memreqaddr[x]); 
               printf("\n"); 
            } else {
               printf("t%02u \n", t); 
            }
         }
      }
      #endif

      // generate the actual memory fetch
      if (g_tm_options.m_use_logical_timestamp_based_tm == false) {
          if(isatomic())
              memory_coalescing_arch_13_atomic(is_write, access_type);
          else
              memory_coalescing_arch_13(is_write, access_type);
      }

      // change the fill address (but not the fetch address) to read log in local memory 
      unsigned wtid = m_warp_id * m_config->warp_size; 
      unsigned int read_entry = warp_info.m_read_log_size; 
      addr_t data_block[4];  // each instruction may load up to 128-bits, writing back the data fields in up to 4 entries 
      for (unsigned w = 0; w * word_size < access_data_size; w++) {
         data_block[w] = 
            warp_info.m_shader->translate_local_memaddr(((read_entry+w)*2+1) * word_size + tm_warp_info::read_log_offset,
                                                        wtid, word_size); 
      }
      for (std::list<mem_access_t>::iterator iAcc = m_accessq.begin(); iAcc != m_accessq.end(); ++iAcc) {
         if (iAcc->get_type() == GLOBAL_ACC_R) {
            iAcc->set_tx_load(data_block[0]); // just set them all to write to data block 0
         }
      }

      unsigned atag_block_size = addr_size * m_config->warp_size; 
      unsigned data_block_size = word_size * m_config->warp_size;
      
      if(m_warp_active_mask.any()){
          // HACK: for data blocks in other entries, just create a store to make sure the entry is allocated in cache 
          for (unsigned w = 1; w * word_size < access_data_size; w++) {
             m_accessq.push_back( mem_access_t(LOCAL_ACC_W,data_block[w],data_block_size,true,empty_mask,full_byte_mask) );
             m_txlog_fill_accesses += 1; // tell the ldst_unit to not treat these accesses as pending writebacks 
             // printf("tm_load: rdlog=%d data_block=%#08x m_accessqsize=%zd\n", read_entry + w, data_block[w], m_accessq.size());
          }

          for (unsigned w = 0; w * word_size < access_data_size; w++) {
             // for each word read, write address tags to local memory
             unsigned int next_read_entry = warp_info.m_read_log_size++;
             addr_t next_atag_block =
                warp_info.m_shader->translate_local_memaddr(next_read_entry*2 * word_size + tm_warp_info::read_log_offset,
                                                            wtid, addr_size);
             m_accessq.push_back( mem_access_t(LOCAL_ACC_W,next_atag_block,atag_block_size,true,empty_mask,full_byte_mask) );
             m_txlog_fill_accesses += 1; // tell the ldst_unit to not treat these accesses as pending writebacks 

             // printf("tm_load: rdlog=%d atag_block=%#08x data_block=%#08x m_accessqsize=%zd\n", next_read_entry, next_atag_block, next_data_block, m_accessq.size());

             // add a entry to read log visible to uarch model
             append_log(shader_id, warp_info.m_read_log, w * word_size, m_config->warp_size, m_tm_access_info.m_writelog_access, next_read_entry);
             #if 0
             if (warp_info.m_shader->get_sid() == 14 and m_warp_id == 4) {
                printf("activemask = %08x\n", m_warp_active_mask.to_ulong()); 
                printf("timeout_validation_fail = %08x\n", m_tm_access_info.m_timeout_validation_fail.to_ulong()); 
                for (unsigned t = 0; t < m_config->warp_size; t++) {
                   printf("lane %u\n", t); 
                   warp_info.print_log(t, stdout); 
                }
             }
             #endif
          }
      }
      
      if (m_tm_access_info.m_writelog_access.any()) {
         // generate write log walk in running order (the stack will reverse the order -- the intended order)
         // assuming worst case traversal through the whole log
         for (unsigned w = 0; w < warp_info.m_write_log_size; w++) {
            if (w == 0) {
               // only one word will hit -- generate a load <data> from local memory 
               addr_t next_data_block = 
                  warp_info.m_shader->translate_local_memaddr((w*2+1) * word_size + tm_warp_info::write_log_offset,
                                                              wtid, word_size); 
               m_accessq.push_back( mem_access_t(LOCAL_ACC_R,next_data_block,data_block_size,false,empty_mask,full_byte_mask) );
            }
            // for each write log entry, generate a load <Address> from local memory 
            addr_t next_atag_block = 
               warp_info.m_shader->translate_local_memaddr(w*2 * word_size + tm_warp_info::write_log_offset,
                                                           wtid, addr_size); 
            m_accessq.push_back( mem_access_t(LOCAL_ACC_R,next_atag_block,atag_block_size,false,empty_mask,full_byte_mask) );

            // printf("tm_load: wtlog=%d atag_block=%#08x data_block=%#08x m_accessqsize=%zd\n", next_write_entry, next_atag_block, next_data_block, m_accessq.size());
         }
      }

      m_warp_active_mask = original_active_mask; // restore active mask 
   }
}

void warp_inst_t::memory_coalescing_arch_13( bool is_write, mem_access_type access_type )
{
    // see the CUDA manual where it discusses coalescing rules before reading this
    unsigned segment_size = 0;
    unsigned warp_parts = m_config->mem_warp_parts;
    switch( data_size ) {
    case 1: segment_size = 32; break;
    case 2: segment_size = 64; break;
    case 4: case 8: case 16: segment_size = 128; break;
    }
    unsigned subwarp_size = m_config->warp_size / warp_parts;

    for( unsigned subwarp=0; subwarp <  warp_parts; subwarp++ ) {
        std::map<new_addr_type,transaction_info> subwarp_transactions;

        // step 1: find all transactions generated by this subwarp
        for( unsigned thread=subwarp*subwarp_size; thread<subwarp_size*(subwarp+1); thread++ ) {
            if( !active(thread) )
                continue;

            unsigned data_size_coales = data_size;
            unsigned num_accesses = 1;

            if( space.get_type() == local_space || space.get_type() == param_space_local ) {
               // Local memory accesses >4B were split into 4B chunks
               if(data_size >= 4) {
                  data_size_coales = 4;
                  num_accesses = data_size/4;
               }
               // Otherwise keep the same data_size for sub-4B access to local memory
            }


            assert(num_accesses <= MAX_ACCESSES_PER_INSN_PER_THREAD);

            for(unsigned access=0; access<num_accesses; access++) {
                new_addr_type addr = m_per_scalar_thread[thread].memreqaddr[access];
                unsigned block_address = line_size_based_tag_func(addr,segment_size);
                unsigned chunk = (addr&127)/32; // which 32-byte chunk within in a 128-byte chunk does this thread access?
                transaction_info &info = subwarp_transactions[block_address];

                // can only write to one segment
                assert(block_address == line_size_based_tag_func(addr+data_size_coales-1,segment_size));

                info.chunks.set(chunk);
                info.active.set(thread);
                unsigned idx = (addr&127);
                for( unsigned i=0; i < data_size_coales; i++ ) {
                    info.bytes.set(idx+i);
                    
		    // HACK for the LSU HPCA2016 Early Abort paper
		    info.word_active[((addr+i) >> 2) << 2].set(thread);
		}
            }
        }

        // step 2: reduce each transaction size, if possible
        std::map< new_addr_type, transaction_info >::iterator t;
        for( t=subwarp_transactions.begin(); t !=subwarp_transactions.end(); t++ ) {
            new_addr_type addr = t->first;
            const transaction_info &info = t->second;

            memory_coalescing_arch_13_reduce_and_send(is_write, access_type, info, addr, segment_size);

        }
    }
}

void warp_inst_t::memory_coalescing_arch_13_atomic( bool is_write, mem_access_type access_type )
{

   assert(space.get_type() == global_space); // Atomics allowed only for global memory

   // see the CUDA manual where it discusses coalescing rules before reading this
   unsigned segment_size = 0;
   unsigned warp_parts = 2;
   switch( data_size ) {
   case 1: segment_size = 32; break;
   case 2: segment_size = 64; break;
   case 4: case 8: case 16: segment_size = 128; break;
   }
   unsigned subwarp_size = m_config->warp_size / warp_parts;

   for( unsigned subwarp=0; subwarp <  warp_parts; subwarp++ ) {
       std::map<new_addr_type,std::list<transaction_info> > subwarp_transactions; // each block addr maps to a list of transactions

       // step 1: find all transactions generated by this subwarp
       for( unsigned thread=subwarp*subwarp_size; thread<subwarp_size*(subwarp+1); thread++ ) {
           if( !active(thread) )
               continue;

           new_addr_type addr = m_per_scalar_thread[thread].memreqaddr[0];
           unsigned block_address = line_size_based_tag_func(addr,segment_size);
           unsigned chunk = (addr&127)/32; // which 32-byte chunk within in a 128-byte chunk does this thread access?

           // can only write to one segment
           assert(block_address == line_size_based_tag_func(addr+data_size-1,segment_size));

           // Find a transaction that does not conflict with this thread's accesses
           bool new_transaction = true;
           std::list<transaction_info>::iterator it;
           transaction_info* info;
           for(it=subwarp_transactions[block_address].begin(); it!=subwarp_transactions[block_address].end(); it++) {
              unsigned idx = (addr&127);
              if(not it->test_bytes(idx,idx+data_size-1)) {
                 new_transaction = false;
                 info = &(*it);
                 break;
              }
           }
           if(new_transaction) {
              // Need a new transaction
              subwarp_transactions[block_address].push_back(transaction_info());
              info = &subwarp_transactions[block_address].back();
           }
           assert(info);

           info->chunks.set(chunk);
           info->active.set(thread);
           unsigned idx = (addr&127);
           for( unsigned i=0; i < data_size; i++ ) {
               assert(!info->bytes.test(idx+i));
               info->bytes.set(idx+i);
           }
       }

       // step 2: reduce each transaction size, if possible
       std::map< new_addr_type, std::list<transaction_info> >::iterator t_list;
       for( t_list=subwarp_transactions.begin(); t_list !=subwarp_transactions.end(); t_list++ ) {
           // For each block addr
           new_addr_type addr = t_list->first;
           const std::list<transaction_info>& transaction_list = t_list->second;

           std::list<transaction_info>::const_iterator t;
           for(t=transaction_list.begin(); t!=transaction_list.end(); t++) {
               // For each transaction
               const transaction_info &info = *t;
               memory_coalescing_arch_13_reduce_and_send(is_write, access_type, info, addr, segment_size);
           }
       }
   }
}

void warp_inst_t::memory_coalescing_arch_13_reduce_and_send( bool is_write, mem_access_type access_type, const transaction_info &info, new_addr_type addr, unsigned segment_size )
{
   assert( (addr & (segment_size-1)) == 0 );

   const std::bitset<4> &q = info.chunks;
   assert( q.count() >= 1 );
   std::bitset<2> h; // halves (used to check if 64 byte segment can be compressed into a single 32 byte segment)

   unsigned size=segment_size;
   if( segment_size == 128 ) {
       bool lower_half_used = q[0] || q[1];
       bool upper_half_used = q[2] || q[3];
       if( lower_half_used && !upper_half_used ) {
           // only lower 64 bytes used
           size = 64;
           if(q[0]) h.set(0);
           if(q[1]) h.set(1);
       } else if ( (!lower_half_used) && upper_half_used ) {
           // only upper 64 bytes used
           addr = addr+64;
           size = 64;
           if(q[2]) h.set(0);
           if(q[3]) h.set(1);
       } else {
           assert(lower_half_used && upper_half_used);
       }
   } else if( segment_size == 64 ) {
       // need to set halves
       if( (addr % 128) == 0 ) {
           if(q[0]) h.set(0);
           if(q[1]) h.set(1);
       } else {
           assert( (addr % 128) == 64 );
           if(q[2]) h.set(0);
           if(q[3]) h.set(1);
       }
   }
   if( size == 64 ) {
       bool lower_half_used = h[0];
       bool upper_half_used = h[1];
       if( lower_half_used && !upper_half_used ) {
           size = 32;
       } else if ( (!lower_half_used) && upper_half_used ) {
           addr = addr+32;
           size = 32;
       } else {
           assert(lower_half_used && upper_half_used);
       }
   }
   
   extern tm_options g_tm_options;
   if (g_tm_options.m_pause_and_go_enabled) {
       m_accessq.push_back( mem_access_t(access_type,addr,size,is_write,info.active,info.bytes, info.word_active) );
   } else {
       m_accessq.push_back( mem_access_t(access_type,addr,size,is_write,info.active,info.bytes) );
   }
}

void warp_inst_t::completed( unsigned long long cycle ) const 
{
   unsigned long long latency = cycle - issue_cycle; 
   assert(latency <= cycle); // underflow detection 
   ptx_file_line_stats_add_latency(pc, latency * active_count());  
}


unsigned kernel_info_t::m_next_uid = 1;

kernel_info_t::kernel_info_t( dim3 gridDim, dim3 blockDim, class function_info *entry )
{
    m_kernel_entry=entry;
    m_grid_dim=gridDim;
    m_block_dim=blockDim;
    m_next_cta.x=0;
    m_next_cta.y=0;
    m_next_cta.z=0;
    m_next_tid=m_next_cta;
    m_num_cores_running=0;
    m_uid = m_next_uid++;
    m_param_mem = new memory_space_impl<8192>("param",64*1024);
}

kernel_info_t::~kernel_info_t()
{
    assert( m_active_threads.empty() );
    delete m_param_mem;
}

std::string kernel_info_t::name() const
{
    return m_kernel_entry->get_name();
}

simt_stack::simt_stack(unsigned sid, unsigned wid, unsigned warpSize)
{
    m_sid = sid;
    m_warp_id=wid;
    m_warp_size = warpSize;
    m_is_tm_restarted = false;
    reset();
}

void simt_stack::reset()
{
    m_stack.clear();
    m_in_transaction = false;
    m_has_paused_threads = false; 
}

void simt_stack::launch( address_type start_pc, const simt_mask_t &active_mask )
{
    reset();
    simt_stack_entry new_stack_entry;
    new_stack_entry.m_pc = start_pc;
    new_stack_entry.m_calldepth = 1;
    new_stack_entry.m_active_mask = active_mask;
    new_stack_entry.m_type = STACK_ENTRY_TYPE_NORMAL;
    m_stack.push_back(new_stack_entry);
}

const simt_mask_t &simt_stack::get_active_mask() const
{
    assert(m_stack.size() > 0);
    return m_stack.back().m_active_mask;
}

const enum simt_stack::stack_entry_type &simt_stack::get_type() const
{
    assert(m_stack.size() > 0);
    return m_stack.back().m_type;
}

void simt_stack::get_pdom_stack_top_info( unsigned *pc, unsigned *rpc ) const
{
   assert(m_stack.size() > 0);
   *pc = m_stack.back().m_pc;
   *rpc = m_stack.back().m_recvg_pc;
}

unsigned simt_stack::get_rp() const 
{ 
    assert(m_stack.size() > 0);
    return m_stack.back().m_recvg_pc;
}

void simt_stack::print (FILE *fout) const
{
    for ( unsigned k=0; k < m_stack.size(); k++ ) {
        simt_stack_entry stack_entry = m_stack[k];
        if ( k==0 ) {
            fprintf(fout, "s%02dw%02d %1u ", m_sid, m_warp_id, k );
        } else {
            fprintf(fout, "       %1u ", k );
        }
        char c_type = '?'; 
        switch (stack_entry.m_type) {
            case STACK_ENTRY_TYPE_INVALID: c_type = 'I'; break; 
            case  STACK_ENTRY_TYPE_NORMAL: c_type = 'N'; break; 
            case   STACK_ENTRY_TYPE_RETRY: c_type = 'R'; break; 
            case   STACK_ENTRY_TYPE_TRANS: c_type = 'T'; break; 
            case   STACK_ENTRY_TYPE_PAUSE: c_type = 'P'; break; 
            case    STACK_ENTRY_TYPE_CALL: c_type = 'C'; break; 
            default: c_type = '?'; break; 
        };
        for (unsigned j=0; j<m_warp_size; j++)
            fprintf(fout, "%c", (stack_entry.m_active_mask.test(j)?'1':'0') );
        fprintf(fout, " pc: 0x%03x", stack_entry.m_pc );
        if ( stack_entry.m_recvg_pc == (unsigned)-1 ) {
            fprintf(fout," rp: ---- tp: %c cd: %2u rd_log: %4d wr_log: %4d ", c_type, stack_entry.m_calldepth, stack_entry.m_paused_read_log_entry, stack_entry.m_paused_write_log_entry );
        } else {
            fprintf(fout," rp: 0x%03x tp: %c cd: %2u rd_log: %4d wr_log: %4d ", stack_entry.m_recvg_pc, c_type, stack_entry.m_calldepth, stack_entry.m_paused_read_log_entry, stack_entry.m_paused_write_log_entry );
        }
        if ( stack_entry.m_branch_div_cycle != 0 ) {
            fprintf(fout," bd@%6u ", (unsigned) stack_entry.m_branch_div_cycle );
        } else {
            fprintf(fout," " );
        }
        ptx_print_insn( stack_entry.m_pc, fout );
        fprintf(fout,"\n");
    }
}

void simt_stack::update( simt_mask_t &thread_done, addr_vector_t &next_pc, address_type recvg_pc, op_type next_inst_op )
{
    assert(m_stack.size() > 0);

    assert( next_pc.size() == m_warp_size );

    simt_mask_t  top_active_mask = m_stack.back().m_active_mask;
    address_type top_recvg_pc = m_stack.back().m_recvg_pc;
    address_type top_pc = m_stack.back().m_pc; // the pc of the instruction just executed
    stack_entry_type top_type = m_stack.back().m_type;

    // pop all invalid entries from the stack (due to TX-abort of non-TOS threads)
    while (top_type == STACK_ENTRY_TYPE_INVALID || top_type == STACK_ENTRY_TYPE_PAUSE) {
        assert(m_stack.back().m_active_mask.none()); 
	if (top_type == STACK_ENTRY_TYPE_PAUSE) {
	    tm_warp_info &current_tm_warp_info = m_warp->get_tm_warp_info();
	    current_tm_warp_info.m_read_log_size = m_stack.back().get_paused_read_log_entry();
	    current_tm_warp_info.m_write_log_size = m_stack.back().get_paused_write_log_entry();
	}
        m_stack.pop_back(); 
        top_active_mask = m_stack.back().m_active_mask;
        top_recvg_pc = m_stack.back().m_recvg_pc;
        top_pc = m_stack.back().m_pc; 
        top_type = m_stack.back().m_type;
    }

    assert(top_active_mask.any());

    const address_type null_pc = -1;
    bool warp_diverged = false;
    address_type new_recvg_pc = null_pc;
    while (top_active_mask.any()) {

        // extract a group of threads with the same next PC among the active threads in the warp
        address_type tmp_next_pc = null_pc;
        simt_mask_t tmp_active_mask;
        for (int i = m_warp_size - 1; i >= 0; i--) {
            if ( top_active_mask.test(i) ) { // is this thread active?
                if (thread_done.test(i)) {
                    top_active_mask.reset(i); // remove completed thread from active mask
                } else if (tmp_next_pc == null_pc) {
                    tmp_next_pc = next_pc[i];
                    tmp_active_mask.set(i);
                    top_active_mask.reset(i);
                } else if (tmp_next_pc == next_pc[i]) {
                    tmp_active_mask.set(i);
                    top_active_mask.reset(i);
                }
            }
        }

        if(tmp_next_pc == null_pc) {
            assert(!top_active_mask.any()); // all threads done
            continue;
        }

        // HANDLE THE SPECIAL CASES FIRST
        if (next_inst_op == CALL_OPS)
        {
            // Since call is not a divergent instruction, all threads should have executed a call instruction
            assert(top_active_mask.any() == false);

            simt_stack_entry new_stack_entry;
            new_stack_entry.m_pc = tmp_next_pc;
            new_stack_entry.m_active_mask = tmp_active_mask;
            new_stack_entry.m_branch_div_cycle = gpu_sim_cycle+gpu_tot_sim_cycle;
            new_stack_entry.m_type = STACK_ENTRY_TYPE_CALL;
            m_stack.push_back(new_stack_entry);
            return;

        } else if (next_inst_op == RET_OPS and 
                   m_stack.size() > 1 and  // avoid popping the top-level kernel return
                   top_type == STACK_ENTRY_TYPE_CALL) 
        {
            // pop the CALL Entry
            assert(top_active_mask.any() == false);
            m_stack.pop_back();

            assert(m_stack.size() > 0);
            m_stack.back().m_pc=tmp_next_pc;// set the PC of the stack top entry to return PC from  the call stack;
            // Check if the New top of the stack is reconverging
            if (tmp_next_pc == m_stack.back().m_recvg_pc && m_stack.back().m_type!=STACK_ENTRY_TYPE_CALL)
            {
                assert(m_stack.back().m_type==STACK_ENTRY_TYPE_NORMAL);
                m_stack.pop_back();
            }

            return;
        }

        // discard the new entry if its PC matches with reconvergence PC
        // that automatically reconverges the entry
        // If the top stack entry is CALL, dont reconverge.
        if (tmp_next_pc == top_recvg_pc && (top_type != STACK_ENTRY_TYPE_CALL)) continue;

        // this new entry is not converging
        // if this entry does not include thread from the warp, divergence occurs
        if (top_active_mask.any() && !warp_diverged ) {
            warp_diverged = true;
            // modify the existing top entry into a reconvergence entry in the pdom stack
            new_recvg_pc = recvg_pc;
            if (new_recvg_pc != top_recvg_pc) {
                m_stack.back().m_pc = new_recvg_pc;
                m_stack.back().m_branch_div_cycle = gpu_sim_cycle+gpu_tot_sim_cycle;

                m_stack.push_back(simt_stack_entry());
	    }
        }

        // discard the new entry if its PC matches with reconvergence PC
        if (warp_diverged && tmp_next_pc == new_recvg_pc) continue;

        // update the current top of pdom stack
        m_stack.back().m_pc = tmp_next_pc;
        m_stack.back().m_active_mask = tmp_active_mask;
        if (warp_diverged) {
            m_stack.back().m_calldepth = 0;
            m_stack.back().m_recvg_pc = new_recvg_pc;
            m_stack.back().m_type = STACK_ENTRY_TYPE_NORMAL; 
        } else {
            m_stack.back().m_recvg_pc = top_recvg_pc;
        }

        m_stack.push_back(simt_stack_entry());
    }
    assert(m_stack.size() > 0);

    if (m_stack.size() > 2 and m_stack[m_stack.size() - 2].m_type == STACK_ENTRY_TYPE_PAUSE) {
	assert(m_stack.back().m_type == STACK_ENTRY_TYPE_TRANS);
	m_stack.back().m_pc = m_stack.back().m_recvg_pc;
        m_stack.back().m_recvg_pc = -1;
    } else {
        m_stack.pop_back();
    }
    
    if (warp_diverged) {
        ptx_file_line_stats_add_warp_divergence(top_pc, 1); 
    }
}

void simt_stack::set_warp(shd_warp_t *w) {
    m_warp = w;
}

void core_t::execute_warp_inst_t(warp_inst_t &inst, unsigned warpId)
{
    for ( unsigned t=0; t < m_warp_size; t++ ) {
        if( inst.active(t) ) {
            if(warpId==(unsigned (-1)))
                warpId = inst.warp_id();
            unsigned tid=m_warp_size*warpId+t;
            m_thread[tid]->ptx_exec_inst(inst,t);
            
            //virtual function
            checkExecutionStatusAndUpdate(inst,t,tid);
	} 
    } 
}
  
// void simt_stack::clone_entry(unsigned dst, unsigned src) 
// {
//     m_pc[dst] = m_pc[src]; 
//     m_active_mask[dst] = m_active_mask[src]; 
//     m_recvg_pc[dst] = m_recvg_pc[src]; 
//     m_calldepth[dst] = m_calldepth[src]; 
//     m_branch_div_cycle[dst] = m_branch_div_cycle[src]; 
//     m_type[dst] = m_type[src]; 
// }

tm_parallel_pdom_warp_ctx_t::tm_parallel_pdom_warp_ctx_t( unsigned sid, unsigned wid, unsigned warp_size )
    : simt_stack(sid, wid, warp_size) 
{ }

void tm_parallel_pdom_warp_ctx_t::txbegin(address_type tm_restart_pc) 
{
    if (m_in_transaction) return; 
    assert(m_stack.size() < m_warp_size * 2 - 2); // not necessarily true with infinite recursion, but a good check anyway

    const simt_stack_entry &orig_top_entry = m_stack.back(); 

    m_active_mask_at_txbegin = orig_top_entry.m_active_mask; 

    // insert retry entry
    simt_stack_entry retry_entry(orig_top_entry); 
    // unsigned retry_idx = m_stack_top + 1;
    // clone_entry(retry_idx, m_stack_top); 
    retry_entry.m_pc = tm_restart_pc; 
    retry_entry.m_recvg_pc = -1; //HACK: need to set this to the insn after txcommit() 
    retry_entry.m_active_mask.reset(); 
    retry_entry.m_type = STACK_ENTRY_TYPE_RETRY; 
    m_stack.push_back(retry_entry); 

    // insert transaction entry 
    simt_stack_entry texec_entry(orig_top_entry); 
    // unsigned texec_idx = m_stack_top + 2;
    // clone_entry(texec_idx, m_stack_top); 
    texec_entry.m_recvg_pc = -1; 
    texec_entry.m_pc = tm_restart_pc; 
    texec_entry.m_recvg_pc = -1; //HACK: need to set this to the insn after txcommit() 
    texec_entry.m_type = STACK_ENTRY_TYPE_TRANS; 
    m_stack.push_back(texec_entry); 

    //TODO: set TOS entry's PC to the insn after txcommit() 

    m_in_transaction = true; 
}

void tm_parallel_pdom_warp_ctx_t::txrestart() 
{
    assert(m_in_transaction); 

    // unsigned retry_idx = m_stack_top; 
    simt_stack_entry &retry_entry = m_stack.back(); 
    assert(retry_entry.m_type == STACK_ENTRY_TYPE_RETRY); 
    assert(retry_entry.m_active_mask.any()); 

    // clone the retry entry to create a new top level transaction entry 
    // unsigned texec_idx = retry_idx + 1; 
    // clone_entry(texec_idx, retry_idx); 
    simt_stack_entry texec_entry(retry_entry); 
    texec_entry.m_type = STACK_ENTRY_TYPE_TRANS; 

    // reset active mask in retry entry 
    retry_entry.m_active_mask.reset(); 

    m_stack.push_back(texec_entry);
}

// check for correctness conditions when a warp-level transaction is restarted 
bool tm_parallel_pdom_warp_ctx_t::check_txrestart_warp_level() 
{
   bool valid = true; 

   // all threads in warp restarts 
   assert(m_stack.back().m_active_mask == m_active_mask_at_txbegin); 
   // all threads in warp restarts 
   assert(m_stack.back().m_type == STACK_ENTRY_TYPE_TRANS); 
   return valid; 
}

// check for correctness conditions when a warp-level transaction is committed 
bool tm_parallel_pdom_warp_ctx_t::check_txcommit_warp_level() 
{
   bool valid = true; 

   // warp should no longer in transaction 
   assert(m_stack.back().m_type == STACK_ENTRY_TYPE_NORMAL); 
   return valid; 
}

void tm_parallel_pdom_warp_ctx_t::txabort(unsigned thread_id) 
{
    assert(m_in_transaction); 
    unsigned wtid = thread_id % m_warp_size; 

    // mask out this thread in the active mask of all entries in the transaction
    // pop TOS entry if the active mask is empty 
    int idx; 
    assert(m_stack.size() > 0);
    for (idx = m_stack.size() - 1; idx > 0 and m_stack[idx].m_type != STACK_ENTRY_TYPE_RETRY; idx--) {
        m_stack[idx].m_active_mask.reset(wtid); 
        if (m_stack[idx].m_active_mask.none()) {
	   if (m_stack[idx].m_type != STACK_ENTRY_TYPE_PAUSE)
               m_stack[idx].m_type = STACK_ENTRY_TYPE_INVALID; // this could be an inactive entry waiting for execution 
           if (idx == (int)(m_stack.size() - 1)) {
	       if (m_stack[idx].m_type == STACK_ENTRY_TYPE_PAUSE) {
	           tm_warp_info &current_tm_warp_info = m_warp->get_tm_warp_info();
		   current_tm_warp_info.m_read_log_size = m_stack.back().get_paused_read_log_entry();
		   current_tm_warp_info.m_write_log_size = m_stack.back().get_paused_write_log_entry();
	       }

               m_stack.pop_back(); 
           }
        }
    }

    // set the active mask for this thread at the retry entry 
    assert(m_stack[idx].m_type == STACK_ENTRY_TYPE_RETRY);
    m_stack[idx].m_active_mask.set(wtid); 

    // if the whole warp is aborted, trigger a retry 
    if (m_stack.back().m_type == STACK_ENTRY_TYPE_RETRY) {
        txrestart();
	extern tm_options g_tm_options;
	if (g_tm_options.m_use_logical_timestamp_based_tm) {
	    set_tm_restarted();	
	}
	if (g_tm_options.m_pause_and_go_enabled) {
	    m_warp->get_tm_warp_info().reset();
	}
	m_has_paused_threads = false;
    }
}

void tm_parallel_pdom_warp_ctx_t::txcommit(unsigned thread_id, address_type tm_commit_pc)
{
    extern tm_options g_tm_options;
    if (g_tm_options.m_pause_and_go_enabled) {
        txcommit_pause(thread_id, tm_commit_pc);
	return;
    }

    assert(m_in_transaction); 
    unsigned int stack_top = m_stack.size() - 1;
    assert(m_stack[stack_top - 1].m_type == STACK_ENTRY_TYPE_RETRY); 
    assert(m_stack.back().m_type == STACK_ENTRY_TYPE_TRANS); 

    unsigned wtid = thread_id % m_warp_size; 

    // clear the active mask for this thread at the top level transaction entry 
    m_stack.back().m_active_mask.reset(wtid); 

    if (m_stack.back().m_active_mask.none()) {
        // all threads in this warp commited or aborted 
        // pop this transaction entry 
        m_stack.pop_back(); 

        // check for need to restart 
        const simt_stack_entry &retry_entry = m_stack.back(); 

        if (retry_entry.m_active_mask.any()) {
            txrestart(); 
        } else {
            // no restart needed, pop the retry entry and set pc of TOS to the 
            // next insn after commit, transaction is done for this warp 
            m_stack.pop_back(); 
            m_stack.back().m_pc = tm_commit_pc; 
            // if the next pc after commit happens to be the reconvergence point as well
            // this is no longer handled by normal stack handler because functional commit is now further down the pipeline
            while ( m_stack.back().at_recvg() ) {
               m_stack.pop_back();
            }
            m_in_transaction = false; 
        }
    }
}

void tm_parallel_pdom_warp_ctx_t::txcommit_pause(unsigned thread_id, address_type tm_commit_pc) {
    assert(m_in_transaction);
    unsigned int stack_top = m_stack.size() - 1;
    assert(m_stack[stack_top - 1].m_type == STACK_ENTRY_TYPE_RETRY ||
           m_stack[stack_top - 1].m_type == STACK_ENTRY_TYPE_PAUSE); 
    assert(m_stack.back().m_type == STACK_ENTRY_TYPE_TRANS); 
  
    unsigned wtid = thread_id % m_warp_size; 
    // clear the active mask for this thread at the top level transaction entry 
    m_stack.back().m_active_mask.reset(wtid);

    if (m_stack.back().m_active_mask.none()) {
        // all threads in this warp commited or aborted 
        // pop this transaction entry 
        m_stack.pop_back();

	do {
            if (m_stack.back().m_type == STACK_ENTRY_TYPE_PAUSE) {
		// pop out and recover log entry number
	        tm_warp_info &current_tm_warp_info = m_warp->get_tm_warp_info();
		current_tm_warp_info.m_read_log_size = m_stack.back().get_paused_read_log_entry();
		current_tm_warp_info.m_write_log_size = m_stack.back().get_paused_write_log_entry();
	        m_stack.pop_back();
	    } else if (m_stack.back().m_type == STACK_ENTRY_TYPE_RETRY){
                // check for need to restart 
                const simt_stack_entry &retry_entry = m_stack.back(); 

                if (retry_entry.m_active_mask.any()) {
                    txrestart();
                } else {
                    // no restart needed, pop the retry entry and set pc of TOS to the 
                    // next insn after commit, transaction is done for this warp 
                    m_stack.pop_back(); 
                    m_stack.back().m_pc = tm_commit_pc; 
                    // if the next pc after commit happens to be the reconvergence point as well
                    // this is no longer handled by normal stack handler because functional commit is now further down the pipeline
                    while ( m_stack.back().at_recvg() ) {
                       m_stack.pop_back();
                    }
                    m_in_transaction = false; 
                }
		m_has_paused_threads = false;
		break;
	    } else {
	        if (m_stack.back().m_active_mask.any()) {
		    break;
		} else {
		    m_stack.pop_back();
		}
	    }
        } while (true);	
    }
}

// Function for the LSU HPCA2016 Early Abort paper
void tm_parallel_pdom_warp_ctx_t::pause_and_go(active_mask_t word_active) {
    // only one thread avtive, just keep going, avoid livelock
    if (get_active_mask().count() == 1) return;
    // generate an entry for the threads which still keep going
    simt_stack_entry go_threads_entry = simt_stack_entry(m_stack.back());
    go_threads_entry.m_type = STACK_ENTRY_TYPE_TRANS;
    go_threads_entry.m_recvg_pc = -1;
    bool paused = false;
    for (unsigned i = 0; i < word_active.size(); i++) {    
	if (go_threads_entry.m_active_mask.count() == 1) break;
	if (word_active.test(i)) {
            if (go_threads_entry.m_active_mask.test(i)) {
                go_threads_entry.m_active_mask.reset(i);
		extern tm_global_statistics g_tm_global_statistics;
		g_tm_global_statistics.m_tot_pauses++;
		paused = true;
	    }
	}
    }
    if (paused) {
        for (unsigned i = 0; i < go_threads_entry.m_active_mask.size(); i++) {
	    if (go_threads_entry.m_active_mask.test(i)) {
                for (int idx = m_stack.size() - 1; idx > 0 and m_stack[idx].m_type != STACK_ENTRY_TYPE_RETRY; idx--) {
                    m_stack[idx].m_active_mask.reset(i); 
                }
	    }
	}

        // insert a pause entry
        simt_stack_entry pause_threads_entry = simt_stack_entry(m_stack.back());
	pause_threads_entry.m_active_mask.reset();
        pause_threads_entry.m_type = STACK_ENTRY_TYPE_PAUSE;
	const tm_warp_info &current_tm_warp_info = m_warp->get_tm_warp_info();
	pause_threads_entry.set_paused_read_log_entry(current_tm_warp_info.m_read_log_size);
	pause_threads_entry.set_paused_write_log_entry(current_tm_warp_info.m_write_log_size);
        m_stack.push_back(pause_threads_entry);
	m_has_paused_threads = true;

        m_stack.push_back(go_threads_entry);
    }
}

tm_serial_pdom_warp_ctx_t::tm_serial_pdom_warp_ctx_t( unsigned sid, unsigned wid, unsigned warp_size )
    : simt_stack(sid, wid, warp_size) 
{ }

void tm_serial_pdom_warp_ctx_t::txbegin(address_type tm_restart_pc) 
{
    if (m_in_transaction) return; 
    assert(m_stack.size() < m_warp_size * 2 - 2); // not necessarily true with infinite recursion, but a good check anyway

    const simt_stack_entry &orig_top_entry = m_stack.back(); 

    // insert retry entry 
    // - this holds the threads that are deferred due to serialization, 
    // as well as place holder for transaction retry 
    // (though usually the same thread will set to retry after abortion)
    simt_stack_entry retry_entry(orig_top_entry); 
    retry_entry.m_pc = tm_restart_pc; 
    retry_entry.m_recvg_pc = -1; //HACK: need to set this to the insn after txcommit() 
    retry_entry.m_type = STACK_ENTRY_TYPE_RETRY; 
    m_stack.push_back(retry_entry); 
    unsigned retry_idx = m_stack.size() - 1; 

    // insert transaction entry 
    simt_stack_entry texec_entry(orig_top_entry); 
    texec_entry.m_recvg_pc = -1; 
    texec_entry.m_pc = tm_restart_pc; 
    texec_entry.m_recvg_pc = -1; //HACK: need to set this to the insn after txcommit() 
    texec_entry.m_type = STACK_ENTRY_TYPE_TRANS; 
    m_stack.push_back(texec_entry); 
    unsigned texec_idx = m_stack.size() - 1; 

    //TODO: set TOS entry's PC to the insn after txcommit() 

    tx_start_thread(retry_idx, texec_idx); 

    m_in_transaction = true; 
}

// move one thread from retry to transaction entry 
void tm_serial_pdom_warp_ctx_t::tx_start_thread(unsigned retry_idx, unsigned texec_idx) 
{
    // find a thread in the retry entry and bring it to transaction entry 
    m_stack[texec_idx].m_active_mask.reset(); // clear mask in transaction entry 
    for (unsigned t = 0; t < m_warp_size; t++) {
        if (m_stack[retry_idx].m_active_mask.test(t) == true) {
            m_stack[texec_idx].m_active_mask.set(t); 
            m_stack[retry_idx].m_active_mask.reset(t);
            
            // unsigned hw_thread_id = t + m_warp_id * m_warp_size; 
            // m_shader->set_transactional_thread( m_shader->get_func_thread_info(hw_thread_id) );

            break; 
        }
    }
    assert(m_stack[texec_idx].m_active_mask.any()); 
}

void tm_serial_pdom_warp_ctx_t::txrestart() 
{
    assert(m_in_transaction); 

    // unsigned retry_idx = m_stack_top; 
    simt_stack_entry &retry_entry = m_stack.back(); 
    assert(retry_entry.m_type == STACK_ENTRY_TYPE_RETRY); 
    assert(retry_entry.m_active_mask.any()); 
    unsigned retry_idx = m_stack.size() - 1;

    // clone the retry entry to create a new top level transaction entry 
    // unsigned texec_idx = retry_idx + 1; 
    // clone_entry(texec_idx, retry_idx); 
    simt_stack_entry texec_entry(retry_entry); 
    texec_entry.m_type = STACK_ENTRY_TYPE_TRANS; 
    m_stack.push_back(texec_entry); 
    unsigned texec_idx = m_stack.size() - 1;

    tx_start_thread(retry_idx, texec_idx); 
    // no need to reset active mask in retry entry 
} 

void tm_serial_pdom_warp_ctx_t::txabort(unsigned thread_id) 
{
    assert(m_in_transaction); 
    unsigned wtid = thread_id % m_warp_size; 

    // mask out this thread in the active mask of all entries in the transaction
    // pop TOS entry if the active mask is empty 
    int idx; 
    assert(m_stack.size() > 0);
    for (idx = m_stack.size() - 1; idx > 0 and m_stack[idx].m_type != STACK_ENTRY_TYPE_RETRY; idx--) {
        m_stack[idx].m_active_mask.reset(wtid); 
        if (m_stack[idx].m_active_mask.none() and idx == (int)(m_stack.size() - 1)) {
            m_stack.pop_back(); 
        }
    }

    // set the active mask for this thread at the retry entry 
    assert(m_stack[idx].m_type == STACK_ENTRY_TYPE_RETRY);
    m_stack[idx].m_active_mask.set(wtid); 

    // if the whole warp is aborted, trigger a retry 
    if (m_stack.back().m_type == STACK_ENTRY_TYPE_RETRY) {
        txrestart();
	extern tm_options g_tm_options;
	if (g_tm_options.m_use_logical_timestamp_based_tm)
            set_tm_restarted();	
    }
}

void tm_serial_pdom_warp_ctx_t::txcommit(unsigned thread_id, address_type tm_commit_pc)
{
    assert(m_in_transaction); 
    unsigned int stack_top = m_stack.size() - 1; 
    assert(m_stack[stack_top - 1].m_type == STACK_ENTRY_TYPE_RETRY); 
    assert(m_stack.back().m_type == STACK_ENTRY_TYPE_TRANS); 

    unsigned wtid = thread_id % m_warp_size; 

    // clear the active mask for this thread at the top level transaction entry 
    m_stack.back().m_active_mask.reset(wtid); 

    // m_shader->set_transactional_thread(NULL);

    if (m_stack.back().m_active_mask.none()) {
        // all threads in this warp commited or aborted 
        // pop this transaction entry 
        m_stack.pop_back(); 

        // check for need to restart 
        const simt_stack_entry &retry_entry = m_stack.back(); 
        if (retry_entry.m_active_mask.any()) {
            txrestart(); 
        } else {
            // no restart needed, pop the retry entry and set pc of TOS to the 
            // next insn after commit, transaction is done for this warp 
            m_stack.pop_back(); 
            m_stack.back().m_pc = tm_commit_pc; 
            m_in_transaction = false; 
        }
    }
}

bool  core_t::ptx_thread_done( unsigned hw_thread_id ) const  
{
    return ((m_thread[ hw_thread_id ]==NULL) || m_thread[ hw_thread_id ]->is_done());
}
  
void core_t::updateSIMTStack(unsigned warpId, warp_inst_t * inst)
{
    // extract thread done and next pc information from functional model here
    simt_mask_t thread_done;
    addr_vector_t next_pc;
    unsigned wtid = warpId * m_warp_size;
    for (unsigned i = 0; i < m_warp_size; i++) {
        if( ptx_thread_done(wtid+i) ) {
            thread_done.set(i);
            next_pc.push_back( (address_type)-1 );
        } else {
            assert( m_thread[wtid + i] != NULL ); 
            if( inst->reconvergence_pc == RECONVERGE_RETURN_PC ) 
                inst->reconvergence_pc = get_return_pc(m_thread[wtid+i]);
            next_pc.push_back( m_thread[wtid+i]->get_pc() );
        }
    }
    m_simt_stack[warpId]->update(thread_done,next_pc,inst->reconvergence_pc, inst->op);
}

//! Get the warp to be executed using the data taken form the SIMT stack
warp_inst_t core_t::getExecuteWarp(unsigned warpId)
{
    unsigned pc,rpc;
    m_simt_stack[warpId]->get_pdom_stack_top_info(&pc,&rpc);
    warp_inst_t wi= *ptx_fetch_inst(pc);
    wi.set_active(m_simt_stack[warpId]->get_active_mask());
    return wi;
}

void core_t::deleteSIMTStack()
{
    if ( m_simt_stack ) {
        for (unsigned i = 0; i < m_warp_count; ++i) 
            delete m_simt_stack[i];
        delete[] m_simt_stack;
        m_simt_stack = NULL;
    }
}

void core_t::initilizeSIMTStack(unsigned warp_count, unsigned warp_size, unsigned core_id)
{ 
    m_simt_stack = new simt_stack*[warp_count];
    for (unsigned i = 0; i < warp_count; ++i) {
        // m_simt_stack[i] = new simt_stack(i,warp_size);
        if (m_gpu->getShaderCoreConfig()->tm_serial_pdom_stack == true) {
            m_simt_stack[i] = new tm_serial_pdom_warp_ctx_t(core_id, i, warp_size);
        } else {
            m_simt_stack[i] = new tm_parallel_pdom_warp_ctx_t(core_id, i,warp_size);
        }
    }
}

void core_t::get_pdom_stack_top_info( unsigned warpId, unsigned *pc, unsigned *rpc ) const
{
    m_simt_stack[warpId]->get_pdom_stack_top_info(pc,rpc);
}

void warp_inst_t::add_logical_tm_callback( unsigned lane_id, 
                                           logical_tm_callback_function function,
                                           const ptx_instruction *inst, 
                                           class ptx_thread_info *thread,
    	                                   const addr_t addr, const size_t size, 
					   const unsigned vector_spec, const unsigned type,
					   memory_space *mem, memory_space_t space )
{
    if( !m_per_scalar_thread_valid ) {
        m_per_scalar_thread.resize(m_config->warp_size);
        m_per_scalar_thread_valid=true;
    }
    m_is_logical_tm_req=true;
    m_per_scalar_thread[lane_id].logical_tm_callback.function = function;
    m_per_scalar_thread[lane_id].logical_tm_callback.instruction = inst;
    m_per_scalar_thread[lane_id].logical_tm_callback.thread = thread;
    m_per_scalar_thread[lane_id].logical_tm_callback.addr = addr;
    m_per_scalar_thread[lane_id].logical_tm_callback.size = size;
    m_per_scalar_thread[lane_id].logical_tm_callback.vector_spec = vector_spec;
    m_per_scalar_thread[lane_id].logical_tm_callback.type = type;
    m_per_scalar_thread[lane_id].logical_tm_callback.mem = mem;
    m_per_scalar_thread[lane_id].logical_tm_callback.space = space;
}

logical_tm_dram_callback_t::logical_tm_dram_callback_t() 
    :space(memory_space_t()) 
{
    function = NULL;
    instruction = NULL;
    thread = NULL;
    mem = NULL;    
}

void logical_tm_dram_callback_t::reset() 
{
    function = NULL;
    instruction = NULL;
    thread = NULL;
    mem = NULL;    
}
