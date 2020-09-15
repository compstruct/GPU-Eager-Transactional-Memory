// Copyright (c) 2009-2011, Tor M. Aamodt, Wilson W.L. Fung, Ali Bakhoda,
// George L. Yuan, Andrew Turner, Inderpreet Singh 
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

#include <float.h>
#include "shader.h"
#include "gpu-sim.h"
#include "addrdec.h"
#include "dram.h"
#include "stat-tool.h"
#include "gpu-misc.h"
#include "../cuda-sim/ptx_sim.h"
#include "../cuda-sim/ptx-stats.h"
#include "../cuda-sim/cuda-sim.h"
#include "gpu-sim.h"
#include "mem_fetch.h"
#include "mem_latency_stat.h"
#include "visualizer.h"
#include "../intersim/statwraper.h"
#include "../intersim/interconnect_interface.h"
#include "icnt_wrapper.h"
#include <string.h>
#include <limits.h>
#include "traffic_breakdown.h"
#include "shader_trace.h"
#include "traffic_breakdown.h"
#include <cstdlib>

#define PRIORITIZE_MSHR_OVER_WB 1
#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

extern tm_options g_tm_options;
extern tm_global_statistics g_tm_global_statistics;

/////////////////////////////////////////////////////////////////////////////

std::list<unsigned> shader_core_ctx::get_regs_written( const inst_t &fvt ) const
{
   std::list<unsigned> result;
   for( unsigned op=0; op < MAX_REG_OPERANDS; op++ ) {
      int reg_num = fvt.arch_reg.dst[op]; // this math needs to match that used in function_info::ptx_decode_inst
      if( reg_num >= 0 ) // valid register
         result.push_back(reg_num);
   }
   return result;
}

shader_core_ctx::shader_core_ctx( class gpgpu_sim *gpu, 
                                  class simt_core_cluster *cluster,
                                  unsigned shader_id,
                                  unsigned tpc_id,
                                  const struct shader_core_config *config,
                                  const struct memory_config *mem_config,
                                  shader_core_stats *stats )
   : core_t( gpu, NULL, config->warp_size, config->n_thread_per_shader, shader_id ),
     m_barriers( config->max_warps_per_shader, config->max_cta_per_core ),
     m_dynamic_warp_id(0),
     m_over_num_aborts_limit(false)
{
    m_cluster = cluster;
    m_config = config;
    m_memory_config = mem_config;
    m_stats = stats;
    unsigned warp_size=config->warp_size;

    m_sid = shader_id;
    m_tpc = tpc_id;

    m_pipeline_reg.reserve(N_PIPELINE_STAGES);
    for (int j = 0; j<N_PIPELINE_STAGES; j++) {
        m_pipeline_reg.push_back(register_set(m_config->pipe_widths[j],pipeline_stage_name_decode[j]));
    }

    m_threadState = new thread_ctx_t[config->n_thread_per_shader];

    m_not_completed = 0;
    m_active_threads.reset();
    m_n_active_cta = 0;
    for (unsigned i = 0; i<MAX_CTA_PER_SHADER; i++  ) 
        m_cta_status[i]=0;
    for (unsigned i = 0; i<config->n_thread_per_shader; i++) {
        m_thread[i]= NULL;
        // TODO: Move to constructor
        m_threadState[i].m_cta_id = -1;
        m_threadState[i].m_active = false;
        m_threadState[i].m_active_set = false;
        m_threadState[i].m_atomic_mode = false;
        m_threadState[i].m_in_pipeline = 0;
        m_threadState[i].m_inflight_mem_accesses = 0;
        m_threadState[i].m_timeout_validation_mode = false;
        m_threadState[i].m_timeout_validation_mode_set = false;
        m_threadState[i].m_timeout_validation_cycle = 0;
        m_threadState[i].m_active_cycle = 0;
        m_threadState[i].m_active_in_commit = false;
        m_threadState[i].m_active_in_cleaning = false;
    }
    
    // m_icnt = new shader_memory_interface(this,cluster);
    // instantiating both perfect and normal memory interface to allow in-flight switching
    m_normal_icnt = new shader_memory_interface(this,cluster);
    m_perfect_icnt = new perfect_memory_interface(this,cluster);
    if (m_config->gpgpu_perfect_mem) {
        m_icnt = m_perfect_icnt; 
    } else {
        m_icnt = m_normal_icnt; 
    }

    m_mem_fetch_allocator = new shader_core_mem_fetch_allocator(shader_id,tpc_id,mem_config);

    // fetch
    m_last_warp_fetched = 0;

    #define STRSIZE 1024
    char name[STRSIZE];
    snprintf(name, STRSIZE, "L1I_%03d", m_sid);
    m_L1I = new read_only_cache( name,m_config->m_L1I_config,m_sid,get_shader_instruction_cache_id(),m_icnt,IN_L1I_MISS_QUEUE);

    m_warp.resize(m_config->max_warps_per_shader, shd_warp_t(this, warp_size));

    if (m_config->simplescoreboard) {
        m_scoreboard = new SimpleScoreboard(m_sid, m_config->max_warps_per_shader, m_simt_stack);
    } else { 
        m_scoreboard = new Scoreboard(m_sid, m_config->max_warps_per_shader, m_simt_stack, m_config->tm_warp_scoreboard_token);
    }

    for (unsigned i = 0; i < m_warp.size(); i++) {
        m_simt_stack[i]->set_warp(&(m_warp[i]));
    }

    //schedulers
    //must currently occur after all inputs have been initialized.
    std::string sched_config = m_config->gpgpu_scheduler_string;
    const concrete_scheduler scheduler = sched_config.find("lrr") != std::string::npos ?
                                         CONCRETE_SCHEDULER_LRR :
                                         sched_config.find("two_level_active") != std::string::npos ?
                                         CONCRETE_SCHEDULER_TWO_LEVEL_ACTIVE :
                                         sched_config.find("gto") != std::string::npos ?
                                         CONCRETE_SCHEDULER_GTO :
                                         sched_config.find("warp_limiting") != std::string::npos ?
                                         CONCRETE_SCHEDULER_WARP_LIMITING:
                                         NUM_CONCRETE_SCHEDULERS;
    assert ( scheduler != NUM_CONCRETE_SCHEDULERS );
    
    for (int i = 0; i < m_config->gpgpu_num_sched_per_core; i++) {
        switch( scheduler )
        {
            case CONCRETE_SCHEDULER_LRR:
                schedulers.push_back(
                    new lrr_scheduler( m_stats,
                                       this,
                                       m_scoreboard,
                                       m_simt_stack,
                                       &m_warp,
                                       &m_pipeline_reg[ID_OC_SP],
                                       &m_pipeline_reg[ID_OC_SFU],
                                       &m_pipeline_reg[ID_OC_MEM],
                                       i
                                     )
                );
                break;
            case CONCRETE_SCHEDULER_TWO_LEVEL_ACTIVE:
                schedulers.push_back(
                    new two_level_active_scheduler( m_stats,
                                                    this,
                                                    m_scoreboard,
                                                    m_simt_stack,
                                                    &m_warp,
                                                    &m_pipeline_reg[ID_OC_SP],
                                                    &m_pipeline_reg[ID_OC_SFU],
                                                    &m_pipeline_reg[ID_OC_MEM],
                                                    i,
                                                    config->gpgpu_scheduler_string
                                                  )
                );
                break;
            case CONCRETE_SCHEDULER_GTO:
                schedulers.push_back(
                    new gto_scheduler( m_stats,
                                       this,
                                       m_scoreboard,
                                       m_simt_stack,
                                       &m_warp,
                                       &m_pipeline_reg[ID_OC_SP],
                                       &m_pipeline_reg[ID_OC_SFU],
                                       &m_pipeline_reg[ID_OC_MEM],
                                       i
                                     )
                );
                break;
            case CONCRETE_SCHEDULER_WARP_LIMITING:
                schedulers.push_back(
                    new swl_scheduler( m_stats,
                                       this,
                                       m_scoreboard,
                                       m_simt_stack,
                                       &m_warp,
                                       &m_pipeline_reg[ID_OC_SP],
                                       &m_pipeline_reg[ID_OC_SFU],
                                       &m_pipeline_reg[ID_OC_MEM],
                                       i,
                                       config->gpgpu_scheduler_string
                                     )
                );
                break;
            default:
                abort();
        };
    }
    
    for (unsigned i = 0; i < m_warp.size(); i++) {
        //distribute i's evenly though schedulers;
        schedulers[i%m_config->gpgpu_num_sched_per_core]->add_supervised_warp_id(i);
    }
    for ( int i = 0; i < m_config->gpgpu_num_sched_per_core; ++i ) {
        schedulers[i]->done_adding_supervised_warps();
    }

    //op collector configuration
    enum { SP_CUS, SFU_CUS, MEM_CUS, GEN_CUS };
    m_operand_collector.add_cu_set(SP_CUS, m_config->gpgpu_operand_collector_num_units_sp, m_config->gpgpu_operand_collector_num_out_ports_sp);
    m_operand_collector.add_cu_set(SFU_CUS, m_config->gpgpu_operand_collector_num_units_sfu, m_config->gpgpu_operand_collector_num_out_ports_sfu);
    m_operand_collector.add_cu_set(MEM_CUS, m_config->gpgpu_operand_collector_num_units_mem, m_config->gpgpu_operand_collector_num_out_ports_mem);
    m_operand_collector.add_cu_set(GEN_CUS, m_config->gpgpu_operand_collector_num_units_gen, m_config->gpgpu_operand_collector_num_out_ports_gen);

    opndcoll_rfu_t::port_vector_t in_ports;
    opndcoll_rfu_t::port_vector_t out_ports;
    opndcoll_rfu_t::uint_vector_t cu_sets;
    for (unsigned i = 0; i < m_config->gpgpu_operand_collector_num_in_ports_sp; i++) {
        in_ports.push_back(&m_pipeline_reg[ID_OC_SP]);
        out_ports.push_back(&m_pipeline_reg[OC_EX_SP]);
        cu_sets.push_back((unsigned)SP_CUS);
        cu_sets.push_back((unsigned)GEN_CUS);
        m_operand_collector.add_port(in_ports,out_ports,cu_sets);
        in_ports.clear(),out_ports.clear(),cu_sets.clear();
    }

    for (unsigned i = 0; i < m_config->gpgpu_operand_collector_num_in_ports_sfu; i++) {
        in_ports.push_back(&m_pipeline_reg[ID_OC_SFU]);
        out_ports.push_back(&m_pipeline_reg[OC_EX_SFU]);
        cu_sets.push_back((unsigned)SFU_CUS);
        cu_sets.push_back((unsigned)GEN_CUS);
        m_operand_collector.add_port(in_ports,out_ports,cu_sets);
        in_ports.clear(),out_ports.clear(),cu_sets.clear();
    }

    for (unsigned i = 0; i < m_config->gpgpu_operand_collector_num_in_ports_mem; i++) {
        in_ports.push_back(&m_pipeline_reg[ID_OC_MEM]);
        out_ports.push_back(&m_pipeline_reg[OC_EX_MEM]);
        cu_sets.push_back((unsigned)MEM_CUS);
        cu_sets.push_back((unsigned)GEN_CUS);                       
        m_operand_collector.add_port(in_ports,out_ports,cu_sets);
        in_ports.clear(),out_ports.clear(),cu_sets.clear();
    }   


    for (unsigned i = 0; i < m_config->gpgpu_operand_collector_num_in_ports_gen; i++) {
        in_ports.push_back(&m_pipeline_reg[ID_OC_SP]);
        in_ports.push_back(&m_pipeline_reg[ID_OC_SFU]);
        in_ports.push_back(&m_pipeline_reg[ID_OC_MEM]);
        out_ports.push_back(&m_pipeline_reg[OC_EX_SP]);
        out_ports.push_back(&m_pipeline_reg[OC_EX_SFU]);
        out_ports.push_back(&m_pipeline_reg[OC_EX_MEM]);
        cu_sets.push_back((unsigned)GEN_CUS);   
        m_operand_collector.add_port(in_ports,out_ports,cu_sets);
        in_ports.clear(),out_ports.clear(),cu_sets.clear();
    }

    m_operand_collector.init( m_config->gpgpu_num_reg_banks, this );

    // execute
    m_num_function_units = m_config->gpgpu_num_sp_units + m_config->gpgpu_num_sfu_units + 1; // sp_unit, sfu, ldst_unit
    //m_dispatch_port = new enum pipeline_stage_name_t[ m_num_function_units ];
    //m_issue_port = new enum pipeline_stage_name_t[ m_num_function_units ];

    //m_fu = new simd_function_unit*[m_num_function_units];

    for (int k = 0; k < m_config->gpgpu_num_sp_units; k++) {
        m_fu.push_back(new sp_unit( &m_pipeline_reg[EX_WB], m_config, this ));
        m_dispatch_port.push_back(ID_OC_SP);
        m_issue_port.push_back(OC_EX_SP);
    }

    for (int k = 0; k < m_config->gpgpu_num_sfu_units; k++) {
        m_fu.push_back(new sfu( &m_pipeline_reg[EX_WB], m_config, this ));
        m_dispatch_port.push_back(ID_OC_SFU);
        m_issue_port.push_back(OC_EX_SFU);
    }

    m_ldst_unit = new ldst_unit( m_icnt, m_mem_fetch_allocator, this, &m_operand_collector, m_scoreboard, config, mem_config, stats, shader_id, tpc_id );
    m_fu.push_back(m_ldst_unit);
    m_dispatch_port.push_back(ID_OC_MEM);
    m_issue_port.push_back(OC_EX_MEM);

    assert(m_num_function_units == m_fu.size() and m_fu.size() == m_dispatch_port.size() and m_fu.size() == m_issue_port.size());

    //there are as many result buses as the width of the EX_WB stage
    num_result_bus = config->pipe_widths[EX_WB];
    for(unsigned i=0; i<num_result_bus; i++){
        this->m_result_bus.push_back(new std::bitset<MAX_ALU_LATENCY>());
    }

    m_last_inst_gpu_sim_cycle = 0;
    m_last_inst_gpu_tot_sim_cycle = 0;
}

shader_core_ctx::~shader_core_ctx() {
   free(m_thread);
   delete [] m_threadState;
}

void shader_core_ctx::reinit(unsigned start_thread, unsigned end_thread, bool reset_not_completed ) 
{
   if( reset_not_completed ) {
       m_not_completed = 0;
       m_active_threads.reset();
   }
   for (unsigned i = start_thread; i<end_thread; i++) {
      m_threadState[i].n_insn = 0;
      m_threadState[i].m_cta_id = -1;
   }
   for (unsigned i = start_thread / m_config->warp_size; i < end_thread / m_config->warp_size; ++i) {
      m_warp[i].reset();
      m_simt_stack[i]->reset();
   }
}

void shader_core_ctx::init_warps( unsigned cta_id, unsigned start_thread, unsigned end_thread )
{
    address_type start_pc = next_pc(start_thread);
    if (m_config->model == POST_DOMINATOR) {
        unsigned start_warp = start_thread / m_config->warp_size;
        unsigned end_warp = end_thread / m_config->warp_size + ((end_thread % m_config->warp_size)? 1 : 0);
        for (unsigned i = start_warp; i < end_warp; ++i) {
            unsigned n_active=0;
            simt_mask_t active_threads;
            for (unsigned t = 0; t < m_config->warp_size; t++) {
                unsigned hwtid = i * m_config->warp_size + t;
                if ( hwtid < end_thread ) {
                    n_active++;
                    assert( !m_active_threads.test(hwtid) );
                    m_active_threads.set( hwtid );
                    active_threads.set(t);
                }
            }
            m_simt_stack[i]->launch(start_pc,active_threads);
            m_warp[i].init(start_pc,cta_id,i,active_threads, m_dynamic_warp_id);
            ++m_dynamic_warp_id;
            m_not_completed += n_active;
      }
   }
}

// return the next pc of a thread 
address_type shader_core_ctx::next_pc( int tid ) const
{
    if( tid == -1 ) 
        return -1;
    ptx_thread_info *the_thread = m_thread[tid];
    if ( the_thread == NULL )
        return -1;
    return the_thread->get_pc(); // PC should already be updatd to next PC at this point (was set in shader_decode() last time thread ran)
}

void gpgpu_sim::get_pdom_stack_top_info( unsigned sid, unsigned tid, unsigned *pc, unsigned *rpc )
{
    unsigned cluster_id = m_shader_config->sid_to_cluster(sid);
    m_cluster[cluster_id]->get_pdom_stack_top_info(sid,tid,pc,rpc);
}

void shader_core_ctx::get_pdom_stack_top_info( unsigned tid, unsigned *pc, unsigned *rpc ) const
{
    unsigned warp_id = tid/m_config->warp_size;
    m_simt_stack[warp_id]->get_pdom_stack_top_info(pc,rpc);
}

class tx_log_walker_stats
{
public:
   unsigned n_atag_read; 
   unsigned n_data_read; 
   unsigned n_atag_cachercf; 
   unsigned n_data_cachercf; 
   unsigned n_atag_cachemiss; 
   unsigned n_data_cachemiss; 
   unsigned n_cu_pass_msg;
   unsigned n_warp_commit_attempt; 
   unsigned n_warp_commit_read_only;
   unsigned n_pre_commit_validation_abort; 
   unsigned n_pre_commit_validation_pass; 
   unsigned n_intra_warp_conflicts_detected; 
   unsigned n_intra_warp_aborts_false_positive; 
   unsigned n_intra_warp_complete_abort; 
   linear_histogram m_intra_warp_pre_cd_active; 
   linear_histogram m_intra_warp_aborts; 
   pow2_histogram m_ownership_table_size; 
   pow2_histogram m_ownership_aliasing_depth_avg; 
   pow2_histogram m_ownership_aliasing_depth_max; 
   pow2_histogram m_ownership_aliasing_depth_usage; 
   std::map<int,unsigned> m_intra_warp_cd_cycle; 
   pow2_histogram m_intra_warp_cd_cycle_per_warp; 
   linear_histogram m_cu_allocation_retries;
   pow2_histogram m_out_message_queue_size; 
   pow2_histogram m_out_txreply_queue_size; 
   linear_histogram m_coalesced_packet_size;
   pow2_histogram m_warp_read_log_size; 
   pow2_histogram m_warp_write_log_size; 
   std::map<int,unsigned*> m_sent_icnt_traffic; 
   
   tx_log_walker_stats() 
      : m_intra_warp_pre_cd_active(1, "tlw_intra_warp_pre_cd_active", 33), 
        m_intra_warp_aborts(1, "tlw_intra_warp_aborts", 33), 
        m_ownership_table_size("tlw_ownership_table_size"),
        m_ownership_aliasing_depth_avg("tlw_ownership_aliasing_depth_avg"), 
        m_ownership_aliasing_depth_max("tlw_ownership_aliasing_depth_max"), 
        m_ownership_aliasing_depth_usage("tlw_ownership_aliasing_depth_usage"), 
        m_intra_warp_cd_cycle_per_warp("tlw_intra_warp_cd_cycle_per_warp"), 
        m_cu_allocation_retries(1, "cu_allocation_retries"),
        m_out_message_queue_size("tlw_out_message_queue_size"),
        m_out_txreply_queue_size("tlw_out_txreply_queue_size"),
        m_coalesced_packet_size(1, "tlw_coalesced_packet_size"),
        m_warp_read_log_size("tlw_warp_read_log_size"), 
        m_warp_write_log_size("tlw_warp_write_log_size") 
   {
      n_atag_read = 0; 
      n_data_read = 0; 
      n_atag_cachercf = 0; 
      n_data_cachercf = 0; 
      n_atag_cachemiss = 0; 
      n_data_cachemiss = 0;
      n_cu_pass_msg = 0;
      n_warp_commit_attempt = 0; 
      n_warp_commit_read_only = 0; 
      n_pre_commit_validation_abort = 0; 
      n_pre_commit_validation_pass = 0; 
      n_intra_warp_conflicts_detected = 0; 
      n_intra_warp_aborts_false_positive = 0; 
      n_intra_warp_complete_abort = 0; 
   }
   void print( FILE *fout ) const; 
};

shader_core_stats::shader_core_stats( const shader_core_config *config )
{
    m_config = config;
    shader_core_stats_pod *pod = reinterpret_cast< shader_core_stats_pod * > ( this->shader_core_stats_pod_start );
    memset(pod,0,sizeof(shader_core_stats_pod));
    m_TLW_stats = new tx_log_walker_stats(); 

    shader_cycles=(unsigned long long *) calloc(config->num_shader(),sizeof(unsigned long long ));

    m_num_sim_insn = (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_sim_winsn = (unsigned*) calloc(config->num_shader(),sizeof(unsigned)); 
    m_last_num_sim_winsn = (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_last_num_sim_insn = (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_pipeline_duty_cycle=(float*) calloc(config->num_shader(),sizeof(float));
    m_num_decoded_insn = (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_FPdecoded_insn = (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_storequeued_insn=(unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_loadqueued_insn=(unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_INTdecoded_insn = (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_ialu_acesses = (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_fp_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_tex_inst= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_imul_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_imul24_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_imul32_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_fpmul_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_idiv_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_fpdiv_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_sp_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_sfu_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_trans_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_mem_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_sp_committed= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_tlb_hits=(unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_tlb_accesses=(unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_active_sp_lanes= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_active_sfu_lanes= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_active_fu_lanes= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_active_fu_mem_lanes= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_sfu_committed= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_num_mem_committed= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_read_regfile_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_write_regfile_acesses= (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_non_rf_operands=(unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    m_n_diverge = (unsigned*) calloc(config->num_shader(),sizeof(unsigned));
    shader_cycle_distro = (unsigned*) calloc(config->warp_size+3, sizeof(unsigned));
    last_shader_cycle_distro = (unsigned*) calloc(m_config->warp_size+3, sizeof(unsigned));

    n_simt_to_mem = (long *)calloc(config->num_shader(), sizeof(long));
    n_mem_to_simt = (long *)calloc(config->num_shader(), sizeof(long));

    m_outgoing_traffic_stats = new traffic_breakdown("coretomem"); 
    m_incoming_traffic_stats = new traffic_breakdown("memtocore"); 

    gpgpu_n_shmem_bank_access = (unsigned *)calloc(config->num_shader(), sizeof(unsigned));

    m_shader_dynamic_warp_issue_distro.resize( config->num_shader() );
    m_shader_warp_slot_issue_distro.resize( config->num_shader() );

    gpgpu_global_thread_state_nonatomic = new thread_state_stat("gpgpu_global_thread_state_nonatomic");
    gpgpu_global_thread_state_atomic = new thread_state_stat("gpgpu_global_thread_state_atomic");
    gpgpu_global_thread_state_tx_useful = new thread_state_stat("gpgpu_global_thread_state_tx_useful");
    gpgpu_global_thread_state_tx_aborted = new thread_state_stat("gpgpu_global_thread_state_tx_aborted");
}

shader_core_stats::~shader_core_stats()
{
    delete m_TLW_stats; 
    delete m_outgoing_traffic_stats; 
    delete m_incoming_traffic_stats; 
    free(m_num_sim_insn); 
    free(m_num_sim_winsn);
    free(m_n_diverge); 
    free(shader_cycle_distro);
    free(last_shader_cycle_distro);

    delete gpgpu_global_thread_state_nonatomic; 
    delete gpgpu_global_thread_state_atomic;
    delete gpgpu_global_thread_state_tx_useful;
    delete gpgpu_global_thread_state_tx_aborted;
}

void shader_core_stats::print( FILE* fout ) const
{
	unsigned long long  thread_icount_uarch=0;
	unsigned long long  warp_icount_uarch=0;

    for(unsigned i=0; i < m_config->num_shader(); i++) {
        thread_icount_uarch += m_num_sim_insn[i];
        warp_icount_uarch += m_num_sim_winsn[i];
    }
    fprintf(fout,"gpgpu_n_tot_thrd_icount = %lld\n", thread_icount_uarch);
    fprintf(fout,"gpgpu_n_tot_w_icount = %lld\n", warp_icount_uarch);

    fprintf(fout,"gpgpu_n_stall_shd_mem = %d\n", gpgpu_n_stall_shd_mem );
    fprintf(fout,"gpgpu_n_mem_read_local = %d\n", gpgpu_n_mem_read_local);
    fprintf(fout,"gpgpu_n_mem_write_local = %d\n", gpgpu_n_mem_write_local);
    fprintf(fout,"gpgpu_n_mem_read_global = %d\n", gpgpu_n_mem_read_global);
    fprintf(fout,"gpgpu_n_mem_write_global = %d\n", gpgpu_n_mem_write_global);
    fprintf(fout,"gpgpu_n_mem_texture = %d\n", gpgpu_n_mem_texture);
    fprintf(fout,"gpgpu_n_mem_const = %d\n", gpgpu_n_mem_const);
    fprintf(fout,"gpgpu_n_tx_msg = %d\n", gpgpu_n_tx_msg);
    m_TLW_stats->print(fout); 

   fprintf(fout, "gpgpu_n_load_insn  = %d\n", gpgpu_n_load_insn);
   fprintf(fout, "gpgpu_n_store_insn = %d\n", gpgpu_n_store_insn);
   fprintf(fout, "gpgpu_n_shmem_insn = %d\n", gpgpu_n_shmem_insn);
   fprintf(fout, "gpgpu_n_tex_insn = %d\n", gpgpu_n_tex_insn);
   fprintf(fout, "gpgpu_n_const_mem_insn = %d\n", gpgpu_n_const_insn);
   fprintf(fout, "gpgpu_n_param_mem_insn = %d\n", gpgpu_n_param_insn);

   fprintf(fout, "gpgpu_n_shmem_bkconflict = %d\n", gpgpu_n_shmem_bkconflict);
   fprintf(fout, "gpgpu_n_cache_bkconflict = %d\n", gpgpu_n_cache_bkconflict);   

   fprintf(fout, "gpgpu_n_intrawarp_mshr_merge = %d\n", gpgpu_n_intrawarp_mshr_merge);
   fprintf(fout, "gpgpu_n_cmem_portconflict = %d\n", gpgpu_n_cmem_portconflict);

   fprintf(fout, "gpgpu_stall_shd_mem[c_mem][bk_conf] = %d\n", gpu_stall_shd_mem_breakdown[C_MEM][BK_CONF]);
   fprintf(fout, "gpgpu_stall_shd_mem[c_mem][mshr_rc] = %d\n", gpu_stall_shd_mem_breakdown[C_MEM][MSHR_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[c_mem][icnt_rc] = %d\n", gpu_stall_shd_mem_breakdown[C_MEM][ICNT_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[c_mem][data_port_stall] = %d\n", gpu_stall_shd_mem_breakdown[C_MEM][DATA_PORT_STALL]);
   fprintf(fout, "gpgpu_stall_shd_mem[t_mem][mshr_rc] = %d\n", gpu_stall_shd_mem_breakdown[T_MEM][MSHR_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[t_mem][icnt_rc] = %d\n", gpu_stall_shd_mem_breakdown[T_MEM][ICNT_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[t_mem][data_port_stall] = %d\n", gpu_stall_shd_mem_breakdown[T_MEM][DATA_PORT_STALL]);
   fprintf(fout, "gpgpu_stall_shd_mem[s_mem][bk_conf] = %d\n", gpu_stall_shd_mem_breakdown[S_MEM][BK_CONF]);
   fprintf(fout, "gpgpu_stall_shd_mem[gl_mem][bk_conf] = %d\n", 
           gpu_stall_shd_mem_breakdown[G_MEM_LD][BK_CONF] + 
           gpu_stall_shd_mem_breakdown[G_MEM_ST][BK_CONF] + 
           gpu_stall_shd_mem_breakdown[L_MEM_LD][BK_CONF] + 
           gpu_stall_shd_mem_breakdown[L_MEM_ST][BK_CONF]   
           ); // coalescing stall at data cache 
   fprintf(fout, "gpgpu_stall_shd_mem[gl_mem][coal_stall] = %d\n", 
           gpu_stall_shd_mem_breakdown[G_MEM_LD][COAL_STALL] + 
           gpu_stall_shd_mem_breakdown[G_MEM_ST][COAL_STALL] + 
           gpu_stall_shd_mem_breakdown[L_MEM_LD][COAL_STALL] + 
           gpu_stall_shd_mem_breakdown[L_MEM_ST][COAL_STALL]    
           ); // coalescing stall + bank conflict at data cache 
   fprintf(fout, "gpgpu_stall_shd_mem[gl_mem][data_port_stall] = %d\n", 
           gpu_stall_shd_mem_breakdown[G_MEM_LD][DATA_PORT_STALL] + 
           gpu_stall_shd_mem_breakdown[G_MEM_ST][DATA_PORT_STALL] + 
           gpu_stall_shd_mem_breakdown[L_MEM_LD][DATA_PORT_STALL] + 
           gpu_stall_shd_mem_breakdown[L_MEM_ST][DATA_PORT_STALL]    
           ); // data port stall at data cache 
   fprintf(fout, "gpgpu_stall_shd_mem[g_mem_ld][mshr_rc] = %d\n", gpu_stall_shd_mem_breakdown[G_MEM_LD][MSHR_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[g_mem_ld][icnt_rc] = %d\n", gpu_stall_shd_mem_breakdown[G_MEM_LD][ICNT_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[g_mem_ld][wb_icnt_rc] = %d\n", gpu_stall_shd_mem_breakdown[G_MEM_LD][WB_ICNT_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[g_mem_ld][wb_rsrv_fail] = %d\n", gpu_stall_shd_mem_breakdown[G_MEM_LD][WB_CACHE_RSRV_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[g_mem_st][mshr_rc] = %d\n", gpu_stall_shd_mem_breakdown[G_MEM_ST][MSHR_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[g_mem_st][icnt_rc] = %d\n", gpu_stall_shd_mem_breakdown[G_MEM_ST][ICNT_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[g_mem_st][wb_icnt_rc] = %d\n", gpu_stall_shd_mem_breakdown[G_MEM_ST][WB_ICNT_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[g_mem_st][wb_rsrv_fail] = %d\n", gpu_stall_shd_mem_breakdown[G_MEM_ST][WB_CACHE_RSRV_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[l_mem_ld][mshr_rc] = %d\n", gpu_stall_shd_mem_breakdown[L_MEM_LD][MSHR_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[l_mem_ld][icnt_rc] = %d\n", gpu_stall_shd_mem_breakdown[L_MEM_LD][ICNT_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[l_mem_ld][wb_icnt_rc] = %d\n", gpu_stall_shd_mem_breakdown[L_MEM_LD][WB_ICNT_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[l_mem_ld][wb_rsrv_fail] = %d\n", gpu_stall_shd_mem_breakdown[L_MEM_LD][WB_CACHE_RSRV_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[l_mem_st][mshr_rc] = %d\n", gpu_stall_shd_mem_breakdown[L_MEM_ST][MSHR_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[l_mem_st][icnt_rc] = %d\n", gpu_stall_shd_mem_breakdown[L_MEM_ST][ICNT_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[l_mem_ld][wb_icnt_rc] = %d\n", gpu_stall_shd_mem_breakdown[L_MEM_ST][WB_ICNT_RC_FAIL]);
   fprintf(fout, "gpgpu_stall_shd_mem[l_mem_ld][wb_rsrv_fail] = %d\n", gpu_stall_shd_mem_breakdown[L_MEM_ST][WB_CACHE_RSRV_FAIL]);

   fprintf(fout, "gpu_reg_bank_conflict_stalls = %d\n", gpu_reg_bank_conflict_stalls);

   // Thread state profiling
   unsigned long long tot_cycles = gpgpu_global_thread_state_atomic->tot_cycles() +
                                     gpgpu_global_thread_state_nonatomic->tot_cycles() +
                                     gpgpu_global_thread_state_tx_useful->tot_cycles() +
                                     gpgpu_global_thread_state_tx_aborted->tot_cycles();
   unsigned long long commit_waiting_cycles = gpgpu_global_thread_state_atomic->commit_waiting_cycles() +
                                              gpgpu_global_thread_state_nonatomic->commit_waiting_cycles() +
                                              gpgpu_global_thread_state_tx_useful->commit_waiting_cycles() +
                                              gpgpu_global_thread_state_tx_aborted->commit_waiting_cycles();
   unsigned long long concurrency_some_committing_cycles = gpgpu_global_thread_state_atomic->concurrency_some_committing_cycles() +
                                                           gpgpu_global_thread_state_nonatomic->concurrency_some_committing_cycles() +
                                                           gpgpu_global_thread_state_tx_useful->concurrency_some_committing_cycles() +
                                                           gpgpu_global_thread_state_tx_aborted->concurrency_some_committing_cycles();
   unsigned long long concurrency_none_committing_cycles = gpgpu_global_thread_state_atomic->concurrency_none_committing_cycles() +
                                                           gpgpu_global_thread_state_nonatomic->concurrency_none_committing_cycles() +
                                                           gpgpu_global_thread_state_tx_useful->concurrency_none_committing_cycles() +
                                                           gpgpu_global_thread_state_tx_aborted->concurrency_none_committing_cycles();
   unsigned long long other_useful_cycles = tot_cycles - 
	                                    commit_waiting_cycles - 
				            concurrency_some_committing_cycles -
				            concurrency_none_committing_cycles;
   gpgpu_global_thread_state_nonatomic->print(fout, tot_cycles);
   gpgpu_global_thread_state_atomic->print(fout, tot_cycles);
   gpgpu_global_thread_state_tx_useful->print(fout, tot_cycles);
   gpgpu_global_thread_state_tx_aborted->print(fout, tot_cycles);
   fprintf(fout, "gpgpu_global_thread_states_tot_cycles = %llu\n", tot_cycles );
   fprintf(fout, "gpgpu_global_hw_thread_idle_cycles = %llu\n", gpgpu_global_hw_thread_idle_cycles);
   fprintf(fout, "gpgpu_global_thread_states_other_useful_cycles = %llu\n", other_useful_cycles);
   fprintf(fout, "gpgpu_global_thread_states_commit_waiting_cycles = %llu\n", commit_waiting_cycles);
   fprintf(fout, "gpgpu_global_thread_states_concurrency_some_committing_cycles = %llu\n", concurrency_some_committing_cycles);
   fprintf(fout, "gpgpu_global_thread_states_concurrency_none_committing_cycles = %llu\n", concurrency_none_committing_cycles);
   fprintf(fout, "gpgpu_global_threads = %u\n", gpgpu_global_threads);
   fprintf(fout, "gpgpu_global_abort_cost = %llu\n", gpgpu_global_thread_state_tx_aborted->tot_cycles() );

   unsigned long long no_waiting_abort_cost = gpgpu_global_thread_state_tx_aborted->tot_cycles() -
	                                      gpgpu_global_thread_state_tx_aborted->m_state_cycles[WAIT_CONCCONTROL_SOME_COMMIT] -
	                                      gpgpu_global_thread_state_tx_aborted->m_state_cycles[WAIT_CONCCONTROL_NONE_COMMIT];
   fprintf(fout, "gpgpu_global_no_waiting_abort_cost = %llu\n", no_waiting_abort_cost );

   unsigned long long tot_tx_exec_cycles = gpgpu_global_thread_state_tx_useful->tot_cycles() + 
	                                   gpgpu_global_thread_state_tx_aborted->tot_cycles();
   fprintf(fout, "gpgpu_global_tot_tx_exec_cycles = %llu\n", tot_tx_exec_cycles );

   unsigned long long tot_no_waiting_tx_exec_cycles = no_waiting_abort_cost + 
	                                              gpgpu_global_thread_state_tx_useful->tot_cycles() -
						      gpgpu_global_thread_state_tx_useful->m_state_cycles[WAIT_CONCCONTROL_SOME_COMMIT] -
						      gpgpu_global_thread_state_tx_useful->m_state_cycles[WAIT_CONCCONTROL_NONE_COMMIT];
   fprintf(fout, "gpgpu_global_tot_no_waiting_tx_exec_cycles = %llu\n", tot_no_waiting_tx_exec_cycles );

   unsigned long long tot_waiting_cost = gpgpu_global_thread_state_atomic->m_state_cycles[WAIT_CONCCONTROL_SOME_COMMIT] +
	                                 gpgpu_global_thread_state_atomic->m_state_cycles[WAIT_CONCCONTROL_NONE_COMMIT] +
	                                 gpgpu_global_thread_state_atomic->m_state_cycles[WAIT_CONTFLOWDIV_COMMITEXIT] +
	                                 gpgpu_global_thread_state_nonatomic->m_state_cycles[WAIT_CONCCONTROL_SOME_COMMIT] +
	                                 gpgpu_global_thread_state_nonatomic->m_state_cycles[WAIT_CONCCONTROL_NONE_COMMIT] +
	                                 gpgpu_global_thread_state_nonatomic->m_state_cycles[WAIT_CONTFLOWDIV_COMMITEXIT];
   fprintf(fout, "gpgpu_global_tot_waiting_cost = %llu\n", tot_waiting_cost );

   fprintf(fout, "gpgpu_n_cycle_shd_inactive = %d\n", gpgpu_n_cycle_shd_inactive);
   fprintf(fout, "Warp Occupancy Distribution:\n");
   fprintf(fout, "Core_Stall = %d\n", shader_cycle_distro[2]);
   fprintf(fout, "Core_W0_Idle = %d\n", shader_cycle_distro[0]);
   fprintf(fout, "Core_W0_Scoreboard = %d\n", shader_cycle_distro[1]);
   unsigned shader_exec_cycles = 0;
   for (unsigned i = 3; i < m_config->warp_size + 3; i++) {
      shader_exec_cycles += shader_cycle_distro[i];
      fprintf(fout, "W%d:%d\t", i-2, shader_cycle_distro[i]);
   }
   fprintf(fout, "\n");
   fprintf(fout, "Core_Execution = %d\n", shader_exec_cycles);

   m_outgoing_traffic_stats->print(fout); 
   m_incoming_traffic_stats->print(fout); 
}

void shader_core_stats::event_warp_issued( unsigned s_id, unsigned warp_id, unsigned num_issued, unsigned dynamic_warp_id ) {
    assert( warp_id <= m_config->max_warps_per_shader );
    for ( unsigned i = 0; i < num_issued; ++i ) {
        if ( m_shader_dynamic_warp_issue_distro[ s_id ].size() <= dynamic_warp_id ) {
            m_shader_dynamic_warp_issue_distro[ s_id ].resize(dynamic_warp_id + 1);
        }
        ++m_shader_dynamic_warp_issue_distro[ s_id ][ dynamic_warp_id ];
        if ( m_shader_warp_slot_issue_distro[ s_id ].size() <= warp_id ) {
            m_shader_warp_slot_issue_distro[ s_id ].resize(warp_id + 1);
        }
        ++m_shader_warp_slot_issue_distro[ s_id ][ warp_id ];
    }
}

void shader_core_stats::visualizer_print( gzFile visualizer_file )
{
    // warp divergence breakdown
    gzprintf(visualizer_file, "WarpDivergenceBreakdown:");
    unsigned int total=0;
    unsigned int cf = (m_config->gpgpu_warpdistro_shader==-1)?m_config->num_shader():1;
    gzprintf(visualizer_file, " %d", (shader_cycle_distro[0] - last_shader_cycle_distro[0]) / cf );
    gzprintf(visualizer_file, " %d", (shader_cycle_distro[1] - last_shader_cycle_distro[1]) / cf );
    gzprintf(visualizer_file, " %d", (shader_cycle_distro[2] - last_shader_cycle_distro[2]) / cf );
    for (unsigned i=0; i<m_config->warp_size+3; i++) {
       if ( i>=3 ) {
          total += (shader_cycle_distro[i] - last_shader_cycle_distro[i]);
          if ( ((i-3) % (m_config->warp_size/8)) == ((m_config->warp_size/8)-1) ) {
             gzprintf(visualizer_file, " %d", total / cf );
             total=0;
          }
       }
       last_shader_cycle_distro[i] = shader_cycle_distro[i];
    }
    gzprintf(visualizer_file,"\n");

    // warp issue breakdown
    unsigned sid = m_config->gpgpu_warp_issue_shader;
    unsigned count = 0;
    unsigned warp_id_issued_sum = 0;
    gzprintf(visualizer_file, "WarpIssueSlotBreakdown:");
    if(m_shader_warp_slot_issue_distro[sid].size() > 0){
        for ( std::vector<unsigned>::const_iterator iter = m_shader_warp_slot_issue_distro[ sid ].begin();
              iter != m_shader_warp_slot_issue_distro[ sid ].end(); iter++, count++ ) {
            unsigned diff = count < m_last_shader_warp_slot_issue_distro.size() ?
                            *iter - m_last_shader_warp_slot_issue_distro[ count ] :
                            *iter;
            gzprintf( visualizer_file, " %d", diff );
            warp_id_issued_sum += diff;
        }
        m_last_shader_warp_slot_issue_distro = m_shader_warp_slot_issue_distro[ sid ];
    }else{
        gzprintf( visualizer_file, " 0");
    }
    gzprintf(visualizer_file,"\n");

    #define DYNAMIC_WARP_PRINT_RESOLUTION 32
    unsigned total_issued_this_resolution = 0;
    unsigned dynamic_id_issued_sum = 0;
    count = 0;
    gzprintf(visualizer_file, "WarpIssueDynamicIdBreakdown:");
    if(m_shader_dynamic_warp_issue_distro[sid].size() > 0){
        for ( std::vector<unsigned>::const_iterator iter = m_shader_dynamic_warp_issue_distro[ sid ].begin();
              iter != m_shader_dynamic_warp_issue_distro[ sid ].end(); iter++, count++ ) {
            unsigned diff = count < m_last_shader_dynamic_warp_issue_distro.size() ?
                            *iter - m_last_shader_dynamic_warp_issue_distro[ count ] :
                            *iter;
            total_issued_this_resolution += diff;
            if ( ( count + 1 ) % DYNAMIC_WARP_PRINT_RESOLUTION == 0 ) {
                gzprintf( visualizer_file, " %d", total_issued_this_resolution );
                dynamic_id_issued_sum += total_issued_this_resolution;
                total_issued_this_resolution = 0;
            }
        }
        if ( count % DYNAMIC_WARP_PRINT_RESOLUTION != 0 ) {
            gzprintf( visualizer_file, " %d", total_issued_this_resolution );
            dynamic_id_issued_sum += total_issued_this_resolution;
        }
        m_last_shader_dynamic_warp_issue_distro = m_shader_dynamic_warp_issue_distro[ sid ];
        assert( warp_id_issued_sum == dynamic_id_issued_sum );
    }else{
        gzprintf( visualizer_file, " 0");
    }
    gzprintf(visualizer_file,"\n");

    // overall cache miss rates
    gzprintf(visualizer_file, "gpgpu_n_cache_bkconflict: %d\n", gpgpu_n_cache_bkconflict);
    gzprintf(visualizer_file, "gpgpu_n_shmem_bkconflict: %d\n", gpgpu_n_shmem_bkconflict);     


   // instruction count per shader core
   gzprintf(visualizer_file, "shaderinsncount:  ");
   for (unsigned i=0;i<m_config->num_shader();i++) 
      gzprintf(visualizer_file, "%u ", m_num_sim_insn[i] );
   gzprintf(visualizer_file, "\n");
   // warp instruction count per shader core
   gzprintf(visualizer_file, "shaderwarpinsncount:  ");
   for (unsigned i=0;i<m_config->num_shader();i++)
      gzprintf(visualizer_file, "%u ", m_num_sim_winsn[i] );
   gzprintf(visualizer_file, "\n");
   // warp divergence per shader core
   gzprintf(visualizer_file, "shaderwarpdiv: ");
   for (unsigned i=0;i<m_config->num_shader();i++) 
      gzprintf(visualizer_file, "%u ", m_n_diverge[i] );
   gzprintf(visualizer_file, "\n");
}

#define PROGRAM_MEM_START 0xF0000000 /* should be distinct from other memory spaces... 
                                        check ptx_ir.h to verify this does not overlap 
                                        other memory spaces */
void shader_core_ctx::decode()
{
    if( m_inst_fetch_buffer.m_valid ) {
        // decode 1 or 2 instructions and place them into ibuffer
        address_type pc = m_inst_fetch_buffer.m_pc;
        const warp_inst_t* pI1 = ptx_fetch_inst(pc);
        m_warp[m_inst_fetch_buffer.m_warp_id].ibuffer_fill(0,pI1);
        m_warp[m_inst_fetch_buffer.m_warp_id].inc_inst_in_pipeline();
        if( pI1 ) {
            m_stats->m_num_decoded_insn[m_sid]++;
            if(pI1->oprnd_type==INT_OP){
                m_stats->m_num_INTdecoded_insn[m_sid]++;
            }else if(pI1->oprnd_type==FP_OP) {
            	m_stats->m_num_FPdecoded_insn[m_sid]++;
            }
           const warp_inst_t* pI2 = ptx_fetch_inst(pc+pI1->isize);
           if( pI2 ) {
               m_warp[m_inst_fetch_buffer.m_warp_id].ibuffer_fill(1,pI2);
               m_warp[m_inst_fetch_buffer.m_warp_id].inc_inst_in_pipeline();
               m_stats->m_num_decoded_insn[m_sid]++;
               if(pI2->oprnd_type==INT_OP){
                   m_stats->m_num_INTdecoded_insn[m_sid]++;
               }else if(pI2->oprnd_type==FP_OP) {
            	   m_stats->m_num_FPdecoded_insn[m_sid]++;
               }
           }
        }
        m_inst_fetch_buffer.m_valid = false;
    }
}

void shader_core_ctx::fetch()
{
    if( !m_inst_fetch_buffer.m_valid ) {
        // find an active warp with space in instruction buffer that is not already waiting on a cache miss
        // and get next 1-2 instructions from i-cache...
        for( unsigned i=0; i < m_config->max_warps_per_shader; i++ ) {
            unsigned warp_id = (m_last_warp_fetched+1+i) % m_config->max_warps_per_shader;

            // this code checks if this warp has finished executing and can be reclaimed
            if( m_warp[warp_id].hardware_done() && !m_scoreboard->pendingWrites(warp_id) && !m_warp[warp_id].done_exit() ) {
                bool did_exit=false;
                for( unsigned t=0; t<m_config->warp_size;t++) {
                    unsigned tid=warp_id*m_config->warp_size+t;
                    if( m_threadState[tid].m_active == true ) {
                        m_threadState[tid].m_active = false;
                        m_threadState[tid].m_timeout_validation_cycle = 0;
                        m_threadState[tid].m_timeout_validation_mode = false;
                        m_threadState[tid].m_timeout_validation_mode_set = false;
                        assert(m_threadState[tid].m_in_pipeline == 0);

                        unsigned cta_id = m_warp[warp_id].get_cta_id();
                        register_cta_thread_exit(cta_id);
                        m_not_completed -= 1;
                        m_active_threads.reset(tid);
                        assert( m_thread[tid]!= NULL );
                        did_exit=true;

                        // Thread state profiling code
                        // Validate the counted states
                        if(m_config->thread_state_profiling) {
                           unsigned long long thread_counted_states = m_threadState[tid].m_state_cycles_atomic.tot_cycles() +
                                                                        m_threadState[tid].m_state_cycles_nonatomic.tot_cycles() +
                                                                        m_threadState[tid].m_state_cycles_tx_useful.tot_cycles() +
                                                                        m_threadState[tid].m_state_cycles_tx_aborted.tot_cycles();
                           unsigned long long thread_lifetime = gpu_tot_sim_cycle + gpu_sim_cycle - m_threadState[tid].m_active_cycle;
                           assert( thread_counted_states == thread_lifetime );
                           m_stats->gpgpu_global_thread_state_nonatomic->add_stats( m_threadState[tid].m_state_cycles_nonatomic );
                           m_stats->gpgpu_global_thread_state_atomic->add_stats( m_threadState[tid].m_state_cycles_atomic );
                           m_stats->gpgpu_global_thread_state_tx_useful->add_stats( m_threadState[tid].m_state_cycles_tx_useful );
                           m_stats->gpgpu_global_thread_state_tx_aborted->add_stats( m_threadState[tid].m_state_cycles_tx_aborted );
                           m_stats->gpgpu_global_threads++;
                           m_threadState[tid].m_state_cycles_nonatomic.reset();
                           m_threadState[tid].m_state_cycles_atomic.reset();
                           m_threadState[tid].m_state_cycles_tx_useful.reset();
                           m_threadState[tid].m_state_cycles_tx_aborted.reset();
                           assert(m_threadState[tid].m_atomic_mode == false);
                        }
                    }
                }
                if( did_exit ) 
                    m_warp[warp_id].set_done_exit();
            }

            // this code fetches instructions from the i-cache or generates memory requests
            if( !m_warp[warp_id].functional_done() && !m_warp[warp_id].imiss_pending() && m_warp[warp_id].ibuffer_empty() ) {
                address_type pc  = m_warp[warp_id].get_pc();
                address_type ppc = pc + PROGRAM_MEM_START;
                unsigned nbytes=16; 
                unsigned offset_in_block = pc & (m_config->m_L1I_config.get_line_sz()-1);
                if( (offset_in_block+nbytes) > m_config->m_L1I_config.get_line_sz() )
                    nbytes = (m_config->m_L1I_config.get_line_sz()-offset_in_block);

                bool perfect_L1I = false; 
                if (perfect_L1I) {
                    // skip the L1 instruction cache and fill the ibuffer directly
                    m_inst_fetch_buffer = ifetch_buffer_t(pc,nbytes,warp_id);
                    m_warp[warp_id].set_last_fetch(gpu_sim_cycle);
                } else {
                    // TODO: replace with use of allocator
                    // mem_fetch *mf = m_mem_fetch_allocator->alloc()
                    mem_access_t acc(INST_ACC_R,ppc,nbytes,false);
                    mem_fetch *mf = new mem_fetch(acc,
                                                  NULL/*we don't have an instruction yet*/,
                                                  READ_PACKET_SIZE,
                                                  warp_id,
                                                  m_sid,
                                                  m_tpc,
                                                  m_memory_config );
                    std::list<cache_event> events;
                    enum cache_request_status status = m_L1I->access( (new_addr_type)ppc, mf, gpu_sim_cycle+gpu_tot_sim_cycle,events);
                    if( status == MISS ) {
                        m_last_warp_fetched=warp_id;
                        m_warp[warp_id].set_imiss_pending();
                        m_warp[warp_id].set_last_fetch(gpu_sim_cycle);
                    } else if( status == HIT ) {
                        m_last_warp_fetched=warp_id;
                        m_inst_fetch_buffer = ifetch_buffer_t(pc,nbytes,warp_id);
                        m_warp[warp_id].set_last_fetch(gpu_sim_cycle);
                        delete mf;
                    } else {
                        m_last_warp_fetched=warp_id;
                        assert( status == RESERVATION_FAIL );
                        delete mf;
                    }
                }
                break;
            }
        }
    }

    m_L1I->cycle();

    if( m_L1I->access_ready() ) {
        mem_fetch *mf = m_L1I->next_access();
        m_warp[mf->get_wid()].clear_imiss_pending();
        delete mf;
    }
}

void shader_core_ctx::func_exec_inst( warp_inst_t &inst )
{
    inst.m_tm_access_info.reset(); // reset TM access info for the warp instruction 

    execute_warp_inst_t(inst);

    if (inst.is_warp_level) {
        if (inst.is_tbegin and m_config->tm_warp_level_gmem_view) {
            tm_manager_inf *warp_tm_manager = NULL;  
            for (unsigned t = 0; t < m_config->warp_size; t++) {
                if (inst.active(t) == false) continue; 
                unsigned tid=m_config->warp_size*inst.warp_id()+t;
                tm_manager_inf *t_tm_manager = get_func_thread_info(tid)->get_tm_manager(); 
                if (warp_tm_manager == NULL) {
                    warp_tm_manager = t_tm_manager; 
                }
                t_tm_manager->share_gmem_view(warp_tm_manager); 
            }
        }
        if( inst.is_load() || inst.is_store() ) {
            if(inst.m_tm_access_info.m_timeout_validation_fail.any()) {
                // abort every transaction in this warp if any of them are aborting 
                for (unsigned t = 0; t < m_config->warp_size; t++) {
                    unsigned tid=m_config->warp_size*inst.warp_id()+t;
                    tm_manager_inf *t_tm_manager = get_func_thread_info(tid)->get_tm_manager(); 
                    if (t_tm_manager == NULL) continue; 
                    // call abort for the ones that have not yet failed timeout validation 
                    if (inst.m_tm_access_info.m_timeout_validation_fail.test(t) == false) {
                        t_tm_manager->abort(); 
	                if (t_tm_manager->get_is_abort_need_clean() == false)
		            inst.set_not_issued(t); 
                    }
                    // TM-HACK: not sure if needed 
                    m_threadState[tid].m_in_pipeline = 0; // set this to inform the fetch stage 
                    m_threadState[tid].m_timeout_validation_mode = true;
                    m_threadState[tid].m_timeout_validation_mode_set = true;
                    m_threadState[tid].m_timeout_validation_cycle = gpu_sim_cycle + gpu_tot_sim_cycle;
                }
                // TM-HACK: set the active mask again if a thread failed timeout validation 
                inst.clear_active(inst.get_active_mask());
                // Check for potential errors 
                assert(inst.active_count() == 0);
                assert(m_simt_stack[inst.warp_id()]->get_type() == simt_stack::STACK_ENTRY_TYPE_TRANS); 
                m_simt_stack[inst.warp_id()]->check_txrestart_warp_level(); 
            }
        }

        // warp-level transaction rollback 
        if( inst.is_trollback and inst.is_warp_level ) {
            for (unsigned t = 0; t < m_config->warp_size; t++) {
                unsigned tid=m_config->warp_size*inst.warp_id()+t;
                tm_manager_inf *t_tm_manager = get_func_thread_info(tid)->get_tm_manager(); 
                if (t_tm_manager == NULL) continue; 
                t_tm_manager->abort(); 
	        if (t_tm_manager->get_is_abort_need_clean() == false)
		    inst.set_not_issued(t); 
            }
            m_simt_stack[inst.warp_id()]->check_txrestart_warp_level(); 
        }

        // warp-level transaction commit for ideal TM 
        if (m_config->timing_mode_vb_commit == false) {
            if( inst.is_tcommit and inst.is_warp_level ) {
                bool all_pass = true;
	        if (m_config->tlw_use_logical_temporal_cd) {
                    unsigned index = m_sid * (m_config->max_warps_per_shader) + inst.warp_id();
	            all_pass = !(logical_temporal_conflict_detector::get_singleton().warp_level_conflict_exist(index));
		} else {	
                    for (unsigned t = 0; t < m_config->warp_size; t++) {
                        if (inst.active(t) == false) continue; 
                        unsigned tid=m_config->warp_size*inst.warp_id()+t;
                        tm_manager_inf *t_tm_manager = get_func_thread_info(tid)->get_tm_manager(); 
                        bool pass = t_tm_manager->validate_all(false); // Always use value-based validation?
                        if (not pass) all_pass = false; 
                    }
		}
                for (unsigned t = 0; t < m_config->warp_size; t++) {
                    if (inst.active(t) == false) continue; 
                    unsigned tid=m_config->warp_size*inst.warp_id()+t;
                    if (all_pass) {
                       bool commit_success = m_thread[tid]->tx_commit(NULL, true); 
                       assert(commit_success == true); 
                    } else {
                       tm_manager_inf *t_tm_manager = get_func_thread_info(tid)->get_tm_manager(); 
                       t_tm_manager->abort();
	               if (t_tm_manager->get_is_abort_need_clean() == false)
		           inst.set_not_issued(t); 
                    }
                }
                if (all_pass == false) {
                    m_simt_stack[inst.warp_id()]->check_txrestart_warp_level(); 
                } else {
                    m_simt_stack[inst.warp_id()]->check_txcommit_warp_level(); 
                }
            }
        }
    }

    if( inst.is_load() || inst.is_store() ) {
        // TM-HACK: set the active mask again if a thread failed timeout validation 
        if (inst.is_warp_level == false) {
           inst.clear_active(inst.m_tm_access_info.m_timeout_validation_fail);
           if(inst.m_tm_access_info.m_timeout_validation_fail.any()) {
              for(unsigned t=0; t<m_config->warp_size; t++) {
                 if(inst.m_tm_access_info.m_timeout_validation_fail.test(t)) {
                    unsigned tid=m_config->warp_size*inst.warp_id()+t;
                    m_threadState[tid].m_in_pipeline = 0;
                    m_threadState[tid].m_timeout_validation_mode = true;
                    m_threadState[tid].m_timeout_validation_mode_set = true;
                    m_threadState[tid].m_timeout_validation_cycle = gpu_sim_cycle + gpu_tot_sim_cycle;
                 }
              }
           }
        }
        inst.generate_mem_accesses(m_sid, m_warp[inst.warp_id()].get_tm_warp_info());
    }

    // Thread profiling
    if(inst.is_load()) {
       for ( unsigned t=0; t < m_config->warp_size; t++ ) {
          if( inst.active(t) ) {
             unsigned tid=m_config->warp_size*inst.warp_id()+t;
             m_threadState[tid].m_inflight_mem_accesses += 1;
          }
       }
    }
}

active_mask_t shader_core_ctx::warps_need_clean_num_writing(unsigned wid) {
    active_mask_t warps_need_clean = active_mask_t((unsigned long long)0);
    for (unsigned t = 0; t < m_config->warp_size; t++) {
        int tid = wid * (m_config->warp_size) + t; 
        tm_manager_inf *thread_tm_manager = get_func_thread_info(tid)->get_tm_manager(); 
	if (thread_tm_manager == NULL) continue;
	if (thread_tm_manager->get_is_abort_need_clean()) {
	    assert(m_config->tlw_use_logical_temporal_cd);
	    warps_need_clean.set(t);
	}
    } 
    return warps_need_clean;
}

void shader_core_ctx::issue_warp_dummy_commit( register_set& pipe_reg_set, const warp_inst_t* next_inst, const active_mask_t &active_mask, unsigned warp_id )
{
    warp_inst_t** pipe_reg = pipe_reg_set.get_free();
    assert(pipe_reg);
    **pipe_reg = *next_inst; // static instruction information
    assert((*pipe_reg)->is_tcommit);
    active_mask_t warps_need_clean = warps_need_clean_num_writing(warp_id);
    assert((active_mask & warps_need_clean).none()); // warps need clean must have been aborted;
    active_mask_t issued_mask = active_mask | warps_need_clean;
    (*pipe_reg)->issue( active_mask, issued_mask, warp_id, gpu_tot_sim_cycle + gpu_sim_cycle, m_warp[warp_id].get_dynamic_warp_id() ); // dynamic instruction information
    m_scoreboard->startTxCommit(warp_id);
    m_scoreboard->doneTxRestart(warp_id);
    for ( unsigned t = 0; t < m_config->warp_size; t++ ) {
       unsigned tid = m_config->warp_size * warp_id + t;
       if ((*pipe_reg)->issued(t)) {
          tm_manager_inf *thread_tm_manager = get_func_thread_info(tid)->get_tm_manager();
          assert(thread_tm_manager != NULL); 
          assert(thread_tm_manager->get_is_abort_need_clean());
          m_threadState[tid].m_active_in_cleaning = true;
       }
    }
    g_tm_global_statistics.m_n_dummy_commits++;
}

void shader_core_ctx::issue_warp( register_set& pipe_reg_set, const warp_inst_t* next_inst, const active_mask_t &active_mask, unsigned warp_id )
{
    warp_inst_t** pipe_reg = pipe_reg_set.get_free();
    assert(pipe_reg);
    
    m_warp[warp_id].ibuffer_free();
    assert(next_inst->valid());
    **pipe_reg = *next_inst; // static instruction information
    active_mask_t issued_mask = active_mask_t((unsigned long long)0);
    if ((*pipe_reg)->is_tcommit) {
        active_mask_t warps_need_clean = warps_need_clean_num_writing(warp_id);
        assert((active_mask & warps_need_clean).none()); // warps need clean must have been aborted;
        issued_mask = active_mask | warps_need_clean;
    } else {
        issued_mask = active_mask;
    }
    (*pipe_reg)->issue( active_mask, issued_mask, warp_id, gpu_tot_sim_cycle + gpu_sim_cycle, m_warp[warp_id].get_dynamic_warp_id() ); // dynamic instruction information
    m_stats->shader_cycle_distro[2+(*pipe_reg)->active_count()]++;

    func_exec_inst( **pipe_reg );
    if( next_inst->op == BARRIER_OP ) 
        m_barriers.warp_reaches_barrier(m_warp[warp_id].get_cta_id(),warp_id);
    else if( next_inst->op == MEMORY_BARRIER_OP ) 
        m_warp[warp_id].set_membar();

    assert(warp_id == (*pipe_reg)->warp_id()); 
    if ((*pipe_reg)->is_tcommit == true) {
        m_scoreboard->startTxCommit(warp_id);
	m_scoreboard->doneTxRestart(warp_id);
	
        g_tm_global_statistics.m_n_real_commits++;
	
	// Thread profiler code, mark these threads as in commit
        for ( unsigned t = 0; t < m_config->warp_size; t++ ) {
           unsigned tid = m_config->warp_size * warp_id + t;
           if( (*pipe_reg)->active(t) ) {
              m_threadState[tid].m_active_in_commit = true;
           } else if (m_config->tlw_use_logical_temporal_cd) {
	      if ((*pipe_reg)->issued(t)) {
                 tm_manager_inf *thread_tm_manager = get_func_thread_info(tid)->get_tm_manager();
	         assert(thread_tm_manager != NULL); 
	         assert(thread_tm_manager->get_is_abort_need_clean());
                 m_threadState[tid].m_active_in_cleaning = true;
	      }
	   }
        }
    }
    #if 0  // TM-Depricated
    if ( m_config->tm_uarch_model >= 20 && pipe_reg->is_tcommit == true ) {
        unsigned trans_wid = pipe_reg->warp_id(); 
        initiate_timing_model_transaction_commit( trans_wid ); 
    }
    #endif

    updateSIMTStack(warp_id,*pipe_reg);
    m_scoreboard->reserveRegisters(*pipe_reg);
    m_warp[warp_id].set_next_pc(next_inst->pc + next_inst->isize);

    // Profiling - mark threads as in pipeline
    for ( unsigned t=0; t < m_config->warp_size; t++ ) {
       if( (*pipe_reg)->active(t) ) {
          unsigned tid = m_config->warp_size * warp_id + t;
          m_threadState[tid].m_in_pipeline += 1;
          m_threadState[tid].m_timeout_validation_mode = false;
          if((*pipe_reg)->isatomic()) {
             m_threadState[tid].m_atomic_mode = true;
          }
       }
    }
}

void shader_core_ctx::issue(){
    //really is issue;
    for (unsigned i = 0; i < schedulers.size(); i++) {
        schedulers[i]->cycle();
    }
}

shd_warp_t& scheduler_unit::warp(int i){
    return (*m_warp)[i];
}


/**
 * A general function to order things in a Loose Round Robin way. The simplist use of this
 * function would be to implement a loose RR scheduler between all the warps assigned to this core.
 * A more sophisticated usage would be to order a set of "fetch groups" in a RR fashion.
 * In the first case, the templated class variable would be a simple unsigned int representing the
 * warp_id.  In the 2lvl case, T could be a struct or a list representing a set of warp_ids.
 * @param result_list: The resultant list the caller wants returned.  This list is cleared and then populated
 *                     in a loose round robin way
 * @param input_list: The list of things that should be put into the result_list. For a simple scheduler
 *                    this can simply be the m_supervised_warps list.
 * @param last_issued_from_input:  An iterator pointing the last member in the input_list that issued.
 *                                 Since this function orders in a RR fashion, the object pointed
 *                                 to by this iterator will be last in the prioritization list
 * @param num_warps_to_add: The number of warps you want the scheudler to pick between this cycle.
 *                          Normally, this will be all the warps availible on the core, i.e.
 *                          m_supervised_warps.size(). However, a more sophisticated scheduler may wish to
 *                          limit this number. If the number if < m_supervised_warps.size(), then only
 *                          the warps with highest RR priority will be placed in the result_list.
 */
template < class T >
void scheduler_unit::order_lrr( std::vector< T >& result_list,
                                const typename std::vector< T >& input_list,
                                const typename std::vector< T >::const_iterator& last_issued_from_input,
                                unsigned num_warps_to_add )
{
    assert( num_warps_to_add <= input_list.size() );
    result_list.clear();
    typename std::vector< T >::const_iterator iter
        = ( last_issued_from_input ==  input_list.end() ) ? input_list.begin()
                                                          : last_issued_from_input + 1;

    for ( unsigned count = 0;
          count < num_warps_to_add;
          ++iter, ++count) {
        if ( iter ==  input_list.end() ) {
            iter = input_list.begin();
        }
        result_list.push_back( *iter );
    }
}

/**
 * A general function to order things in an priority-based way.
 * The core usage of the function is similar to order_lrr.
 * The explanation of the additional parameters (beyond order_lrr) explains the further extensions.
 * @param ordering: An enum that determines how the age function will be treated in prioritization
 *                  see the definition of OrderingType.
 * @param priority_function: This function is used to sort the input_list.  It is passed to stl::sort as
 *                           the sorting fucntion. So, if you wanted to sort a list of integer warp_ids
 *                           with the oldest warps having the most priority, then the priority_function
 *                           would compare the age of the two warps.
 */
template < class T >
void scheduler_unit::order_by_priority( std::vector< T >& result_list,
                                        const typename std::vector< T >& input_list,
                                        const typename std::vector< T >::const_iterator& last_issued_from_input,
                                        unsigned num_warps_to_add,
                                        OrderingType ordering,
                                        bool (*priority_func)(T lhs, T rhs) )
{
    assert( num_warps_to_add <= input_list.size() );
    result_list.clear();
    typename std::vector< T > temp = input_list;

    if ( ORDERING_GREEDY_THEN_PRIORITY_FUNC == ordering ) {
        T greedy_value = *last_issued_from_input;
        result_list.push_back( greedy_value );

        std::sort( temp.begin(), temp.end(), priority_func );
        typename std::vector< T >::iterator iter = temp.begin();
        for ( unsigned count = 0; count < num_warps_to_add; ++count, ++iter ) {
            if ( *iter != greedy_value ) {
                result_list.push_back( *iter );
            }
        }
    } else if ( ORDERED_PRIORITY_FUNC_ONLY == ordering ) {
        std::sort( temp.begin(), temp.end(), priority_func );
        typename std::vector< T >::iterator iter = temp.begin();
        for ( unsigned count = 0; count < num_warps_to_add; ++count, ++iter ) {
            result_list.push_back( *iter );
        }
    } else {
        fprintf( stderr, "Unknown ordering - %d\n", ordering );
        abort();
    }
}

void scheduler_unit::cycle()
{
    SCHED_DPRINTF( "scheduler_unit::cycle()\n" );
    bool valid_inst = false;  // there was one warp with a valid instruction to issue (didn't require flush due to control hazard)
    bool ready_inst = false;  // of the valid instructions, there was one not waiting for pending register writes
    bool issued_inst = false; // of these we issued one

    order_warps();
    for ( std::vector< shd_warp_t* >::const_iterator iter = m_next_cycle_prioritized_warps.begin();
          iter != m_next_cycle_prioritized_warps.end();
          iter++ ) {
        // Don't consider warps that are not yet valid
        if ( (*iter) == NULL || (*iter)->done_exit() ) {
            continue;
        }
        SCHED_DPRINTF( "Testing (warp_id %u, dynamic_warp_id %u)\n",
                       (*iter)->get_warp_id(), (*iter)->get_dynamic_warp_id() );
        unsigned warp_id = (*iter)->get_warp_id();
        unsigned checked=0;
        unsigned issued=0;
        unsigned max_issue = m_shader->m_config->gpgpu_max_insn_issue_per_warp;
        while( !warp(warp_id).waiting() && !warp(warp_id).ibuffer_empty() && (checked < max_issue) && (checked <= issued) && (issued < max_issue) ) {
            const warp_inst_t *pI = warp(warp_id).ibuffer_next_inst();
            bool valid = warp(warp_id).ibuffer_next_valid();
            bool warp_inst_issued = false;

            unsigned pc,rpc;
            m_simt_stack[warp_id]->get_pdom_stack_top_info(&pc,&rpc);
            SCHED_DPRINTF( "Warp (warp_id %u, dynamic_warp_id %u) has valid instruction (%s)\n",
                           (*iter)->get_warp_id(), (*iter)->get_dynamic_warp_id(),
                           ptx_get_insn_str( pc).c_str() );
            if( pI ) {
                assert(valid);
                if( pc != pI->pc ) {
                    SCHED_DPRINTF( "Warp (warp_id %u, dynamic_warp_id %u) control hazard instruction flush\n",
                                   (*iter)->get_warp_id(), (*iter)->get_dynamic_warp_id() );
                    // control hazard
                    warp(warp_id).set_next_pc(pc);
                    warp(warp_id).ibuffer_flush();
                } else {
                    valid_inst = true;
                    if ( !m_scoreboard->checkCollision(warp_id, pI) ) {
                        SCHED_DPRINTF( "Warp (warp_id %u, dynamic_warp_id %u) passes scoreboard\n",
                                       (*iter)->get_warp_id(), (*iter)->get_dynamic_warp_id() );
                        ready_inst = true;
                        const active_mask_t &active_mask = m_simt_stack[warp_id]->get_active_mask();
                        assert( warp(warp_id).inst_in_pipeline() );
                        if ( (pI->op == LOAD_OP) || (pI->op == STORE_OP) || (pI->op == MEMORY_BARRIER_OP) || 
                             (pI->op == COMMIT_OP) ) 
                        {   
			    if( m_mem_out->has_free() ) {
			        if (pI->op == COMMIT_OP && warp(warp_id).get_n_logical_tm_req() > 0) {
			           // While we still have outstanding logical tm requests, we cannot commit
				   ready_inst = false;
				} else {
                                   m_shader->issue_warp(*m_mem_out,pI,active_mask,warp_id);
                                   issued++;
                                   issued_inst=true;
                                   warp_inst_issued = true;
				}
                            }
                        } else {
                            bool sp_pipe_avail = m_sp_out->has_free();
                            bool sfu_pipe_avail = m_sfu_out->has_free();
                            if( sp_pipe_avail && (pI->op != SFU_OP) ) {
                                // always prefer SP pipe for operations that can use both SP and SFU pipelines
                                m_shader->issue_warp(*m_sp_out,pI,active_mask,warp_id);
                                issued++;
                                issued_inst=true;
                                warp_inst_issued = true;
                            } else if ( (pI->op == SFU_OP) || (pI->op == ALU_SFU_OP) ) {
                                if( sfu_pipe_avail ) {
                                    m_shader->issue_warp(*m_sfu_out,pI,active_mask,warp_id);
                                    issued++;
                                    issued_inst=true;
                                    warp_inst_issued = true;
                                }
                            } 
                        }
                    } else {
			// TM-HACK: issue a dummy COMMIT to clear the number of writing threads
			if (m_scoreboard->inTxRestart(warp_id)) { 
			    assert(g_tm_options.m_use_logical_timestamp_based_tm == true);
			    
			    if (warp(warp_id).get_n_logical_tm_req() == 0) {
			        ready_inst = true;
			        if ((m_shader->warps_need_clean_num_writing(warp_id)).any()) {
			            warp_inst_t dummy_commit_inst = warp_inst_t(true, m_shader->get_config());
			            dummy_commit_inst.op = COMMIT_OP;
				    dummy_commit_inst.memory_op = no_memory_op;
			            dummy_commit_inst.is_tcommit = true;
			            for (int r = 0; r < 4; r++) {
			                assert(dummy_commit_inst.out[r] == 0);
			            }
			            active_mask_t dummy_active_mask = active_mask_t((unsigned long long)0); 
			            if (m_mem_out->has_free()) {
			                m_shader->issue_warp_dummy_commit(*m_mem_out, &dummy_commit_inst, dummy_active_mask, warp_id);
					issued++;
			            }
			        } else {
				    if (m_shader->could_mask_tm_token(warp_id)) {
					assert(g_tm_options.m_logical_timestamp_dynamic_concurrency_enabled);
				        m_shader->mask_tm_token(warp_id);
				    } else {
					if (m_shader->is_masked_tm_token(warp_id) == false) {
			                    m_shader->init_aborted_tx_pts(warp_id);
			                    m_scoreboard->doneTxRestart(warp_id);
                                            (m_shader->get_warps())[warp_id].get_tm_warp_info().reset();
					}
				    }
				}
			    }
			}
                        SCHED_DPRINTF( "Warp (warp_id %u, dynamic_warp_id %u) fails scoreboard\n",
                                       (*iter)->get_warp_id(), (*iter)->get_dynamic_warp_id() );
                    }
                }
            } else if( valid ) {
               // this case can happen after a return instruction in diverged warp
               SCHED_DPRINTF( "Warp (warp_id %u, dynamic_warp_id %u) return from diverged warp flush\n",
                              (*iter)->get_warp_id(), (*iter)->get_dynamic_warp_id() );
               warp(warp_id).set_next_pc(pc);
               warp(warp_id).ibuffer_flush();
            }
            if(warp_inst_issued) {
                SCHED_DPRINTF( "Warp (warp_id %u, dynamic_warp_id %u) issued %u instructions\n",
                               (*iter)->get_warp_id(),
                               (*iter)->get_dynamic_warp_id(),
                               issued );
                do_on_warp_issued( warp_id, issued, iter);
            }
            checked++;
        }

        if ( issued ) {
            // This might be a bit inefficient, but we need to maintain
            // two ordered list for proper scheduler execution.
            // We could remove the need for this loop by associating a
            // supervised_is index with each entry in the m_next_cycle_prioritized_warps
            // vector. For now, just run through until you find the right warp_id
            for ( std::vector< shd_warp_t* >::const_iterator supervised_iter = m_supervised_warps.begin();
                  supervised_iter != m_supervised_warps.end();
                  ++supervised_iter ) {
                if ( *iter == *supervised_iter ) {
                    m_last_supervised_issued = supervised_iter;
                }
            }
            break;
        } 
    }

    // issue stall statistics:
    if( !valid_inst ) 
        m_stats->shader_cycle_distro[0]++; // idle or control hazard
    else if( !ready_inst ) 
        m_stats->shader_cycle_distro[1]++; // waiting for RAW hazards (possibly due to memory) 
    else if( !issued_inst ) 
        m_stats->shader_cycle_distro[2]++; // pipeline stalled
}

void scheduler_unit::do_on_warp_issued( unsigned warp_id,
                                        unsigned num_issued,
                                        const std::vector< shd_warp_t* >::const_iterator& prioritized_iter )
{
    m_stats->event_warp_issued( m_shader->get_sid(),
                                warp_id,
                                num_issued,
                                warp(warp_id).get_dynamic_warp_id() );
    warp(warp_id).ibuffer_step();
}

bool scheduler_unit::sort_warps_by_oldest_dynamic_id(shd_warp_t* lhs, shd_warp_t* rhs)
{
    if (rhs && lhs) {
        if ( lhs->done_exit() || lhs->waiting() ) {
            return false;
        } else if ( rhs->done_exit() || rhs->waiting() ) {
            return true;
        } else {
            return lhs->get_dynamic_warp_id() < rhs->get_dynamic_warp_id();
        }
    } else {
        return lhs < rhs;
    }
}

void lrr_scheduler::order_warps()
{
    order_lrr( m_next_cycle_prioritized_warps,
               m_supervised_warps,
               m_last_supervised_issued,
               m_supervised_warps.size() );
}

void gto_scheduler::order_warps()
{
    order_by_priority( m_next_cycle_prioritized_warps,
                       m_supervised_warps,
                       m_last_supervised_issued,
                       m_supervised_warps.size(),
                       ORDERING_GREEDY_THEN_PRIORITY_FUNC,
                       scheduler_unit::sort_warps_by_oldest_dynamic_id );
}

void
two_level_active_scheduler::do_on_warp_issued( unsigned warp_id,
                                               unsigned num_issued,
                                               const std::vector< shd_warp_t* >::const_iterator& prioritized_iter )
{
    scheduler_unit::do_on_warp_issued( warp_id, num_issued, prioritized_iter);
    if ( SCHEDULER_PRIORITIZATION_LRR == m_inner_level_prioritization ) {
        std::vector< shd_warp_t* > new_active; 
        order_lrr( new_active,
                   m_next_cycle_prioritized_warps,
                   prioritized_iter,
                   m_next_cycle_prioritized_warps.size() );
        m_next_cycle_prioritized_warps = new_active;
    } else {
        fprintf( stderr,
                 "Unimplemented m_inner_level_prioritization: %d\n",
                 m_inner_level_prioritization );
        abort();
    }
}

void two_level_active_scheduler::order_warps()
{
    //Move waiting warps to m_pending_warps
    unsigned num_demoted = 0;
    for (   std::vector< shd_warp_t* >::iterator iter = m_next_cycle_prioritized_warps.begin();
            iter != m_next_cycle_prioritized_warps.end(); ) {
        bool waiting = (*iter)->waiting();
        for (int i=0; i<4; i++){
            const warp_inst_t* inst = (*iter)->ibuffer_next_inst();
            //Is the instruction waiting on a long operation?
            if ( inst && inst->in[i] > 0 && this->m_scoreboard->islongop((*iter)->get_warp_id(), inst->in[i])){
                waiting = true;
            }
        }

        if( waiting ) {
            m_pending_warps.push_back(*iter);
            iter = m_next_cycle_prioritized_warps.erase(iter);
            SCHED_DPRINTF( "DEMOTED warp_id=%d, dynamic_warp_id=%d\n",
                           (*iter)->get_warp_id(),
                           (*iter)->get_dynamic_warp_id() );
            ++num_demoted;
        } else {
            ++iter;
        }
    }

    //If there is space in m_next_cycle_prioritized_warps, promote the next m_pending_warps
    unsigned num_promoted = 0;
    if ( SCHEDULER_PRIORITIZATION_SRR == m_outer_level_prioritization ) {
        while ( m_next_cycle_prioritized_warps.size() < m_max_active_warps ) {
            m_next_cycle_prioritized_warps.push_back(m_pending_warps.front());
            m_pending_warps.pop_front();
            SCHED_DPRINTF( "PROMOTED warp_id=%d, dynamic_warp_id=%d\n",
                           (m_next_cycle_prioritized_warps.back())->get_warp_id(),
                           (m_next_cycle_prioritized_warps.back())->get_dynamic_warp_id() );
            ++num_promoted;
        }
    } else {
        fprintf( stderr,
                 "Unimplemented m_outer_level_prioritization: %d\n",
                 m_outer_level_prioritization );
        abort();
    }
    assert( num_promoted == num_demoted );
}

swl_scheduler::swl_scheduler ( shader_core_stats* stats, shader_core_ctx* shader,
                               Scoreboard* scoreboard, simt_stack** simt,
                               std::vector<shd_warp_t>* warp,
                               register_set* sp_out,
                               register_set* sfu_out,
                               register_set* mem_out,
                               int id,
                               char* config_string )
    : scheduler_unit ( stats, shader, scoreboard, simt, warp, sp_out, sfu_out, mem_out, id )
{
    int ret = sscanf( config_string,
                      "warp_limiting:%d:%d",
                      (int*)&m_prioritization,
                      &m_num_warps_to_limit
                     );
    assert( 2 == ret );
    // Currently only GTO is implemented
    assert( m_prioritization == SCHEDULER_PRIORITIZATION_GTO );
    assert( m_num_warps_to_limit <= shader->get_config()->max_warps_per_shader );
}

void swl_scheduler::order_warps()
{
    if ( SCHEDULER_PRIORITIZATION_GTO == m_prioritization ) {
        order_by_priority( m_next_cycle_prioritized_warps,
                           m_supervised_warps,
                           m_last_supervised_issued,
                           MIN( m_num_warps_to_limit, m_supervised_warps.size() ),
                           ORDERING_GREEDY_THEN_PRIORITY_FUNC,
                           scheduler_unit::sort_warps_by_oldest_dynamic_id );
    } else {
        fprintf(stderr, "swl_scheduler m_prioritization = %d\n", m_prioritization);
        abort();
    }
}

void shader_core_ctx::read_operands()
{
}

address_type coalesced_segment(address_type addr, unsigned segment_size_lg2bytes)
{
   return  (addr >> segment_size_lg2bytes);
}

// Translation for single 4B access
address_type shader_core_ctx::translate_local_memaddr(address_type localaddr, unsigned tid, unsigned data_size ) {
    assert(data_size == 4);

    unsigned num_shader = m_config->n_simt_clusters*m_config->n_simt_cores_per_cluster;

    new_addr_type translated_addrs[1];
    translate_local_memaddr(localaddr, tid, num_shader, data_size, (new_addr_type*) translated_addrs);
    return translated_addrs[0];
}

// Returns numbers of addresses in translated_addrs, each addr points to a 4B (32-bit) word
unsigned shader_core_ctx::translate_local_memaddr( address_type localaddr, unsigned tid, unsigned num_shader, unsigned datasize, new_addr_type* translated_addrs )
{
   // During functional execution, each thread sees its own memory space for local memory, but these
   // need to be mapped to a shared address space for timing simulation.  We do that mapping here.

   address_type thread_base = 0;
   unsigned max_concurrent_threads=0;
   if (m_config->gpgpu_local_mem_map) {
      // Dnew = D*N + T%nTpC + nTpC*C
      // N = nTpC*nCpS*nS (max concurent threads)
      // C = nS*K + S (hw cta number per gpu)
      // K = T/nTpC   (hw cta number per core)
      // D = data index
      // T = thread
      // nTpC = number of threads per CTA
      // nCpS = number of CTA per shader
      // 
      // for a given local memory address threads in a CTA map to contiguous addresses,
      // then distribute across memory space by CTAs from successive shader cores first, 
      // then by successive CTA in same shader core
      thread_base = 4*(kernel_padded_threads_per_cta * (m_sid + num_shader * (tid / kernel_padded_threads_per_cta))
                       + tid % kernel_padded_threads_per_cta); 
      max_concurrent_threads = kernel_padded_threads_per_cta * kernel_max_cta_per_shader * num_shader;
   } else {
      // legacy mapping that maps the same address in the local memory space of all threads 
      // to a single contiguous address region 
      thread_base = 4*(m_config->n_thread_per_shader * m_sid + tid);
      max_concurrent_threads = num_shader * m_config->n_thread_per_shader;
   }
   assert( thread_base < 4/*word size*/*max_concurrent_threads );

   // If requested datasize > 4B, split into multiple 4B accesses
   // otherwise do one sub-4 byte memory access
   unsigned num_accesses = 0;

   if(datasize >= 4) {
      // >4B access, split into 4B chunks
      assert(datasize%4 == 0);   // Must be a multiple of 4B
      num_accesses = datasize/4;
      assert(num_accesses <= MAX_ACCESSES_PER_INSN_PER_THREAD); // max 32B
      assert(localaddr%4 == 0); // Address must be 4B aligned - required if accessing 4B per request, otherwise access will overflow into next thread's space
      for(unsigned i=0; i<num_accesses; i++) {
          address_type local_word = localaddr/4 + i;
          address_type linear_address = local_word*max_concurrent_threads*4 + thread_base + LOCAL_GENERIC_START;
          translated_addrs[i] = linear_address;
      }
   } else {
      // Sub-4B access, do only one access
      assert(datasize > 0);
      num_accesses = 1;
      address_type local_word = localaddr/4;
      address_type local_word_offset = localaddr%4;
      assert( (localaddr+datasize-1)/4  == local_word ); // Make sure access doesn't overflow into next 4B chunk
      address_type linear_address = local_word*max_concurrent_threads*4 + local_word_offset + thread_base + LOCAL_GENERIC_START;
      translated_addrs[0] = linear_address;
   }
   return num_accesses;
}

/////////////////////////////////////////////////////////////////////////////////////////
int shader_core_ctx::test_res_bus(int latency){
	for(unsigned i=0; i<num_result_bus; i++){
		if(!m_result_bus[i]->test(latency)){return i;}
	}
	return -1;
}

void shader_core_ctx::execute()
{
	for(unsigned i=0; i<num_result_bus; i++){
		*(m_result_bus[i]) >>=1;
	}
    for( unsigned n=0; n < m_num_function_units; n++ ) {
        unsigned multiplier = m_fu[n]->clock_multiplier();
        for( unsigned c=0; c < multiplier; c++ ) 
            m_fu[n]->cycle();
        m_fu[n]->active_lanes_in_pipeline();
        enum pipeline_stage_name_t issue_port = m_issue_port[n];
        register_set& issue_inst = m_pipeline_reg[ issue_port ];
	warp_inst_t** ready_reg = issue_inst.get_ready();
        if( issue_inst.has_ready() && m_fu[n]->can_issue( **ready_reg ) ) {
            bool schedule_wb_now = !m_fu[n]->stallable();
            int resbus = -1;
            if( schedule_wb_now && (resbus=test_res_bus( (*ready_reg)->latency ))!=-1 ) {
                assert( (*ready_reg)->latency < MAX_ALU_LATENCY );
                m_result_bus[resbus]->set( (*ready_reg)->latency );
                m_fu[n]->issue( issue_inst );
            } else if( !schedule_wb_now ) {
                m_fu[n]->issue( issue_inst );
            } else {
                // stall issue (cannot reserve result bus)
            }
        }
    }
}

void shader_core_ctx::dec_inst_in_pipeline( const warp_inst_t &inst )
{
    unsigned warp_id = inst.warp_id();
    m_warp[warp_id].dec_inst_in_pipeline();
    m_scoreboard->dec_inflight_insn(warp_id);

    // Profiling - mark threads as exiting pipeline
    for ( unsigned t=0; t < m_config->warp_size; t++ ) {
        unsigned tid=m_config->warp_size*inst.warp_id()+t;
        if( inst.active(t) && !m_threadState[tid].m_timeout_validation_mode ) {
           // Not in validation mode
           if(!m_threadState[tid].m_timeout_validation_mode_set || inst.get_issue_cycle() > m_threadState[tid].m_timeout_validation_cycle ) {
              // If thread ever was in validation mode, make sure this returning access was issued after it
              m_threadState[tid].m_in_pipeline -= 1;
              if(m_threadState[tid].m_in_pipeline < 0) {
                 printf("Error: sid=%d wid=%d tid=%d \n", m_sid, inst.warp_id(), tid);
              }
              assert(m_threadState[tid].m_in_pipeline >= 0);
           }
        }
    }
}

void ldst_unit::print_cache_stats( FILE *fp, unsigned& dl1_accesses, unsigned& dl1_misses ) {
   if( !m_config->m_L1D_config.disabled() ) 
      m_L1D->print( fp, dl1_accesses, dl1_misses );
}

void ldst_unit::get_cache_stats(cache_stats &cs) {
    // Adds stats to 'cs' from each cache
    if(m_L1D)
        cs += m_L1D->get_stats();
    if(m_L1C)
        cs += m_L1C->get_stats();
    if(m_L1T)
        cs += m_L1T->get_stats();
}

void ldst_unit::get_L1D_sub_stats(struct cache_sub_stats &css) const{
    if(m_L1D)
        m_L1D->get_sub_stats(css);
}
void ldst_unit::get_L1C_sub_stats(struct cache_sub_stats &css) const{
    if(m_L1C)
        m_L1C->get_sub_stats(css);
}
void ldst_unit::get_L1T_sub_stats(struct cache_sub_stats &css) const{
    if(m_L1T)
        m_L1T->get_sub_stats(css);
}

void shader_core_ctx::warp_inst_complete(const warp_inst_t &inst)
{
   #if 0
      printf("[warp_inst_complete] uid=%u core=%u warp=%u pc=%#x @ time=%llu issued@%llu\n", 
             inst.get_uid(), m_sid, inst.warp_id(), inst.pc, gpu_tot_sim_cycle + gpu_sim_cycle, inst.get_issue_cycle()); 
   #endif
  if(inst.op_pipe==SP__OP)
	  m_stats->m_num_sp_committed[m_sid]++;
  else if(inst.op_pipe==SFU__OP)
	  m_stats->m_num_sfu_committed[m_sid]++;
  else if(inst.op_pipe==MEM__OP)
	  m_stats->m_num_mem_committed[m_sid]++;

  if(m_config->gpgpu_clock_gated_lanes==false)
	  m_stats->m_num_sim_insn[m_sid] += m_config->warp_size;
  else
	  m_stats->m_num_sim_insn[m_sid] += inst.active_count();

  m_stats->m_num_sim_winsn[m_sid]++;
  m_gpu->gpu_sim_insn += inst.active_count();
  inst.completed(gpu_tot_sim_cycle + gpu_sim_cycle);
}

void shader_core_ctx::writeback()
{

	unsigned max_committed_thread_instructions=m_config->warp_size * (m_config->pipe_widths[EX_WB]); //from the functional units
	m_stats->m_pipeline_duty_cycle[m_sid]=((float)(m_stats->m_num_sim_insn[m_sid]-m_stats->m_last_num_sim_insn[m_sid]))/max_committed_thread_instructions;

    m_stats->m_last_num_sim_insn[m_sid]=m_stats->m_num_sim_insn[m_sid];
    m_stats->m_last_num_sim_winsn[m_sid]=m_stats->m_num_sim_winsn[m_sid];

    warp_inst_t** preg = m_pipeline_reg[EX_WB].get_ready();
    warp_inst_t* pipe_reg = (preg==NULL)? NULL:*preg;
    while( preg and !pipe_reg->empty() ) {
    	/*
    	 * Right now, the writeback stage drains all waiting instructions
    	 * assuming there are enough ports in the register file or the
    	 * conflicts are resolved at issue.
    	 */
    	/*
    	 * The operand collector writeback can generally generate a stall
    	 * However, here, the pipelines should be un-stallable. This is
    	 * guaranteed because this is the first time the writeback function
    	 * is called after the operand collector's step function, which
    	 * resets the allocations. There is one case which could result in
    	 * the writeback function returning false (stall), which is when
    	 * an instruction tries to modify two registers (GPR and predicate)
    	 * To handle this case, we ignore the return value (thus allowing
    	 * no stalling).
    	 */
        m_operand_collector.writeback(*pipe_reg);
        // unsigned warp_id = pipe_reg->warp_id();
        m_scoreboard->releaseRegisters( pipe_reg );
        dec_inst_in_pipeline(*pipe_reg);
        warp_inst_complete(*pipe_reg);
        m_gpu->gpu_sim_insn_last_update_sid = m_sid;
        m_gpu->gpu_sim_insn_last_update = gpu_sim_cycle;
        m_last_inst_gpu_sim_cycle = gpu_sim_cycle;
        m_last_inst_gpu_tot_sim_cycle = gpu_tot_sim_cycle;
        pipe_reg->clear();
        preg = m_pipeline_reg[EX_WB].get_ready();
        pipe_reg = (preg==NULL)? NULL:*preg;
    }
}

bool ldst_unit::shared_cycle( warp_inst_t &inst, mem_stage_stall_type &rc_fail, mem_stage_access_type &fail_type)
{
   if( inst.space.get_type() != shared_space )
       return true;

   if(inst.has_dispatch_delay()){
	   m_stats->gpgpu_n_shmem_bank_access[m_sid]++;
   }

   bool stall = inst.dispatch_delay();
   if( stall ) {
       fail_type = S_MEM;
       rc_fail = BK_CONF;
   } else 
       rc_fail = NO_RC_FAIL;
   return !stall; 
}

mem_stage_stall_type
ldst_unit::process_cache_access( cache_t* cache,
                                 new_addr_type address,
                                 warp_inst_t &inst,
                                 std::list<cache_event>& events,
                                 mem_fetch *mf,
                                 enum cache_request_status status )
{
    mem_stage_stall_type result = NO_RC_FAIL;
    bool write_sent = was_write_sent(events);
    bool read_sent = was_read_sent(events);
    if( write_sent ) { 
        m_core->inc_store_req( inst.warp_id() );
    }
    if ( status == HIT ) {
        assert( !read_sent );
        bool write_access = inst.accessq_back().is_write(); 
        inst.accessq_pop_back();
        if (not write_access) {  // tx_load can generate local memory writes 
            for ( unsigned r=0; r < 4; r++)
                if (inst.out[r] > 0)
                    m_pending_writes[inst.warp_id()][inst.out[r]]--; 
        }
        if( !write_sent ) 
        delete mf;
    } else if ( status == RESERVATION_FAIL ) {
        result = COAL_STALL;
        assert( !read_sent );
        assert( !write_sent );
        delete mf;
    } else {
        assert( status == MISS || status == HIT_RESERVED );
        // inst.clear_active( access.get_warp_mask() ); // threads in mf writeback when mf returns
        inst.accessq_pop_back();
    }
    if( !inst.accessq_empty() )
        result = BK_CONF;
    return result;
}

mem_stage_stall_type ldst_unit::process_memory_access_queue( cache_t *cache, warp_inst_t &inst )
{
    mem_stage_stall_type result = NO_RC_FAIL;
    if( inst.accessq_empty() )
        return result;

    if( !cache->data_port_free() ) 
        return DATA_PORT_STALL; 

    //const mem_access_t &access = inst.accessq_back();
    mem_fetch *mf = m_mf_allocator->alloc(inst,inst.accessq_back());
    if (inst.in_transaction) {
        pause_and_go(mf);
    }
    std::list<cache_event> events;
    enum cache_request_status status = cache->access(mf->get_addr(),mf,gpu_sim_cycle+gpu_tot_sim_cycle,events);
    return process_cache_access( cache, mf->get_addr(), inst, events, mf, status );
}

bool ldst_unit::constant_cycle( warp_inst_t &inst, mem_stage_stall_type &rc_fail, mem_stage_access_type &fail_type)
{
   if( inst.empty() || ((inst.space.get_type() != const_space) && (inst.space.get_type() != param_space_kernel)) )
       return true;
   if( inst.active_count() == 0 ) 
       return true;
   mem_stage_stall_type fail = process_memory_access_queue(m_L1C,inst);
   if (fail != NO_RC_FAIL){ 
      rc_fail = fail; //keep other fails if this didn't fail.
      fail_type = C_MEM;
      if (rc_fail == BK_CONF or rc_fail == COAL_STALL) {
         m_stats->gpgpu_n_cmem_portconflict++; //coal stalls aren't really a bank conflict, but this maintains previous behavior.
      }
   }
   return inst.accessq_empty(); //done if empty.
}

bool ldst_unit::texture_cycle( warp_inst_t &inst, mem_stage_stall_type &rc_fail, mem_stage_access_type &fail_type)
{
   if( inst.empty() || inst.space.get_type() != tex_space )
       return true;
   if( inst.active_count() == 0 ) 
       return true;
   mem_stage_stall_type fail = process_memory_access_queue(m_L1T,inst);
   if (fail != NO_RC_FAIL){ 
      rc_fail = fail; //keep other fails if this didn't fail.
      fail_type = T_MEM;
   }
   return inst.accessq_empty(); //done if empty.
}

bool ldst_unit::memory_cycle( warp_inst_t &inst, mem_stage_stall_type &stall_reason, mem_stage_access_type &access_type ) {
   if( inst.empty() || 
       ((inst.space.get_type() != global_space) &&
        (inst.space.get_type() != local_space) &&
        (inst.space.get_type() != param_space_local)) ) 
       return true;
   if( inst.active_count() == 0 ) {
       assert ( inst.accessq_empty() ); 
       return true;
   }
   assert( !inst.accessq_empty() );
   mem_stage_stall_type stall_cond = NO_RC_FAIL;
   const mem_access_t &access = inst.accessq_back();
   unsigned size = access.get_size();

   bool bypassL1D = false;
   
   if ( CACHE_GLOBAL == inst.cache_op || 
        (m_L1D == NULL) || 
	(inst.is_logical_tm_req() && (access.get_type() == GLOBAL_ACC_W)) ) {
       bypassL1D = true; 
   } else if (inst.space.is_global()) { // global memory access 
       // skip L1 cache only if it is not in transaction or this is ideal TM
       if (m_core->get_config()->gmem_skip_L1D and (m_core->get_config()->no_tx_log_gen or inst.in_transaction == false)) 
           bypassL1D = true; 
   }

   if( bypassL1D ) {
       // bypass L1 cache
       if( m_icnt->full(size, inst.is_store() || inst.isatomic()) ) {
           stall_cond = ICNT_RC_FAIL;
       } else {
           mem_fetch *mf = m_mf_allocator->alloc(inst,access);
	   if (inst.in_transaction) {
	       pause_and_go(mf);
	   }
           m_icnt->push(mf);
           inst.accessq_pop_back();
           //inst.clear_active( access.get_warp_mask() );
           if( inst.is_load() ) { 
              for( unsigned r=0; r < 4; r++) 
                  if(inst.out[r] > 0) 
                      assert( m_pending_writes[inst.warp_id()][inst.out[r]] > 0 );
           } else if( inst.is_store() ) {
              m_core->inc_store_req( inst.warp_id() );
	   }
       }
   } else {
       assert( CACHE_UNDEFINED != inst.cache_op );
       stall_cond = process_memory_access_queue(m_L1D,inst);
   }
   if( !inst.accessq_empty() ) 
       stall_cond = COAL_STALL;
   if (stall_cond != NO_RC_FAIL) {
      stall_reason = stall_cond;
      bool iswrite = inst.is_store();
      if (inst.space.is_local()) 
         access_type = (iswrite)?L_MEM_ST:L_MEM_LD;
      else 
         access_type = (iswrite)?G_MEM_ST:G_MEM_LD;
   }
   return inst.accessq_empty(); 
}

bool ldst_unit::response_buffer_full() const
{
    return m_response_fifo.size() >= m_config->ldst_unit_response_queue_size;
}

void ldst_unit::fill( mem_fetch *mf )
{
    mf->set_status(IN_SHADER_LDST_RESPONSE_FIFO,gpu_sim_cycle+gpu_tot_sim_cycle);
    m_response_fifo.push_back(mf);
}

void ldst_unit::flush(){
	// Flush L1D cache
	m_L1D->flush();
}

simd_function_unit::simd_function_unit( const shader_core_config *config )
{ 
    m_config=config;
    m_dispatch_reg = new warp_inst_t(config); 
}


sfu:: sfu(  register_set* result_port, const shader_core_config *config,shader_core_ctx *core  )
    : pipelined_simd_unit(result_port,config,config->max_sfu_latency,core)
{ 
    m_name = "SFU"; 
}

void sfu::issue( register_set& source_reg )
{
    warp_inst_t** ready_reg = source_reg.get_ready();
	//m_core->incexecstat((*ready_reg));

	(*ready_reg)->op_pipe=SFU__OP;
	m_core->incsfu_stat(m_core->get_config()->warp_size,(*ready_reg)->latency);
	pipelined_simd_unit::issue(source_reg);
}

void ldst_unit::active_lanes_in_pipeline(){
	unsigned active_count=pipelined_simd_unit::get_active_lanes_in_pipeline();
	assert(active_count<=m_core->get_config()->warp_size);
	m_core->incfumemactivelanes_stat(active_count);
}
void sp_unit::active_lanes_in_pipeline(){
	unsigned active_count=pipelined_simd_unit::get_active_lanes_in_pipeline();
	assert(active_count<=m_core->get_config()->warp_size);
	m_core->incspactivelanes_stat(active_count);
	m_core->incfuactivelanes_stat(active_count);
	m_core->incfumemactivelanes_stat(active_count);
}

void sfu::active_lanes_in_pipeline(){
	unsigned active_count=pipelined_simd_unit::get_active_lanes_in_pipeline();
	assert(active_count<=m_core->get_config()->warp_size);
	m_core->incsfuactivelanes_stat(active_count);
	m_core->incfuactivelanes_stat(active_count);
	m_core->incfumemactivelanes_stat(active_count);
}

sp_unit::sp_unit( register_set* result_port, const shader_core_config *config,shader_core_ctx *core)
    : pipelined_simd_unit(result_port,config,config->max_sp_latency,core)
{ 
    m_name = "SP "; 
}

void sp_unit :: issue(register_set& source_reg)
{
    warp_inst_t** ready_reg = source_reg.get_ready();
	//m_core->incexecstat((*ready_reg));
	(*ready_reg)->op_pipe=SP__OP;
	m_core->incsp_stat(m_core->get_config()->warp_size,(*ready_reg)->latency);
	pipelined_simd_unit::issue(source_reg);
}


pipelined_simd_unit::pipelined_simd_unit( register_set* result_port, const shader_core_config *config, unsigned max_latency,shader_core_ctx *core )
    : simd_function_unit(config) 
{
    m_result_port = result_port;
    m_pipeline_depth = max_latency;
    m_pipeline_reg = new warp_inst_t*[m_pipeline_depth];
    for( unsigned i=0; i < m_pipeline_depth; i++ ) 
	m_pipeline_reg[i] = new warp_inst_t( config );
    m_core=core;
}


void pipelined_simd_unit::issue( register_set& source_reg )
{
    //move_warp(m_dispatch_reg,source_reg);
    warp_inst_t** ready_reg = source_reg.get_ready();
	m_core->incexecstat((*ready_reg));
	//source_reg.move_out_to(m_dispatch_reg);
	simd_function_unit::issue(source_reg);
}


void ldst_unit::init( mem_fetch_interface *&icnt,
                      shader_core_mem_fetch_allocator *mf_allocator,
                      shader_core_ctx *core, 
                      opndcoll_rfu_t *operand_collector,
                      Scoreboard *scoreboard,
                      const shader_core_config *config,
                      const memory_config *mem_config,  
                      shader_core_stats *stats,
                      unsigned sid,
                      unsigned tpc )
{
    m_mf_allocator=mf_allocator;
    m_core = core;
    m_operand_collector = operand_collector;
    m_scoreboard = scoreboard;
    m_stats = stats;
    m_sid = sid;
    m_tpc = tpc;
    #define STRSIZE 1024
    char L1T_name[STRSIZE];
    char L1C_name[STRSIZE];
    snprintf(L1T_name, STRSIZE, "L1T_%03d", m_sid);
    snprintf(L1C_name, STRSIZE, "L1C_%03d", m_sid);
    m_L1T = new tex_cache(L1T_name,m_config->m_L1T_config,m_sid,get_shader_texture_cache_id(),icnt,IN_L1T_MISS_QUEUE,IN_SHADER_L1T_ROB);
    m_L1C = new read_only_cache(L1C_name,m_config->m_L1C_config,m_sid,get_shader_constant_cache_id(),icnt,IN_L1C_MISS_QUEUE);
    m_L1D = NULL;
    m_mem_rc = NO_RC_FAIL;
    m_num_writeback_clients=6; // = shared memory, global/local (uncached), L1D, L1T, L1C, TLW
    m_writeback_arb = 0;
    m_next_global=NULL;
    m_last_inst_gpu_sim_cycle=0;
    m_last_inst_gpu_tot_sim_cycle=0;
}


ldst_unit::ldst_unit( mem_fetch_interface *&icnt,
                      shader_core_mem_fetch_allocator *mf_allocator,
                      shader_core_ctx *core, 
                      opndcoll_rfu_t *operand_collector,
                      Scoreboard *scoreboard,
                      const shader_core_config *config,
                      const memory_config *mem_config,  
                      shader_core_stats *stats,
                      unsigned sid,
                      unsigned tpc ) 
    : pipelined_simd_unit(NULL,config,3,core), m_memory_config(mem_config), 
      m_icnt(icnt), m_next_wb(config)
{
    init( icnt,
          mf_allocator,
          core, 
          operand_collector,
          scoreboard,
          config, 
          mem_config,  
          stats, 
          sid,
          tpc );
    if( !m_config->m_L1D_config.disabled() ) {
        char L1D_name[STRSIZE];
        snprintf(L1D_name, STRSIZE, "L1D_%03d", m_sid);
        m_L1D = new l1_cache( L1D_name,
                              m_config->m_L1D_config,
                              m_sid,
                              get_shader_normal_cache_id(),
                              m_icnt,
                              m_mf_allocator,
                              IN_L1D_MISS_QUEUE, true );
    }
    // WF: create log waker and attach to L1D cache
    if (m_config->tlw_coalesce_packets) {
       if (m_config->tlw_use_logical_temporal_cd) {
          m_TLW = new tx_log_walker_warpc_logical(m_core, m_sid, tpc, m_L1D, m_icnt, m_mf_allocator, config, mem_config, *stats, *(stats->m_TLW_stats));
       } else {
          m_TLW = new tx_log_walker_warpc(m_core, m_sid, tpc, m_L1D, m_icnt, m_mf_allocator, config, mem_config, *stats, *(stats->m_TLW_stats));
       }
    } else {
       m_TLW = new tx_log_walker(m_core, m_sid, tpc, m_L1D, m_icnt, m_mf_allocator, config, mem_config, *stats, *(stats->m_TLW_stats));
    }
}

ldst_unit::ldst_unit( mem_fetch_interface *&icnt,
                      shader_core_mem_fetch_allocator *mf_allocator,
                      shader_core_ctx *core, 
                      opndcoll_rfu_t *operand_collector,
                      Scoreboard *scoreboard,
                      const shader_core_config *config,
                      const memory_config *mem_config,  
                      shader_core_stats *stats,
                      unsigned sid,
                      unsigned tpc,
                      l1_cache* new_l1d_cache )
    : pipelined_simd_unit(NULL,config,3,core), m_memory_config(mem_config), 
      m_icnt(icnt), m_L1D(new_l1d_cache), m_next_wb(config)
{
    init( icnt,
          mf_allocator,
          core, 
          operand_collector,
          scoreboard,
          config, 
          mem_config,  
          stats, 
          sid,
          tpc );
    // WF: create log waker and attach to L1D cache
    if (m_config->tlw_coalesce_packets) {
       if (m_config->tlw_use_logical_temporal_cd) {
          m_TLW = new tx_log_walker_warpc_logical(m_core, m_sid, tpc, m_L1D, m_icnt, m_mf_allocator, config, mem_config, *stats, *(stats->m_TLW_stats));
       } else {
	  m_TLW = new tx_log_walker_warpc(m_core, m_sid, tpc, m_L1D, m_icnt, m_mf_allocator, config, mem_config, *stats, *(stats->m_TLW_stats));
       }
    } else {
       m_TLW = new tx_log_walker(m_core, m_sid, tpc, m_L1D, m_icnt, m_mf_allocator, config, mem_config, *stats, *(stats->m_TLW_stats));
    }
}

void ldst_unit::issue( register_set &reg_set )
{
	warp_inst_t* inst = *(reg_set.get_ready());

   // record how many pending register writes/memory accesses there are for this instruction
   assert(inst->empty() == false);
   if (inst->is_load() and inst->space.get_type() != shared_space) {
      unsigned warp_id = inst->warp_id();
      unsigned n_accesses = inst->accessq_count() - inst->n_txlog_fill(); 
      for (unsigned r = 0; r < 4; r++) {
         unsigned reg_id = inst->out[r];
         if (reg_id > 0) {
            m_pending_writes[warp_id][reg_id] += n_accesses;
         }
      }
   }

	inst->op_pipe=MEM__OP;
	// stat collection
	m_core->mem_instruction_stats(*inst);
	m_core->incmem_stat(m_core->get_config()->warp_size,1);
	pipelined_simd_unit::issue(reg_set);
}

void ldst_unit::writeback()
{
    // process next instruction that is going to writeback
    if( !m_next_wb.empty() ) {
        if( m_operand_collector->writeback(m_next_wb) ) {
            bool insn_completed = false; 
            for( unsigned r=0; r < 4; r++ ) {
                if( m_next_wb.out[r] > 0 ) {
                    if( m_next_wb.space.get_type() != shared_space ) {
                        assert( m_pending_writes[m_next_wb.warp_id()][m_next_wb.out[r]] > 0 );
                        unsigned still_pending = --m_pending_writes[m_next_wb.warp_id()][m_next_wb.out[r]];
                        if( !still_pending ) {
                            m_pending_writes[m_next_wb.warp_id()].erase(m_next_wb.out[r]);
                            m_scoreboard->releaseRegister( m_next_wb.warp_id(), m_next_wb.out[r] );
                            insn_completed = true; 
                        }
                    } else { // shared 
                        m_scoreboard->releaseRegister( m_next_wb.warp_id(), m_next_wb.out[r] );
                        insn_completed = true; 
                    }
                }
            }
            if (m_next_wb.is_tcommit) {
                m_scoreboard->doneTxCommit(m_next_wb.warp_id());
                insn_completed = true;  // TODO: Confirm this is needed
            }
            if( insn_completed && m_next_wb.is_dummy == false ) {
                m_core->warp_inst_complete(m_next_wb);
            }
            m_next_wb.clear();
            m_last_inst_gpu_sim_cycle = gpu_sim_cycle;
            m_last_inst_gpu_tot_sim_cycle = gpu_tot_sim_cycle;
        }
    }

    unsigned serviced_client = -1; 
    for( unsigned c = 0; m_next_wb.empty() && (c < m_num_writeback_clients); c++ ) {
        unsigned next_client = (c+m_writeback_arb)%m_num_writeback_clients;
        switch( next_client ) {
        case 0: // shared memory 
            if( !m_pipeline_reg[0]->empty() ) {
                m_next_wb = *m_pipeline_reg[0];
                // TODO: move these to warp_inst_complete()? 
                if(m_next_wb.isatomic()) {
                    m_next_wb.do_atomic();
                    m_core->decrement_atomic_count(m_next_wb.warp_id(), m_next_wb.active_count());
                }
		if (m_next_wb.is_logical_tm_req()) {
		    assert(0);
		}
                m_core->dec_inst_in_pipeline(*m_pipeline_reg[0]);
                m_pipeline_reg[0]->clear();
                serviced_client = next_client; 
            }
            break;
        case 1: // texture response
            if( m_L1T->access_ready() ) {
                mem_fetch *mf = m_L1T->next_access();
		if (mf->is_logical_tm_req()) {
		    assert(0);
		}
                m_next_wb = mf->get_inst();
                delete mf;
                serviced_client = next_client; 
            }
            break;
        case 2: // const cache response
            if( m_L1C->access_ready() ) {
                mem_fetch *mf = m_L1C->next_access();
		if (mf->is_logical_tm_req()) {
		    assert(0);
		}
                m_next_wb = mf->get_inst();
                delete mf;
                serviced_client = next_client; 
            }
            break;
        case 3: // global/local
            if( m_next_global ) {
                m_next_wb = m_next_global->get_inst();
                if( m_next_global->is_logical_tm_req() ) {
		    assert(m_next_global->is_tx_load());
		    assert(m_next_global->get_access_type() == GLOBAL_ACC_R);
		    unsigned dec_n_logical_tm_req = (m_next_global->get_access_warp_mask() & (~m_next_global->get_inst().m_tm_access_info.m_writelog_access)).count(); 
                    m_core->decrement_logical_tm_req_count(m_next_global->get_wid(), dec_n_logical_tm_req);
		    for (int i = 0; i < 32; i++) {
		        if (m_next_global->get_access_warp_mask().test(i) and !m_next_global->get_inst().m_tm_access_info.m_writelog_access.test(i)) {
			    (m_core->get_warps())[m_next_global->get_wid()].dec_n_logical_tm_req_per_thread(i);
			}
		    }
		}
                if( m_next_global->isatomic() ) {
                    m_core->decrement_atomic_count(m_next_global->get_wid(),m_next_global->get_access_warp_mask().count());

                    // Atomic profiling
                    for(unsigned i=0; i<m_config->warp_size; i++) {
                       if(m_next_global->get_access_warp_mask().test(i) == true) {
                          unsigned tid = m_next_global->get_wid() * m_config->warp_size + i;
                          assert(m_core->get_thread_ctx(tid)->m_atomic_mode == true);
                          m_core->get_thread_ctx(tid)->m_atomic_mode = false;

                          // Hack for detecting lock acquisition failure. TODO:HACK
                          if(m_config->thread_state_cas_lock) {
                             ptx_thread_info *thread_state = m_core->get_func_thread_info(tid);
                             if(thread_state->m_last_atomic_callback_value_set) {
                                thread_state->m_last_atomic_callback_value_set = false;
                                if(thread_state->m_last_atomic_callback_value != m_config->thread_state_cas_lock_free_value) {
                                   // Free lock not acquired
                                   m_core->get_thread_ctx(tid)->m_atomic_mode = true;
                                }
                             }
                          }
                       }
                    }
                }
                delete m_next_global;
                m_next_global = NULL;
                serviced_client = next_client; 
            }
            break;
        case 4: 
            if( m_L1D && m_L1D->access_ready() ) {
                mem_fetch *mf = m_L1D->next_access();
                if (mf->get_inst().is_tcommit) { // snoop the writeback if request is for log walker 
                   m_TLW->process_L1D_mem_fetch(mf); 
                } else {
                   m_next_wb = mf->get_inst();
                }
                delete mf;
                serviced_client = next_client; 
            }
            break;
        case 5: // tx log walker response
            if (m_TLW && m_TLW->commit_ready()) {
                m_next_wb = m_TLW->next_commit_ready();
                m_TLW->pop_commit_ready(); 
                serviced_client = next_client; 
            }
            break;
        default: abort();
        }
    }
    // update arbitration priority only if: 
    // 1. the writeback buffer was available 
    // 2. a client was serviced 
    if (serviced_client != (unsigned)-1) {
        m_writeback_arb = (serviced_client + 1) % m_num_writeback_clients; 
    }
}

unsigned ldst_unit::clock_multiplier() const
{ 
    return m_config->mem_warp_parts; 
}

bool ldst_unit::has_message_pending() const 
{
   // TLW still has message to send out 
   bool has_message = m_TLW->has_out_message(); 

   // ldst unit still has message to process 
   has_message = has_message or (not m_response_fifo.empty()); 

   return has_message; 
}

void ldst_unit::cycle()
{
   writeback();
   m_operand_collector->step();
   for( unsigned stage=0; (stage+1)<m_pipeline_depth; stage++ ) 
       if( m_pipeline_reg[stage]->empty() && !m_pipeline_reg[stage+1]->empty() )
            move_warp(m_pipeline_reg[stage], m_pipeline_reg[stage+1]);

   if( !m_response_fifo.empty() ) {
       mem_fetch *mf = m_response_fifo.front();
       if (mf->istexture()) {
           if (m_L1T->fill_port_free()) {
               m_L1T->fill(mf,gpu_sim_cycle+gpu_tot_sim_cycle);
               m_response_fifo.pop_front(); 
           }
       } else if (mf->isconst())  {
           if (m_L1C->fill_port_free()) {
               mf->set_status(IN_SHADER_FETCHED,gpu_sim_cycle+gpu_tot_sim_cycle);
               m_L1C->fill(mf,gpu_sim_cycle+gpu_tot_sim_cycle);
               m_response_fifo.pop_front(); 
           }
       } else {
           // intercept commit unit reply for tx log walker 
           if (m_TLW->process_commit_unit_reply(mf) == true) {
               // packet intercepted
               m_response_fifo.pop_front(); 
               delete mf; 
           } else if( mf->get_type() == WRITE_ACK || ( m_config->gpgpu_perfect_mem && mf->get_is_write() )) {
               m_core->store_ack(mf);
	       if (mf->is_logical_tm_req()) {
		   if (mf->get_access_type() == GLOBAL_ACC_W) {
		       unsigned dec_n_logical_tm_req = (mf->get_access_warp_mask() & (~mf->get_inst().m_tm_access_info.m_writelog_access)).count();
                       m_core->decrement_logical_tm_req_count(mf->get_wid(), dec_n_logical_tm_req);
		       for (int i = 0; i < 32; i++) {
		           if (mf->get_access_warp_mask().test(i) and !mf->get_inst().m_tm_access_info.m_writelog_access.test(i)) {
		               (m_core->get_warps())[mf->get_wid()].dec_n_logical_tm_req_per_thread(i);
		           }
		       }
		   }
	       }
               m_response_fifo.pop_front();
               delete mf;
           } else {
               assert( !mf->get_is_write() ); // L1 cache is write evict, allocate line on load miss only

               bool bypassL1D = false; 
               if ( CACHE_GLOBAL == mf->get_inst().cache_op || (m_L1D == NULL) ) {
                   bypassL1D = true; 
               } else if (mf->get_access_type() == GLOBAL_ACC_R || mf->get_access_type() == GLOBAL_ACC_W) { // global memory access 
                   // skip L1 cache only if it is not in transaction or this is ideal TM 
                   if (m_core->get_config()->gmem_skip_L1D and (m_core->get_config()->no_tx_log_gen or mf->is_transactional() == false)) 
                       bypassL1D = true; 
               }
               if( bypassL1D ) {
                   if( m_next_global == NULL ) {
                       mf->set_status(IN_SHADER_FETCHED,gpu_sim_cycle+gpu_tot_sim_cycle);
                       m_response_fifo.pop_front();
                       m_next_global = mf;
                   }
               } else {
                   if ( mf->is_tx_load() ) {
                       // tx load is like a cache_global but also writes to L1 cache 
                       if ( m_next_global == NULL and m_L1D->fill_port_free() ) {
                           mf->set_status(IN_SHADER_FETCHED,gpu_sim_cycle+gpu_tot_sim_cycle);
                           m_L1D->tx_load_fill(mf, gpu_sim_cycle+gpu_tot_sim_cycle); 
                           m_response_fifo.pop_front();
                           m_next_global = mf;
                       }
                   } else {
                       if (m_L1D->fill_port_free()) {
                           m_L1D->fill(mf,gpu_sim_cycle+gpu_tot_sim_cycle);
                           m_response_fifo.pop_front();
                       }
                   }
               }
           }
       }
   }

   m_TLW->cycle(); 
   m_L1T->cycle();
   m_L1C->cycle();
   if( m_L1D ) m_L1D->cycle();

   warp_inst_t &pipe_reg = *m_dispatch_reg;
   enum mem_stage_stall_type rc_fail = NO_RC_FAIL;
   mem_stage_access_type type;
   bool done = true;
   done &= shared_cycle(pipe_reg, rc_fail, type);
   done &= constant_cycle(pipe_reg, rc_fail, type);
   done &= texture_cycle(pipe_reg, rc_fail, type);
   done &= m_TLW->process_commit(pipe_reg, rc_fail, type);
   done &= memory_cycle(pipe_reg, rc_fail, type);
   m_mem_rc = rc_fail;

   if (!done) { // log stall types and return
      assert(rc_fail != NO_RC_FAIL);
      m_stats->gpgpu_n_stall_shd_mem++;
      m_stats->gpu_stall_shd_mem_breakdown[type][rc_fail]++;
      return;
   }

   if( !pipe_reg.empty() ) {
       unsigned warp_id = pipe_reg.warp_id();
       if( pipe_reg.is_load() ) {
           if( pipe_reg.space.get_type() == shared_space ) {
               if( m_pipeline_reg[2]->empty() ) {
                   // new shared memory request
                   move_warp(m_pipeline_reg[2],m_dispatch_reg);
                   m_dispatch_reg->clear();
               }
           } else {
               //if( pipe_reg.active_count() > 0 ) {
               //    if( !m_operand_collector->writeback(pipe_reg) ) 
               //        return;
               //} 

               bool pending_requests=false;
               for( unsigned r=0; r<4; r++ ) {
                   unsigned reg_id = pipe_reg.out[r];
                   if( reg_id > 0 ) {
                       if( m_pending_writes[warp_id].find(reg_id) != m_pending_writes[warp_id].end() ) {
                           if ( m_pending_writes[warp_id][reg_id] > 0 ) {
                               pending_requests=true;
                               break;
                           } else {
                               // this instruction is done already
                               m_pending_writes[warp_id].erase(reg_id); 
                           }
                       }
                   }
               }
               if( !pending_requests ) {
                   m_core->warp_inst_complete(*m_dispatch_reg);
                   m_scoreboard->releaseRegisters(m_dispatch_reg);
               }
               m_core->dec_inst_in_pipeline(pipe_reg); // the instruction is exicting the pipeline regardless
               m_dispatch_reg->clear();
           }
       } else {
           // stores exit pipeline here
           if (!pipe_reg.is_dummy) {
              m_core->dec_inst_in_pipeline(pipe_reg);
              m_core->warp_inst_complete(*m_dispatch_reg);
	   }
           m_dispatch_reg->clear();
       }
   }
}

void shader_core_ctx::register_cta_thread_exit( unsigned cta_num )
{
   assert( m_cta_status[cta_num] > 0 );
   m_cta_status[cta_num]--;
   if (!m_cta_status[cta_num]) {
      m_n_active_cta--;
      m_barriers.deallocate_barrier(cta_num);
      shader_CTA_count_unlog(m_sid, 1);
      printf("GPGPU-Sim uArch: Shader %d finished CTA #%d (%lld,%lld), %u CTAs running\n", m_sid, cta_num, gpu_sim_cycle, gpu_tot_sim_cycle,
             m_n_active_cta );
      if( m_n_active_cta == 0 ) {
          assert( m_kernel != NULL );
          m_kernel->dec_running();
          printf("GPGPU-Sim uArch: Shader %u empty (release kernel %u \'%s\').\n", m_sid, m_kernel->get_uid(),
                 m_kernel->name().c_str() );
          if( m_kernel->no_more_ctas_to_run() ) {
              if( !m_kernel->running() ) {
                  printf("GPGPU-Sim uArch: GPU detected kernel \'%s\' finished on shader %u.\n", m_kernel->name().c_str(), m_sid );
                  m_gpu->set_kernel_done( m_kernel );
              }
          }
          m_kernel=NULL;
          fflush(stdout);
      }
   }
}

void gpgpu_sim::shader_print_il1_miss_stat( FILE *fout ) const
{
   unsigned total_il1_misses = 0, total_il1_accesses = 0;
   for ( unsigned i = 0; i < m_shader_config->n_simt_clusters; ++i ) {
         unsigned cluster_il1_misses = 0, cluster_il1_accesses = 0;
         m_cluster[ i ]->print_icache_stats( fout, cluster_il1_accesses, cluster_il1_misses );
         total_il1_misses += cluster_il1_misses;
         total_il1_accesses += cluster_il1_accesses;
   }
   fprintf( fout, "total_il1_misses=%d\n", total_il1_misses );
   fprintf( fout, "total_il1_accesses=%d\n", total_il1_accesses );
}

void gpgpu_sim::shader_print_scheduler_stat( FILE* fout, bool print_dynamic_info ) const
{
    // Print out the stats from the sampling shader core
    const unsigned scheduler_sampling_core = m_shader_config->gpgpu_warp_issue_shader;
    #define STR_SIZE 55
    char name_buff[ STR_SIZE ];
    name_buff[ STR_SIZE - 1 ] = '\0';
    const std::vector< unsigned >& distro
        = print_dynamic_info ?
          m_shader_stats->get_dynamic_warp_issue()[ scheduler_sampling_core ] :
          m_shader_stats->get_warp_slot_issue()[ scheduler_sampling_core ];
    if ( print_dynamic_info ) {
        snprintf( name_buff, STR_SIZE - 1, "dynamic_warp_id" );
    } else {
        snprintf( name_buff, STR_SIZE - 1, "warp_id" );
    }
    fprintf( fout,
             "Shader %d %s issue ditsribution:\n",
             scheduler_sampling_core,
             name_buff );
    const unsigned num_warp_ids = distro.size();
    // First print out the warp ids
    fprintf( fout, "%s:\n", name_buff );
    for ( unsigned warp_id = 0;
          warp_id < num_warp_ids;
          ++warp_id  ) {
        fprintf( fout, "%d, ", warp_id );
    }

    fprintf( fout, "\ndistro:\n" );
    // Then print out the distribution of instuctions issued
    for ( std::vector< unsigned >::const_iterator iter = distro.begin();
          iter != distro.end();
          iter++ ) {
        fprintf( fout, "%d, ", *iter );
    }
    fprintf( fout, "\n" );
}

void gpgpu_sim::shader_print_cache_stats( FILE *fout ) const{

    // L1I
    struct cache_sub_stats total_css;
    struct cache_sub_stats css;

    if(!m_shader_config->m_L1I_config.disabled()){
        total_css.clear();
        css.clear();
        fprintf(fout, "\n========= Core cache stats =========\n");
        fprintf(fout, "L1I_cache:\n");
        for ( unsigned i = 0; i < m_shader_config->n_simt_clusters; ++i ) {
            m_cluster[i]->get_L1I_sub_stats(css);
            total_css += css;
        }
        fprintf(fout, "\tL1I_total_cache_accesses = %u\n", total_css.accesses);
        fprintf(fout, "\tL1I_total_cache_misses = %u\n", total_css.misses);
        if(total_css.accesses > 0){
            fprintf(fout, "\tL1I_total_cache_miss_rate = %.4lf\n", (double)total_css.misses / (double)total_css.accesses);
        }
        fprintf(fout, "\tL1I_total_cache_pending_hits = %u\n", total_css.pending_hits);
        fprintf(fout, "\tL1I_total_cache_reservation_fails = %u\n", total_css.res_fails);
    }

    // L1D
    if(!m_shader_config->m_L1D_config.disabled()){
        total_css.clear();
        css.clear();
        fprintf(fout, "L1D_cache:\n");
        for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++){
            m_cluster[i]->get_L1D_sub_stats(css);

            fprintf( stdout, "\tL1D_cache_core[%d]: Access = %d, Miss = %d, Miss_rate = %.3lf, Pending_hits = %u, Reservation_fails = %u\n",
                     i, css.accesses, css.misses, (double)css.misses / (double)css.accesses, css.pending_hits, css.res_fails);

            total_css += css;
        }
        fprintf(fout, "\tL1D_total_cache_accesses = %u\n", total_css.accesses);
        fprintf(fout, "\tL1D_total_cache_misses = %u\n", total_css.misses);
        if(total_css.accesses > 0){
            fprintf(fout, "\tL1D_total_cache_miss_rate = %.4lf\n", (double)total_css.misses / (double)total_css.accesses);
        }
        fprintf(fout, "\tL1D_total_cache_pending_hits = %u\n", total_css.pending_hits);
        fprintf(fout, "\tL1D_total_cache_reservation_fails = %u\n", total_css.res_fails);
        total_css.print_local_write_misses(fout, "\tL1D_cache"); 
        total_css.print_port_stats(fout, "\tL1D_cache"); 
    }

    // L1C
    if(!m_shader_config->m_L1C_config.disabled()){
        total_css.clear();
        css.clear();
        fprintf(fout, "L1C_cache:\n");
        for ( unsigned i = 0; i < m_shader_config->n_simt_clusters; ++i ) {
            m_cluster[i]->get_L1C_sub_stats(css);
            total_css += css;
        }
        fprintf(fout, "\tL1C_total_cache_accesses = %u\n", total_css.accesses);
        fprintf(fout, "\tL1C_total_cache_misses = %u\n", total_css.misses);
        if(total_css.accesses > 0){
            fprintf(fout, "\tL1C_total_cache_miss_rate = %.4lf\n", (double)total_css.misses / (double)total_css.accesses);
        }
        fprintf(fout, "\tL1C_total_cache_pending_hits = %u\n", total_css.pending_hits);
        fprintf(fout, "\tL1C_total_cache_reservation_fails = %u\n", total_css.res_fails);
    }

    // L1T
    if(!m_shader_config->m_L1T_config.disabled()){
        total_css.clear();
        css.clear();
        fprintf(fout, "L1T_cache:\n");
        for ( unsigned i = 0; i < m_shader_config->n_simt_clusters; ++i ) {
            m_cluster[i]->get_L1T_sub_stats(css);
            total_css += css;
        }
        fprintf(fout, "\tL1T_total_cache_accesses = %u\n", total_css.accesses);
        fprintf(fout, "\tL1T_total_cache_misses = %u\n", total_css.misses);
        if(total_css.accesses > 0){
            fprintf(fout, "\tL1T_total_cache_miss_rate = %.4lf\n", (double)total_css.misses / (double)total_css.accesses);
        }
        fprintf(fout, "\tL1T_total_cache_pending_hits = %u\n", total_css.pending_hits);
        fprintf(fout, "\tL1T_total_cache_reservation_fails = %u\n", total_css.res_fails);
    }
}

void warp_inst_t::print( FILE *fout ) const
{
    if (empty() ) {
        fprintf(fout,"bubble\n" );
        return;
    } else 
        fprintf(fout,"0x%04x ", pc );
    fprintf(fout, "w%02d[", m_warp_id);
    for (unsigned j=0; j<m_config->warp_size; j++)
        fprintf(fout, "%c", (active(j)?'1':'0') );
    fprintf(fout, "]: ");
    ptx_print_insn( pc, fout );
    fprintf(fout, "\n");
}
void shader_core_ctx::incexecstat(warp_inst_t *&inst)
{
	if(inst->mem_op==TEX)
		inctex_stat(inst->active_count(),1);

    // Latency numbers for next operations are used to scale the power values
    // for special operations, according observations from microbenchmarking
    // TODO: put these numbers in the xml configuration

	switch(inst->sp_op){
	case INT__OP:
		incialu_stat(inst->active_count(),25);
		break;
	case INT_MUL_OP:
		incimul_stat(inst->active_count(),7.2);
		break;
	case INT_MUL24_OP:
		incimul24_stat(inst->active_count(),4.2);
		break;
	case INT_MUL32_OP:
		incimul32_stat(inst->active_count(),4);
		break;
	case INT_DIV_OP:
		incidiv_stat(inst->active_count(),40);
		break;
	case FP__OP:
		incfpalu_stat(inst->active_count(),1);
		break;
	case FP_MUL_OP:
		incfpmul_stat(inst->active_count(),1.8);
		break;
	case FP_DIV_OP:
		incfpdiv_stat(inst->active_count(),48);
		break;
	case FP_SQRT_OP:
		inctrans_stat(inst->active_count(),25);
		break;
	case FP_LG_OP:
		inctrans_stat(inst->active_count(),35);
		break;
	case FP_SIN_OP:
		inctrans_stat(inst->active_count(),12);
		break;
	case FP_EXP_OP:
		inctrans_stat(inst->active_count(),35);
		break;
	default:
		break;
	}
}
void shader_core_ctx::print_stage(unsigned int stage, FILE *fout ) const
{
   m_pipeline_reg[stage].print(fout);
}

void shader_core_ctx::display_simt_state(FILE *fout, int mask ) const
{
    if ( (mask & 4) && m_config->model == POST_DOMINATOR ) {
       fprintf(fout,"per warp SIMT control-flow state:\n");
       unsigned n = m_config->n_thread_per_shader / m_config->warp_size;
       for (unsigned i=0; i < n; i++) {
          unsigned nactive = 0;
          for (unsigned j=0; j<m_config->warp_size; j++ ) {
             unsigned tid = i*m_config->warp_size + j;
             int done = ptx_thread_done(tid);
             nactive += (ptx_thread_done(tid)?0:1);
             if ( done && (mask & 8) ) {
                unsigned done_cycle = m_thread[tid]->donecycle();
                if ( done_cycle ) {
                   printf("\n w%02u:t%03u: done @ cycle %u", i, tid, done_cycle );
                }
             }
          }
          if ( nactive == 0 ) {
             continue;
          }
          m_simt_stack[i]->print(fout);
       }
       fprintf(fout,"\n");
    }
}

void ldst_unit::print(FILE *fout) const
{
    fprintf(fout,"LD/ST unit  = ");
    m_dispatch_reg->print(fout);
    m_dispatch_reg->dump_access(fout);
    if ( m_mem_rc != NO_RC_FAIL ) {
        fprintf(fout,"              LD/ST stall condition: ");
        switch ( m_mem_rc ) {
        case BK_CONF:        fprintf(fout,"BK_CONF"); break;
        case MSHR_RC_FAIL:   fprintf(fout,"MSHR_RC_FAIL"); break;
        case ICNT_RC_FAIL:   fprintf(fout,"ICNT_RC_FAIL"); break;
        case COAL_STALL:     fprintf(fout,"COAL_STALL"); break;
        case WB_ICNT_RC_FAIL: fprintf(fout,"WB_ICNT_RC_FAIL"); break;
        case WB_CACHE_RSRV_FAIL: fprintf(fout,"WB_CACHE_RSRV_FAIL"); break;
        case N_MEM_STAGE_STALL_TYPE: fprintf(fout,"N_MEM_STAGE_STALL_TYPE"); break;
        default: abort();
        }
        fprintf(fout,"\n");
    }
    fprintf(fout,"LD/ST wb    = ");
    m_next_wb.print(fout);
    fprintf(fout, "Last LD/ST writeback @ %llu + %llu (gpu_sim_cycle+gpu_tot_sim_cycle)\n",
                  m_last_inst_gpu_sim_cycle, m_last_inst_gpu_tot_sim_cycle );
    fprintf(fout,"Pending register writes:\n");
    std::map<unsigned/*warp_id*/, std::map<unsigned/*regnum*/,unsigned/*count*/> >::const_iterator w;
    for( w=m_pending_writes.begin(); w!=m_pending_writes.end(); w++ ) {
        unsigned warp_id = w->first;
        const std::map<unsigned/*regnum*/,unsigned/*count*/> &warp_info = w->second;
        if( warp_info.empty() ) 
            continue;
        fprintf(fout,"  w%2u : ", warp_id );
        std::map<unsigned/*regnum*/,unsigned/*count*/>::const_iterator r;
        for( r=warp_info.begin(); r!=warp_info.end(); ++r ) {
            fprintf(fout,"  %u(%u)", r->first, r->second );
        }
        fprintf(fout,"\n");
    }
    m_L1C->display_state(fout);
    m_L1T->display_state(fout);
    if( !m_config->m_L1D_config.disabled() )
    	m_L1D->display_state(fout);
    fprintf(fout,"LD/ST response FIFO (occupancy = %zu):\n", m_response_fifo.size() );
    for( std::list<mem_fetch*>::const_iterator i=m_response_fifo.begin(); i != m_response_fifo.end(); i++ ) {
        const mem_fetch *mf = *i;
        mf->print(fout);
    }
}

void shader_core_ctx::display_pipeline(FILE *fout, int print_mem, int mask ) const
{
   fprintf(fout, "=================================================\n");
   fprintf(fout, "shader %u at cycle %Lu+%Lu (%u threads running)\n", m_sid, 
           gpu_tot_sim_cycle, gpu_sim_cycle, m_not_completed);
   fprintf(fout, "=================================================\n");

   dump_warp_state(fout);
   fprintf(fout,"\n");

   m_L1I->display_state(fout, true);

   fprintf(fout, "IF/ID       = ");
   if( !m_inst_fetch_buffer.m_valid )
       fprintf(fout,"bubble\n");
   else {
       fprintf(fout,"w%2u : pc = 0x%x, nbytes = %u\n", 
               m_inst_fetch_buffer.m_warp_id,
               m_inst_fetch_buffer.m_pc, 
               m_inst_fetch_buffer.m_nbytes );
   }
   fprintf(fout,"\nibuffer status:\n");
   for( unsigned i=0; i<m_config->max_warps_per_shader; i++) {
       if( !m_warp[i].ibuffer_empty() ) 
           m_warp[i].print_ibuffer(fout);
   }
   fprintf(fout,"\n");
   display_simt_state(fout,mask);
   fprintf(fout, "-------------------------- Scoreboard\n");
   m_scoreboard->printContents();
/*
   fprintf(fout,"ID/OC (SP)  = ");
   print_stage(ID_OC_SP, fout);
   fprintf(fout,"ID/OC (SFU) = ");
   print_stage(ID_OC_SFU, fout);
   fprintf(fout,"ID/OC (MEM) = ");
   print_stage(ID_OC_MEM, fout);
*/
   fprintf(fout, "-------------------------- OP COL\n");
   m_operand_collector.dump(fout);
/* fprintf(fout, "OC/EX (SP)  = ");
   print_stage(OC_EX_SP, fout);
   fprintf(fout, "OC/EX (SFU) = ");
   print_stage(OC_EX_SFU, fout);
   fprintf(fout, "OC/EX (MEM) = ");
   print_stage(OC_EX_MEM, fout);
*/
   fprintf(fout, "-------------------------- Pipe Regs\n");

   for (unsigned i = 0; i < N_PIPELINE_STAGES; i++) {
       fprintf(fout,"--- %s ---\n",pipeline_stage_name_decode[i]);
       print_stage(i,fout);fprintf(fout,"\n");
   }

   fprintf(fout, "-------------------------- Fu\n");
   for( unsigned n=0; n < m_num_function_units; n++ ){
       m_fu[n]->print(fout);
       fprintf(fout, "---------------\n");
   }
   fprintf(fout, "-------------------------- other:\n");

   for(unsigned i=0; i<num_result_bus; i++){
	   std::string bits = m_result_bus[i]->to_string();
	   fprintf(fout, "EX/WB sched[%d]= %s\n", i, bits.c_str() );
   }
   fprintf(fout, "EX/WB      = ");
   print_stage(EX_WB, fout);
   fprintf(fout, "\n");
   fprintf(fout, "Last EX/WB writeback @ %llu + %llu (gpu_sim_cycle+gpu_tot_sim_cycle)\n",
                 m_last_inst_gpu_sim_cycle, m_last_inst_gpu_tot_sim_cycle );

   if( m_active_threads.count() <= 2*m_config->warp_size ) {
       fprintf(fout,"Active Threads : ");
       unsigned last_warp_id = -1;
       for(unsigned tid=0; tid < m_active_threads.size(); tid++ ) {
           unsigned warp_id = tid/m_config->warp_size;
           if( m_active_threads.test(tid) ) {
               if( warp_id != last_warp_id ) {
                   fprintf(fout,"\n  warp %u : ", warp_id );
                   last_warp_id=warp_id;
               }
               fprintf(fout,"%u ", tid );
           }
       }
   }

}

unsigned int shader_core_config::max_cta( const kernel_info_t &k ) const
{
   unsigned threads_per_cta  = k.threads_per_cta();
   const class function_info *kernel = k.entry();
   unsigned int padded_cta_size = threads_per_cta;
   if (padded_cta_size%warp_size) 
      padded_cta_size = ((padded_cta_size/warp_size)+1)*(warp_size);

   //Limit by n_threads/shader
   unsigned int result_thread = n_thread_per_shader / padded_cta_size;

   const struct gpgpu_ptx_sim_kernel_info *kernel_info = ptx_sim_kernel_info(kernel);

   //Limit by shmem/shader
   unsigned int result_shmem = (unsigned)-1;
   if (kernel_info->smem > 0)
      result_shmem = gpgpu_shmem_size / kernel_info->smem;

   //Limit by register count, rounded up to multiple of 4.
   unsigned int result_regs = (unsigned)-1;
   if (kernel_info->regs > 0)
      result_regs = gpgpu_shader_registers / (padded_cta_size * ((kernel_info->regs+3)&~3));

   //Limit by CTA
   unsigned int result_cta = max_cta_per_core;

   unsigned result = result_thread;
   result = gs_min2(result, result_shmem);
   result = gs_min2(result, result_regs);
   result = gs_min2(result, result_cta);

   static const struct gpgpu_ptx_sim_kernel_info* last_kinfo = NULL;
   if (last_kinfo != kernel_info) {   //Only print out stats if kernel_info struct changes
      last_kinfo = kernel_info;
      printf ("GPGPU-Sim uArch: CTA/core = %u, limited by:", result);
      if (result == result_thread) printf (" threads");
      if (result == result_shmem) printf (" shmem");
      if (result == result_regs) printf (" regs");
      if (result == result_cta) printf (" cta_limit");
      printf ("\n");
   }

    //gpu_max_cta_per_shader is limited by number of CTAs if not enough to keep all cores busy    
    if( k.num_blocks() < result*num_shader() ) { 
       result = k.num_blocks() / num_shader();
       if (k.num_blocks() % num_shader())
          result++;
    }

    assert( result <= MAX_CTA_PER_SHADER );
   if (result < 1) {
      printf ("GPGPU-Sim uArch: ERROR ** Kernel requires more resources than shader has.\n");
      abort();
   }

   return result;
}

void shader_core_ctx::cycle()
{
    m_stats->shader_cycles[m_sid]++;
    check_num_aborts();
    //unsigned long long num_newly_committed_tx = g_tm_global_statistics.m_n_commits - g_tm_global_statistics.m_n_prev_commits;
    //if (num_newly_committed_tx >= 5000) {
    //   m_scoreboard->set_num_tm_tokens((num_newly_committed_tx/5000)*(num_newly_committed_tx/5000));
    //   g_tm_global_statistics.m_n_prev_commits = g_tm_global_statistics.m_n_commits;
    //}
    writeback();
    execute();
    read_operands();
    issue();
    decode();
    fetch();
}

void shader_core_ctx::profile_thread_states()
{
   for(unsigned tid=0; tid < m_config->n_thread_per_shader; tid++) {
      if(m_threadState[tid].m_active_set) {
         // This hw thread was ever active

         if(m_threadState[tid].m_active) {
            // Thread exists

            thread_state_stat* thread_state;
            if(m_thread[tid]->is_in_transaction() != NULL)
               thread_state = &m_threadState[tid].m_state_cycles_tx_useful;
            else if(m_threadState[tid].m_atomic_mode)
               thread_state = &m_threadState[tid].m_state_cycles_atomic;
            else
               thread_state = &m_threadState[tid].m_state_cycles_nonatomic;

            unsigned warp_id = tid / m_config->warp_size;
            unsigned lane_id = tid % m_config->warp_size;
            
	    if( m_threadState[tid].m_in_pipeline > 0 ) {
               // At least one instruction in pipeline
               thread_state->m_state_cycles[INPIPE] += 1;
	    } else if (m_simt_stack[warp_id]->get_stack_size() == 0) {
	       assert(m_warp[warp_id].functional_done());
               thread_state->m_state_cycles[WAIT_INITKERNEL] += 1;
	    } else {
               // No instructions in pipeline
	       
               // Check if thread is active (in top active mask)
               const active_mask_t &active_mask = m_simt_stack[warp_id]->get_active_mask();

               // Check if valid instruction in ibuffer
               bool valid_insn = false;
               const warp_inst_t* pI = NULL;
               if( !m_warp[warp_id].waiting() && !m_warp[warp_id].ibuffer_empty() ) {
                  pI = m_warp[warp_id].ibuffer_next_inst();
                  unsigned pc,rpc;
                  m_simt_stack[warp_id]->get_pdom_stack_top_info(&pc,&rpc);
                  if( pI && pc == pI->pc ) {
                     valid_insn = true;
                  }
               }

               if( active_mask.test(lane_id) ) {
                  // Thread is active

                  if( valid_insn && m_scoreboard->checkCollision(warp_id, pI) ) {
                     if (m_scoreboard->inTxCommit(warp_id)) {
                        // Waiting for commit or cleaning
			if (m_threadState[tid].m_active_in_cleaning) {
			    assert(m_config->tlw_use_logical_temporal_cd);
			    thread_state->m_state_cycles[WAIT_CLEANING] += 1;
			} else {
                            thread_state->m_state_cycles[WAIT_COMMIT] += 1;
			}
                     }
                     else if( m_scoreboard->checkTMToken(warp_id, pI) ) {
                        // TM Concurrency control hazard
			if (m_scoreboard->someInCommit())
                            thread_state->m_state_cycles[WAIT_CONCCONTROL_SOME_COMMIT] += 1;
			else
                            thread_state->m_state_cycles[WAIT_CONCCONTROL_NONE_COMMIT] += 1;
                     } else {
                        // Data hazard
                        data_hazard_t hazard_type = m_scoreboard->getDataHazardType(warp_id, pI);
                        if(hazard_type == ALU_HAZARD)
                           thread_state->m_state_cycles[WAIT_DATAHAZARD_ALU] += 1;
                        else if(hazard_type == MEM_HAZARD)
                           thread_state->m_state_cycles[WAIT_DATAHAZARD_MEM] += 1;
                        else
                           // Due to waiting for inflight instructions to exit before issuing txcommit
                           thread_state->m_state_cycles[WAIT_DATAHAZARD_OTHER] += 1;
                     }
                  } else if( m_warp[warp_id].get_n_atomic() > 0 ) {
                     // In-flight atomics
                     thread_state->m_state_cycles[WAIT_ATOMIC] += 1;
                  } else if ( m_warp[warp_id].functional_done() ) {
                     // Waiting to be initialized with a kernel
                     thread_state->m_state_cycles[WAIT_INITKERNEL] += 1;
                  } else if ( warp_waiting_at_barrier(warp_id) || warp_waiting_at_mem_barrier(warp_id) ) {
                     // Waiting for CTA or mem barrier
                     thread_state->m_state_cycles[WAIT_BARRIER] += 1;
                  } else if ( m_warp[warp_id].ibuffer_empty() ) {
                     // Waiting for ibuffer to fill
                     thread_state->m_state_cycles[WAIT_IBUFFER] += 1;
                  } else {
                     // Waiting to be scheduled or issued for some other reason
                     thread_state->m_state_cycles[WAIT_SCHEDULER] += 1;
                  }
               } else {
                  // Thread inactive due to control flow div
                  if(m_scoreboard->inTxCommit(warp_id)) {
                     // This thread is masked off while warp is committing or cleaning
		     if (m_threadState[tid].m_active_in_cleaning) {
			 assert(m_config->tlw_use_logical_temporal_cd);
			 thread_state->m_state_cycles[WAIT_CONTFLOWDIV_CLEANING] += 1;
	             } else if (m_threadState[tid].m_active_in_commit) {
                         thread_state->m_state_cycles[WAIT_CONTFLOWDIV_COMMITEXIT] += 1;
		     } else {
                         thread_state->m_state_cycles[WAIT_CONTFLOWDIV_COMMIT] += 1;
		     }
                  } else if (valid_insn && m_scoreboard->checkTMToken(warp_id, pI)) {
                     // This thread is masked off while warp is blocked by concurrency control
		     if (m_scoreboard->someInCommit())
                         thread_state->m_state_cycles[WAIT_CONTFLOWDIV_CONCCONTROL_SOME_COMMIT] += 1;
		     else
                         thread_state->m_state_cycles[WAIT_CONTFLOWDIV_CONCCONTROL_NONE_COMMIT] += 1;
                  } else if (m_threadState[tid].m_active_in_commit) {
                     // Thread is masked off because it PASSED
                     thread_state->m_state_cycles[WAIT_CONTFLOWDIV_COMMITEXIT] += 1;
                  } else {
                     thread_state->m_state_cycles[WAIT_CONTFLOWDIV_NORMAL] += 1;
                  }
               }
            }
         } else {
            // Hw thread was once active but now isnt
            m_stats->gpgpu_global_hw_thread_idle_cycles += 1;
         }
      }
   }
}

// Flushes all content of the cache to memory

void shader_core_ctx::cache_flush()
{
   m_ldst_unit->flush();
}

// modifiers
std::list<opndcoll_rfu_t::op_t> opndcoll_rfu_t::arbiter_t::allocate_reads() 
{
   std::list<op_t> result;  // a list of registers that (a) are in different register banks, (b) do not go to the same operand collector

   int input;
   int output;
   int _inputs = m_num_banks;
   int _outputs = m_num_collectors;
   int _square = ( _inputs > _outputs ) ? _inputs : _outputs;
   assert(_square > 0);
   int _pri = (int)m_last_cu;

   // Clear matching
   for ( int i = 0; i < _inputs; ++i ) 
      _inmatch[i] = -1;
   for ( int j = 0; j < _outputs; ++j ) 
      _outmatch[j] = -1;

   for( unsigned i=0; i<m_num_banks; i++) {
      for( unsigned j=0; j<m_num_collectors; j++) {
         assert( i < (unsigned)_inputs );
         assert( j < (unsigned)_outputs );
         _request[i][j] = 0;
      }
      if( !m_queue[i].empty() ) {
         const op_t &op = m_queue[i].front();
         int oc_id = op.get_oc_id();
         assert( i < (unsigned)_inputs );
         assert( oc_id < _outputs );
         _request[i][oc_id] = 1;
      }
      if( m_allocated_bank[i].is_write() ) {
         assert( i < (unsigned)_inputs );
         _inmatch[i] = 0; // write gets priority
      }
   }

   ///// wavefront allocator from booksim... --->
   
   // Loop through diagonals of request matrix

   for ( int p = 0; p < _square; ++p ) {
      output = ( _pri + p ) % _square;

      // Step through the current diagonal
      for ( input = 0; input < _inputs; ++input ) {
          assert( input < _inputs );
          assert( output < _outputs );
         if ( ( output < _outputs ) && 
              ( _inmatch[input] == -1 ) && 
              ( _outmatch[output] == -1 ) &&
              ( _request[input][output]/*.label != -1*/ ) ) {
            // Grant!
            _inmatch[input] = output;
            _outmatch[output] = input;
         }

         output = ( output + 1 ) % _square;
      }
   }

   // Round-robin the priority diagonal
   _pri = ( _pri + 1 ) % _square;

   /// <--- end code from booksim

   m_last_cu = _pri;
   for( unsigned i=0; i < m_num_banks; i++ ) {
      if( _inmatch[i] != -1 ) {
         if( !m_allocated_bank[i].is_write() ) {
            unsigned bank = (unsigned)i;
            op_t &op = m_queue[bank].front();
            result.push_back(op);
            m_queue[bank].pop_front();
         }
      }
   }

   return result;
}

barrier_set_t::barrier_set_t( unsigned max_warps_per_core, unsigned max_cta_per_core )
{
   m_max_warps_per_core = max_warps_per_core;
   m_max_cta_per_core = max_cta_per_core;
   if( max_warps_per_core > WARP_PER_CTA_MAX ) {
      printf("ERROR ** increase WARP_PER_CTA_MAX in shader.h from %u to >= %u or warps per cta in gpgpusim.config\n",
             WARP_PER_CTA_MAX, max_warps_per_core );
      exit(1);
   }
   m_warp_active.reset();
   m_warp_at_barrier.reset();
}

// during cta allocation
void barrier_set_t::allocate_barrier( unsigned cta_id, warp_set_t warps )
{
   assert( cta_id < m_max_cta_per_core );
   cta_to_warp_t::iterator w=m_cta_to_warps.find(cta_id);
   assert( w == m_cta_to_warps.end() ); // cta should not already be active or allocated barrier resources
   m_cta_to_warps[cta_id] = warps;
   assert( m_cta_to_warps.size() <= m_max_cta_per_core ); // catch cta's that were not properly deallocated
  
   m_warp_active |= warps;
   m_warp_at_barrier &= ~warps;
}

// during cta deallocation
void barrier_set_t::deallocate_barrier( unsigned cta_id )
{
   cta_to_warp_t::iterator w=m_cta_to_warps.find(cta_id);
   if( w == m_cta_to_warps.end() )
      return;
   warp_set_t warps = w->second;
   warp_set_t at_barrier = warps & m_warp_at_barrier;
   assert( at_barrier.any() == false ); // no warps stuck at barrier
   warp_set_t active = warps & m_warp_active;
   assert( active.any() == false ); // no warps in CTA still running
   m_warp_active &= ~warps;
   m_warp_at_barrier &= ~warps;
   m_cta_to_warps.erase(w);
}

// individual warp hits barrier
void barrier_set_t::warp_reaches_barrier( unsigned cta_id, unsigned warp_id )
{
   cta_to_warp_t::iterator w=m_cta_to_warps.find(cta_id);

   if( w == m_cta_to_warps.end() ) { // cta is active
      printf("ERROR ** cta_id %u not found in barrier set on cycle %llu+%llu...\n", cta_id, gpu_tot_sim_cycle, gpu_sim_cycle );
      dump();
      abort();
   }
   assert( w->second.test(warp_id) == true ); // warp is in cta

   m_warp_at_barrier.set(warp_id);

   warp_set_t warps_in_cta = w->second;
   warp_set_t at_barrier = warps_in_cta & m_warp_at_barrier;
   warp_set_t active = warps_in_cta & m_warp_active;

   if( at_barrier == active ) {
      // all warps have reached barrier, so release waiting warps...
      m_warp_at_barrier &= ~at_barrier;
   }
}

// fetching a warp
bool barrier_set_t::available_for_fetch( unsigned warp_id ) const
{
   return m_warp_active.test(warp_id) && m_warp_at_barrier.test(warp_id);
}

// warp reaches exit 
void barrier_set_t::warp_exit( unsigned warp_id )
{
   // caller needs to verify all threads in warp are done, e.g., by checking PDOM stack to 
   // see it has only one entry during exit_impl()
   m_warp_active.reset(warp_id);

   // test for barrier release 
   cta_to_warp_t::iterator w=m_cta_to_warps.begin(); 
   for (; w != m_cta_to_warps.end(); ++w) {
      if (w->second.test(warp_id) == true) break; 
   }
   warp_set_t warps_in_cta = w->second;
   warp_set_t at_barrier = warps_in_cta & m_warp_at_barrier;
   warp_set_t active = warps_in_cta & m_warp_active;

   if( at_barrier == active ) {
      // all warps have reached barrier, so release waiting warps...
      m_warp_at_barrier &= ~at_barrier;
   }
}

// assertions
bool barrier_set_t::warp_waiting_at_barrier( unsigned warp_id ) const
{ 
   return m_warp_at_barrier.test(warp_id);
}

void barrier_set_t::dump() const
{
   printf( "barrier set information\n");
   printf( "  m_max_cta_per_core = %u\n",  m_max_cta_per_core );
   printf( "  m_max_warps_per_core = %u\n", m_max_warps_per_core );
   printf( "  cta_to_warps:\n");
   
   cta_to_warp_t::const_iterator i;
   for( i=m_cta_to_warps.begin(); i!=m_cta_to_warps.end(); i++ ) {
      unsigned cta_id = i->first;
      warp_set_t warps = i->second;
      printf("    cta_id %u : %s\n", cta_id, warps.to_string().c_str() );
   }
   printf("  warp_active: %s\n", m_warp_active.to_string().c_str() );
   printf("  warp_at_barrier: %s\n", m_warp_at_barrier.to_string().c_str() );
   fflush(stdout); 
}

void shader_core_ctx::warp_exit( unsigned warp_id )
{
	bool done = true;
	for (	unsigned i = warp_id*get_config()->warp_size;
			i < (warp_id+1)*get_config()->warp_size;
			i++ ) {

//		if(this->m_thread[i]->m_functional_model_thread_state && this->m_thread[i].m_functional_model_thread_state->donecycle()==0) {
//			done = false;
//		}


		if (m_thread[i] && !m_thread[i]->is_done()) done = false;
	}
	//if (m_warp[warp_id].get_n_completed() == get_config()->warp_size)
	//if (this->m_simt_stack[warp_id]->get_num_entries() == 0)
	if (done)
		m_barriers.warp_exit( warp_id );
}

bool shader_core_ctx::warp_waiting_at_barrier( unsigned warp_id ) const
{
   return m_barriers.warp_waiting_at_barrier(warp_id);
}

bool shader_core_ctx::warp_waiting_at_mem_barrier( unsigned warp_id ) 
{
   if( !m_warp[warp_id].get_membar() ) 
      return false;
   if( !m_scoreboard->pendingWrites(warp_id) ) {
      m_warp[warp_id].clear_membar();
      return false;
   }
   return true;
}

void shader_core_ctx::set_max_cta( const kernel_info_t &kernel ) 
{
    // calculate the max cta count and cta size for local memory address mapping
    kernel_max_cta_per_shader = m_config->max_cta(kernel);
    unsigned int gpu_cta_size = kernel.threads_per_cta();
    kernel_padded_threads_per_cta = (gpu_cta_size%m_config->warp_size) ? 
        m_config->warp_size*((gpu_cta_size/m_config->warp_size)+1) : 
        gpu_cta_size;
}

void shader_core_ctx::decrement_atomic_count( unsigned wid, unsigned n )
{
   assert( m_warp[wid].get_n_atomic() >= n );
   m_warp[wid].dec_n_atomic(n);
}

void shader_core_ctx::decrement_logical_tm_req_count( unsigned wid, unsigned n )
{
   assert( m_warp[wid].get_n_logical_tm_req() >= n );
   m_warp[wid].dec_n_logical_tm_req(n);
}

bool shader_core_ctx::fetch_unit_response_buffer_full() const
{
    return false;
}

void shader_core_ctx::accept_fetch_response( mem_fetch *mf )
{
    mf->set_status(IN_SHADER_FETCHED,gpu_sim_cycle+gpu_tot_sim_cycle);
    m_L1I->fill(mf,gpu_sim_cycle+gpu_tot_sim_cycle);
}

bool shader_core_ctx::ldst_unit_response_buffer_full() const
{
    return m_ldst_unit->response_buffer_full();
}

void shader_core_ctx::accept_ldst_unit_response(mem_fetch * mf) 
{
   m_ldst_unit->fill(mf);
}

void shader_core_ctx::set_kernel( kernel_info_t *k ) 
{
   assert(k);
   m_kernel=k; 
   k->inc_running(); 
   printf("GPGPU-Sim uArch: Shader %d bind to kernel %u \'%s\'\n", m_sid, m_kernel->get_uid(),
            m_kernel->name().c_str() );
   // detect the need to activate perfect memory based on kernel name 
   const char * pmem_exempt_kernel = m_gpu->get_config().get_pmem_exempt_kernel();
   if (pmem_exempt_kernel != NULL) {
      // if kernel name does not match the option, simulate it in perfect memory 
      // otherwise, let the other option control it 
      std::string kernel_name = m_kernel->name(); 
      if (kernel_name.find(pmem_exempt_kernel) != std::string::npos) {
         set_perfect_memory(m_config->gpgpu_perfect_mem);
      } else {
         printf("GPGPU-Sim uArch: Kernel %s simulated with perfect memory for fast forwarding\n", kernel_name.c_str()); 
         set_perfect_memory(true);
      }
   }
}


void shader_core_ctx::store_ack( class mem_fetch *mf )
{
    assert( mf->get_type() == WRITE_ACK  || ( m_config->gpgpu_perfect_mem && mf->get_is_write() ) );
    unsigned warp_id = mf->get_wid();
    m_warp[warp_id].dec_store_req();
}

void shader_core_ctx::print_icache_stats( FILE *fp, unsigned& il1_accesses, unsigned& il1_misses ) {
   if(!m_config->m_L1I_config.disabled())
      m_L1I->print(fp, il1_accesses, il1_misses);
}

void shader_core_ctx::print_cache_stats( FILE *fp, unsigned& dl1_accesses, unsigned& dl1_misses ) {
   m_ldst_unit->print_cache_stats( fp, dl1_accesses, dl1_misses );
}

void shader_core_ctx::get_cache_stats(cache_stats &cs){
    // Adds stats from each cache to 'cs'
    cs += m_L1I->get_stats(); // Get L1I stats
    m_ldst_unit->get_cache_stats(cs); // Get L1D, L1C, L1T stats
}

void shader_core_ctx::get_L1I_sub_stats(struct cache_sub_stats &css) const{
    if(m_L1I)
        m_L1I->get_sub_stats(css);
}
void shader_core_ctx::get_L1D_sub_stats(struct cache_sub_stats &css) const{
    m_ldst_unit->get_L1D_sub_stats(css);
}
void shader_core_ctx::get_L1C_sub_stats(struct cache_sub_stats &css) const{
    m_ldst_unit->get_L1C_sub_stats(css);
}
void shader_core_ctx::get_L1T_sub_stats(struct cache_sub_stats &css) const{
    m_ldst_unit->get_L1T_sub_stats(css);
}

void shader_core_ctx::get_icnt_power_stats(long &n_simt_to_mem, long &n_mem_to_simt) const{
	n_simt_to_mem += m_stats->n_simt_to_mem[m_sid];
	n_mem_to_simt += m_stats->n_mem_to_simt[m_sid];
}

bool shd_warp_t::functional_done() const
{
    return get_n_completed() == m_warp_size;
}

bool shd_warp_t::hardware_done() const
{
    return functional_done() && stores_done() && !inst_in_pipeline(); 
}

bool shd_warp_t::waiting() 
{
    if ( functional_done() ) {
        // waiting to be initialized with a kernel
        return true;
    } else if ( m_shader->warp_waiting_at_barrier(m_warp_id) ) {
        // waiting for other warps in CTA to reach barrier
        return true;
    } else if ( m_shader->warp_waiting_at_mem_barrier(m_warp_id) ) {
        // waiting for memory barrier
        return true;
    } else if ( m_n_atomic >0 ) {
        // waiting for atomic operation to complete at memory:
        // this stall is not required for accurate timing model, but rather we
        // stall here since if a call/return instruction occurs in the meantime
        // the functional execution of the atomic when it hits DRAM can cause
        // the wrong register to be read.
        return true;
    }
    return false;
}

void shd_warp_t::print( FILE *fout ) const
{
    if( !done_exit() ) {
        fprintf( fout, "w%02u npc: 0x%04x, done:%c%c%c%c:%2u i:%u s:%u a:%u (done: ", 
                m_warp_id,
                m_next_pc,
                (functional_done()?'f':' '),
                (stores_done()?'s':' '),
                (inst_in_pipeline()?' ':'i'),
                (done_exit()?'e':' '),
                n_completed,
                m_inst_in_pipeline, 
                m_stores_outstanding,
                m_n_atomic );
        for (unsigned i = m_warp_id*m_warp_size; i < (m_warp_id+1)*m_warp_size; i++ ) {
          if ( m_shader->ptx_thread_done(i) ) fprintf(fout,"1");
          else fprintf(fout,"0");
          if ( (((i+1)%4) == 0) && (i+1) < (m_warp_id+1)*m_warp_size ) 
             fprintf(fout,",");
        }
        fprintf(fout,") ");
        fprintf(fout," active=%s", m_active_threads.to_string().c_str() );
        fprintf(fout," last fetched @ %5llu", m_last_fetch);
        if( m_imiss_pending ) 
            fprintf(fout," i-miss pending");
        fprintf(fout,"\n");
    }
}

void shd_warp_t::print_ibuffer( FILE *fout ) const
{
    fprintf(fout,"  ibuffer[%2u] : ", m_warp_id );
    for( unsigned i=0; i < IBUFFER_SIZE; i++) {
        const inst_t *inst = m_ibuffer[i].m_inst;
        if( inst ) inst->print_insn(fout);
        else if( m_ibuffer[i].m_valid ) 
           fprintf(fout," <invalid instruction> ");
        else fprintf(fout," <empty> ");
    }
    fprintf(fout,"\n");
}

thread_ctx_t* shader_core_ctx::get_thread_ctx(unsigned tid) const 
{ 
    assert(tid < m_config->n_thread_per_shader); 
    return &m_threadState[tid]; 
}

class ptx_thread_info* shader_core_ctx::get_func_thread_info(unsigned tid) const 
{ 
    assert(tid < m_config->n_thread_per_shader); 
    return m_thread[tid]; 
}

void shader_core_ctx::begin_callback( unsigned thread_id, unsigned warp_id, address_type restart_pc )
{
    m_simt_stack[warp_id]->txbegin(restart_pc); // this triggers a divergence 
}

void shader_core_ctx::initiate_timing_model_transaction_commit( unsigned warp_id )
{
    assert(0);  // deprecated function -- should never be called 
    #if 0
    ptx_thread_info *thd = get_transactional_thread(); 
    assert(thd != NULL); 
    tm_manager_inf *ct_tm = thd->is_in_transaction(); 
    assert(thd != NULL); 
    if (ct_tm->nesting_level() > 1) {
        // sub-level commit - just update functional tm manager
        bool commit_success = ct_tm->commit(true); 
        assert(commit_success == false); 
    } else {
        // top-level commit - change state in transactional cache and scoreboard to initiate the commit timing 
    }
    #endif 
}

void shader_core_ctx::commit_warp_cleanup(unsigned warp_id)
{
   if (m_simt_stack[warp_id]->in_transaction() == false) {
       m_operand_collector.history_file_commit_clear(); 
       m_scoreboard->releaseTMToken(warp_id); 

       // Thread profiler code - clear threads from commit
       for ( unsigned t=0; t < m_config->warp_size; t++ ) {
          unsigned tid=m_config->warp_size*warp_id+t;
          m_threadState[tid].m_active_in_commit = false;
          m_threadState[tid].m_active_in_cleaning = false;
       }

       unsigned hwwarpid = m_sid*m_config->max_warps_per_shader + warp_id; // global hw warp id across all shader cores
       m_gpu->get_coherence_manager()->tm_warp_commited(hwwarpid);
   }
   init_aborted_tx_pts(warp_id);
}

void shader_core_ctx::commit_callback( unsigned thread_id, unsigned warp_id, address_type commit_pc )
{
    // COH model commit callback
    unsigned hwtid = m_sid*m_config->n_thread_per_shader + thread_id; // global hw tid across all shader cores
    unsigned hwwarpid = m_sid*m_config->max_warps_per_shader + warp_id; // global hw warp id across all shader cores
    m_gpu->get_coherence_manager()->tm_commited(hwtid, hwwarpid, m_thread[thread_id]);

    // update the pdom stack to swtich to the next thread in transaction 
    m_simt_stack[warp_id]->txcommit(thread_id, commit_pc); // this triggers a divergence 

    // if this the last thread in the warp to commit, release TM token in the scoreboard 
    // TODO: move this to where the warp is commiting in uarch
    if (not m_config->timing_mode_vb_commit && not g_tm_options.m_eager_warptm_enabled) {
        commit_warp_cleanup(warp_id); 
    }
}

bool null_match( warp_inst_t* warp_inst ) { return true; }
bool is_tcommit( warp_inst_t* warp_inst ) { return warp_inst->is_tcommit; }

void shader_core_ctx::rollback_callback( unsigned thread_id, unsigned warp_id, address_type pc ) 
{
    // COH model abort callback
    unsigned hwtid = m_sid*m_config->n_thread_per_shader + thread_id; // global hw tid across all shader cores
    m_gpu->get_coherence_manager()->tm_aborted(hwtid, m_thread[thread_id]);

    //HACK: will roll the whole warp back
    m_simt_stack[warp_id]->txabort(thread_id); // this triggers a divergence 

    // tell the operand collector to start restoring pre-transaction state  
    m_operand_collector.history_file_start_rollback(); 
    
    // mask the thread out in any inflight instruction in the pipeline 
    bool rolled_back = false;
    #if 0
    warp_inst_t * fexec_pipeline_reg = m_pipeline_reg[ID_OC_SP]; // the register under func exec
    if (!fexec_pipeline_reg->empty() and fexec_pipeline_reg->warp_id() == warp_id) {
        fexec_pipeline_reg->set_not_active(thread_id % m_config->warp_size);
        rolled_back = true;
    }
    fexec_pipeline_reg = m_pipeline_reg[ID_OC_SFU]; // the register under func exec
    if (!fexec_pipeline_reg->empty() and fexec_pipeline_reg->warp_id() == warp_id) {
        fexec_pipeline_reg->set_not_active(thread_id % m_config->warp_size);
        rolled_back = true;
    }
    fexec_pipeline_reg = m_pipeline_reg[ID_OC_MEM]; // the register under func exec
    if (!fexec_pipeline_reg->empty() and fexec_pipeline_reg->is_tcommit and fexec_pipeline_reg->warp_id() == warp_id) {
        fexec_pipeline_reg->set_not_active(thread_id % m_config->warp_size);
        rolled_back = true;
    }
    #endif
    bool logical_tm = m_config->tlw_use_logical_temporal_cd;
    if (m_pipeline_reg[ID_OC_SP].mask_out_matched(warp_id, thread_id % m_config->warp_size, null_match, logical_tm)) {
        if (logical_tm) m_threadState[thread_id].m_in_pipeline -= 1;
	rolled_back = true;
    }

    if (m_pipeline_reg[ID_OC_SFU].mask_out_matched(warp_id, thread_id % m_config->warp_size, null_match, logical_tm)) {
        if (logical_tm) m_threadState[thread_id].m_in_pipeline -= 1;
	rolled_back = true;
    }
    
    if (m_pipeline_reg[ID_OC_MEM].mask_out_matched(warp_id, thread_id % m_config->warp_size, is_tcommit, logical_tm)) {
        if (logical_tm) m_threadState[thread_id].m_in_pipeline -= 1;
	rolled_back = true;
    }
    
    m_warp[warp_id].ibuffer_flush();  // WF: not sure if this is needed 

    // If don't need to clear number of writing, just reset log. Otherwise, delay it until number of writing is cleared.
    tm_manager_inf *t_tm_manager = get_func_thread_info(thread_id)->get_tm_manager();
    if (t_tm_manager->get_is_abort_need_clean() == false) {
	unsigned lane_id = thread_id % m_config->warp_size;
        m_warp[warp_id].get_tm_warp_info().reset_lane(lane_id);
    }

    // Profiler - treat rollback as timeout (since being removed inflight)
    if(m_config->thread_state_profiling) {
       if(rolled_back) {
          m_threadState[thread_id].m_in_pipeline = 0;
          m_threadState[thread_id].m_timeout_validation_mode = true;
          m_threadState[thread_id].m_timeout_validation_mode_set = true;
          m_threadState[thread_id].m_timeout_validation_cycle = gpu_sim_cycle + gpu_tot_sim_cycle;
       }

       // Profiler - move all bins from tx_useful to tx_aborted
       m_threadState[thread_id].m_state_cycles_tx_aborted.add_stats(m_threadState[thread_id].m_state_cycles_tx_useful);
       m_threadState[thread_id].m_state_cycles_tx_useful.reset();
    }
} 

// TODO: Move to abstract hardware
#if 0
simt_stack::simt_stack( unsigned wid, class shader_core_ctx *shdr )
{
    m_warp_id=wid;
    m_shader=shdr;
    m_warp_size=m_shader->get_config()->warp_size;
    m_stack_top = 0;
    m_pc.resize(m_warp_size * 2);
    m_calldepth.resize(m_warp_size * 2);
    m_active_mask.resize(m_warp_size * 2); 
    m_recvg_pc.resize(m_warp_size * 2); 
    m_branch_div_cycle.resize(m_warp_size * 2); 
    m_type.resize(m_warp_size * 2); 
    reset();
}

void simt_stack::reset()
{
    m_stack_top = 0;
    m_pc.assign(m_warp_size * 2, -1);
    m_calldepth.assign(m_warp_size * 2, 0);
    m_active_mask.assign(m_warp_size * 2, 0);
    m_recvg_pc.assign(m_warp_size * 2, -1);
    m_branch_div_cycle.assign(m_warp_size * 2, 0);
    m_type.assign(m_warp_size * 2, INVALID); 
    m_in_transaction = false; 
}

void simt_stack::launch( address_type start_pc, const simt_mask_t &active_mask )
{
    reset();
    m_pc[0] = start_pc;
    m_calldepth[0] = 1;
    m_active_mask[0] = active_mask;
    m_type[0] = NORMAL; 
}

const simt_mask_t &simt_stack::get_active_mask() const
{
    return m_active_mask[m_stack_top];
}

void simt_stack::clone_entry(unsigned dst, unsigned src) 
{
    m_pc[dst] = m_pc[src]; 
    m_active_mask[dst] = m_active_mask[src]; 
    m_recvg_pc[dst] = m_recvg_pc[src]; 
    m_calldepth[dst] = m_calldepth[src]; 
    m_branch_div_cycle[dst] = m_branch_div_cycle[src]; 
    m_type[dst] = m_type[src]; 
}

tm_parallel_pdom_warp_ctx_t::tm_parallel_pdom_warp_ctx_t( unsigned wid, class shader_core_ctx *shdr )
    : simt_stack(wid, shdr) 
{ }

void tm_parallel_pdom_warp_ctx_t::txbegin(address_type tm_restart_pc) 
{
    if (m_in_transaction) return; 
    assert(m_stack_top < m_warp_size * 2 - 2); 

    // insert retry entry
    unsigned retry_idx = m_stack_top + 1;
    clone_entry(retry_idx, m_stack_top); 
    m_pc[retry_idx] = tm_restart_pc; 
    m_recvg_pc[retry_idx] = -1; //HACK: need to set this to the insn after txcommit() 
    m_active_mask[retry_idx].reset(); 
    m_type[retry_idx] = RETRY; 

    // insert transaction entry 
    unsigned texec_idx = m_stack_top + 2;
    clone_entry(texec_idx, m_stack_top); 
    m_recvg_pc[texec_idx] = -1; 
    m_pc[texec_idx] = tm_restart_pc; 
    m_recvg_pc[texec_idx] = -1; //HACK: need to set this to the insn after txcommit() 
    m_type[texec_idx] = TRANS; 

    //TODO: set TOS entry's PC to the insn after txcommit() 

    m_in_transaction = true; 

    m_stack_top += 2; 
}

void tm_parallel_pdom_warp_ctx_t::txrestart() 
{
    assert(m_in_transaction); 

    unsigned retry_idx = m_stack_top; 
    assert(m_type[retry_idx] == RETRY); 
    assert(m_active_mask[retry_idx].any()); 

    // clone the retry entry to create a new top level transaction entry 
    unsigned texec_idx = retry_idx + 1; 
    clone_entry(texec_idx, retry_idx); 
    m_type[texec_idx] = TRANS; 

    // reset active mask in retry entry 
    m_active_mask[retry_idx] = 0; 

    m_stack_top += 1; 
}

void tm_parallel_pdom_warp_ctx_t::txabort(unsigned thread_id) 
{
    assert(m_in_transaction); 
    unsigned wtid = thread_id % m_warp_size; 

    // mask out this thread in the active mask of all entries in the transaction
    // pop TOS entry if the active mask is empty 
    int idx; 
    for (idx = m_stack_top; idx > 0 and m_type[idx] != RETRY; idx--) {
        m_active_mask[idx].reset(wtid); 
        if (m_active_mask[idx].none()) {
           m_type[idx] = INVALID; // this could be an inactive entry waiting for execution 
           if (idx == (int)m_stack_top) {
               m_stack_top -= 1;
           }
        }
    }

    // set the active mask for this thread at the retry entry 
    assert(m_type[idx] == RETRY);
    m_active_mask[idx].set(wtid); 

    // if the whole warp is aborted, trigger a retry 
    if (m_type[m_stack_top] == RETRY) {
        txrestart(); 
    }
}

void tm_parallel_pdom_warp_ctx_t::txcommit(unsigned thread_id, address_type tm_commit_pc)
{
    assert(m_in_transaction); 
    assert(m_type[m_stack_top - 1] == RETRY); 
    assert(m_type[m_stack_top] == TRANS); 

    unsigned wtid = thread_id % m_warp_size; 

    // clear the active mask for this thread at the top level transaction entry 
    m_active_mask[m_stack_top].reset(wtid); 

    if (m_active_mask[m_stack_top].none()) {
        // all threads in this warp commited or aborted 
        // pop this transaction entry 
        m_stack_top -= 1; 

        // check for need to restart 
        unsigned retry_idx = m_stack_top; 
        if (m_active_mask[retry_idx].any()) {
            txrestart(); 
        } else {
            // no restart needed, pop the retry entry and set pc of TOS to the 
            // next insn after commit, transaction is done for this warp 
            m_stack_top -= 1; 
            m_pc[m_stack_top] = tm_commit_pc; 
            // if the next pc after commit happens to be the reconvergence point as well
            // this is no longer handled by normal stack handler because functional commit is now further down the pipeline
            while ( m_recvg_pc[m_stack_top] == m_pc[m_stack_top] ) {
               m_stack_top -= 1; 
               assert(m_stack_top >= 0); 
            }
            m_in_transaction = false; 
        }
    }
}

tm_serial_pdom_warp_ctx_t::tm_serial_pdom_warp_ctx_t( unsigned wid, class shader_core_ctx *shdr )
    : simt_stack(wid, shdr) 
{ }

void tm_serial_pdom_warp_ctx_t::txbegin(address_type tm_restart_pc) 
{
    if (m_in_transaction) return; 
    assert(m_stack_top < m_warp_size * 2 - 2); 

    // insert retry entry 
    // - this holds the threads that are deferred due to serialization, 
    // as well as place holder for transaction retry 
    // (though usually the same thread will set to retry after abortion)
    unsigned retry_idx = m_stack_top + 1;
    clone_entry(retry_idx, m_stack_top); 
    m_pc[retry_idx] = tm_restart_pc; 
    m_recvg_pc[retry_idx] = -1; //HACK: need to set this to the insn after txcommit() 
    m_type[retry_idx] = RETRY; 

    // insert transaction entry 
    unsigned texec_idx = m_stack_top + 2;
    clone_entry(texec_idx, m_stack_top); 
    m_recvg_pc[texec_idx] = -1; 
    m_pc[texec_idx] = tm_restart_pc; 
    m_recvg_pc[texec_idx] = -1; //HACK: need to set this to the insn after txcommit() 
    m_type[texec_idx] = TRANS; 

    //TODO: set TOS entry's PC to the insn after txcommit() 

    tx_start_thread(retry_idx, texec_idx); 

    m_in_transaction = true; 

    m_stack_top += 2; 
}

// move one thread from retry to transaction entry 
void tm_serial_pdom_warp_ctx_t::tx_start_thread(unsigned retry_idx, unsigned texec_idx) 
{
    // find a thread in the retry entry and bring it to transaction entry 
    m_active_mask[texec_idx].reset(); // clear mask in transaction entry 
    for (unsigned t = 0; t < m_warp_size; t++) {
        if (m_active_mask[retry_idx].test(t) == true) {
            m_active_mask[texec_idx].set(t); 
            m_active_mask[retry_idx].reset(t);
            
            unsigned hw_thread_id = t + m_warp_id*m_warp_size; 
            m_shader->set_transactional_thread( m_shader->get_func_thread_info(hw_thread_id) );

            break; 
        }
    }
    assert(m_active_mask[texec_idx].any()); 
}

void tm_serial_pdom_warp_ctx_t::txrestart() 
{
    assert(m_in_transaction); 

    unsigned retry_idx = m_stack_top; 
    assert(m_type[retry_idx] == RETRY); 
    assert(m_active_mask[retry_idx].any()); 

    // clone the retry entry to create a new top level transaction entry 
    unsigned texec_idx = retry_idx + 1; 
    clone_entry(texec_idx, retry_idx); 
    m_type[texec_idx] = TRANS; 

    tx_start_thread(retry_idx, texec_idx); 
    // no need to reset active mask in retry entry 

    m_stack_top += 1; 
}

void tm_serial_pdom_warp_ctx_t::txabort(unsigned thread_id) 
{
    assert(m_in_transaction); 
    unsigned wtid = thread_id % m_warp_size; 

    // mask out this thread in the active mask of all entries in the transaction
    // pop TOS entry if the active mask is empty 
    int idx; 
    for (idx = m_stack_top; idx > 0 and m_type[idx] != RETRY; idx--) {
        m_active_mask[idx].reset(wtid); 
        if (m_active_mask[idx] == 0 and idx == (int)m_stack_top) {
            m_stack_top -= 1;
        }
    }

    // set the active mask for this thread at the retry entry 
    assert(m_type[idx] == RETRY);
    m_active_mask[idx].set(wtid); 

    // if the whole warp is aborted, trigger a retry 
    if (m_type[m_stack_top] == RETRY) {
        txrestart(); 
    }
}

void tm_serial_pdom_warp_ctx_t::txcommit(unsigned thread_id, address_type tm_commit_pc)
{
    assert(m_in_transaction); 
    assert(m_type[m_stack_top - 1] == RETRY); 
    assert(m_type[m_stack_top] == TRANS); 

    unsigned wtid = thread_id % m_warp_size; 

    // clear the active mask for this thread at the top level transaction entry 
    m_active_mask[m_stack_top].reset(wtid); 

    m_shader->set_transactional_thread(NULL);

    if (m_active_mask[m_stack_top] == 0) {
        // all threads in this warp commited or aborted 
        // pop this transaction entry 
        m_stack_top -= 1; 

        // check for need to restart 
        unsigned retry_idx = m_stack_top; 
        if (m_active_mask[retry_idx] != 0) {
            txrestart(); 
        } else {
            // no restart needed, pop the retry entry and set pc of TOS to the 
            // next insn after commit, transaction is done for this warp 
            m_stack_top -= 1; 
            m_pc[m_stack_top] = tm_commit_pc; 
            m_in_transaction = false; 
        }
    }
}
#endif

void opndcoll_rfu_t::add_cu_set(unsigned set_id, unsigned num_cu, unsigned num_dispatch){
    m_cus[set_id].reserve(num_cu); //this is necessary to stop pointers in m_cu from being invalid do to a resize;
    for (unsigned i = 0; i < num_cu; i++) {
        m_cus[set_id].push_back(collector_unit_t());
        m_cu.push_back(&m_cus[set_id].back());
    }
    // for now each collector set gets dedicated dispatch units.
    for (unsigned i = 0; i < num_dispatch; i++) {
        m_dispatch_units.push_back(dispatch_unit_t(&m_cus[set_id]));
    }
}


void opndcoll_rfu_t::add_port(port_vector_t & input, port_vector_t & output, uint_vector_t cu_sets)
{
    //m_num_ports++;
    //m_num_collectors += num_collector_units;
    //m_input.resize(m_num_ports);
    //m_output.resize(m_num_ports);
    //m_num_collector_units.resize(m_num_ports);
    //m_input[m_num_ports-1]=input_port;
    //m_output[m_num_ports-1]=output_port;
    //m_num_collector_units[m_num_ports-1]=num_collector_units;
    m_in_ports.push_back(input_port_t(input,output,cu_sets));
}

void opndcoll_rfu_t::init( unsigned num_banks, shader_core_ctx *shader )
{
   m_shader=shader;
   m_arbiter.init(m_cu.size(),num_banks);
   //for( unsigned n=0; n<m_num_ports;n++ ) 
   //    m_dispatch_units[m_output[n]].init( m_num_collector_units[n] );
   m_num_banks = num_banks;
   m_bank_warp_shift = 0; 
   m_warp_size = shader->get_config()->warp_size;
   m_bank_warp_shift = (unsigned)(int) (log(m_warp_size+0.5) / log(2.0));
   assert( (m_bank_warp_shift == 5) || (m_warp_size != 32) );

   for( unsigned j=0; j<m_cu.size(); j++) {
       m_cu[j]->init(j,num_banks,m_bank_warp_shift,shader->get_config(),this);
   }

   m_history_file.init(num_banks); 

   m_initialized=true;
}

int register_bank(int regnum, int wid, unsigned num_banks, unsigned bank_warp_shift)
{
   int bank = regnum;
   if (bank_warp_shift)
      bank += wid;
   return bank % num_banks;
}

bool opndcoll_rfu_t::writeback( const warp_inst_t &inst )
{
   assert( !inst.empty() );
   std::list<unsigned> regs = m_shader->get_regs_written(inst);
   std::list<unsigned>::iterator r;
   unsigned n=0;
   for( r=regs.begin(); r!=regs.end();r++,n++ ) {
      unsigned reg = *r;
      unsigned bank = register_bank(reg,inst.warp_id(),m_num_banks,m_bank_warp_shift);
      if( m_arbiter.bank_idle(bank) ) {
          m_arbiter.allocate_bank_for_write(bank,op_t(&inst,reg,m_num_banks,m_bank_warp_shift));
      } else {
          return false;
      }
   }
   for(unsigned i=0;i<(unsigned)regs.size();i++){
	      if(m_shader->get_config()->gpgpu_clock_gated_reg_file){
	    	  unsigned active_count=0;
	    	  for(unsigned i=0;i<m_shader->get_config()->warp_size;i=i+m_shader->get_config()->n_regfile_gating_group){
	    		  for(unsigned j=0;j<m_shader->get_config()->n_regfile_gating_group;j++){
	    			  if(inst.get_active_mask().test(i+j)){
	    				  active_count+=m_shader->get_config()->n_regfile_gating_group;
	    				  break;
	    			  }
	    		  }
	    	  }
	    	  m_shader->incregfile_writes(active_count);
	      }else{
	    	  m_shader->incregfile_writes(m_shader->get_config()->warp_size);//inst.active_count());
	      }
   }
   return true;
}

void history_file_t::init( unsigned num_banks ) 
{
   m_register_set.resize(num_banks); 
}

void history_file_t::print(FILE *fout) 
{
   fprintf(fout, "w%02d: \n", m_current_wid);
   for (unsigned b = 0; b < m_register_set.size(); b++) {
      fprintf(fout, "b%u[", b); 
      for (std::set<unsigned>::iterator ireg = m_register_set[b].begin(); 
           ireg != m_register_set[b].end(); ++ireg) 
      {
         fprintf(fout, "%d ", *ireg);
      }
      fprintf(fout, "]\n"); 
   }
}

void history_file_t::commit_clear() 
{
   assert(m_in_rollback == false); 
   for (size_t b = 0; b < m_register_set.size(); b++) {
      m_register_set[b].clear(); 
   }
   m_current_wid = c_null_wid; 
}

// called at collect_operand if the operand is a TM backup
void history_file_t::backup_register_value(unsigned wid, unsigned reg, unsigned bank) 
{
   // ensure that we are not in rollback
   //assert(m_in_rollback == false);

   if(m_in_rollback == true) // Transactional instructions may be in flight after the Transaction has aborted.
	  return;
   // ensure that we are backing up a single warp only 
   if (m_current_wid == c_null_wid) m_current_wid = wid; 
   assert(m_current_wid == wid); 

   m_register_set[bank].insert(reg); 
}

std::list<unsigned> history_file_t::get_regs_restore() 
{
   bool has_more_restore = false; 
   std::list<unsigned> restore_regs; 
   for (size_t b = 0; b < m_register_set.size(); b++) {
      std::set<unsigned> &hf_regbank = m_register_set[b]; 
      if (hf_regbank.empty()) continue; 
      unsigned reg = *(hf_regbank.begin()); 
      restore_regs.push_back(reg); 
      hf_regbank.erase(hf_regbank.begin());

      // as long as one of the bank has pending register to restore next cycle 
      if (hf_regbank.empty() == false) 
         has_more_restore = true; 
   }

   if (has_more_restore == false) {
      m_in_rollback = false; // done rolling back 
      m_current_wid = c_null_wid; 
   }

   return restore_regs;
}

bool opndcoll_rfu_t::history_file_enabled() const 
{
    return (m_shader->get_tm_uarch_model() >= 10); 
}

bool opndcoll_rfu_t::in_rollback() const 
{
   if ( !history_file_enabled() ) return false; 
   return m_history_file.in_rollback(); 
}

void opndcoll_rfu_t::history_file_start_rollback()
{
   if ( !history_file_enabled() ) return; 
   m_history_file.start_rollback(); 
}

void opndcoll_rfu_t::history_file_commit_clear()
{
   if ( !history_file_enabled() ) return; 
   m_history_file.commit_clear(); 
}

bool opndcoll_rfu_t::history_file_rollback( )
{
   if ( !history_file_enabled() ) return false; 
   if ( !m_history_file.in_rollback() ) return false;

   std::list<unsigned> regs = m_history_file.get_regs_restore();
   if (regs.empty()) return true; 

   unsigned warp_id = m_history_file.get_current_wid(); 
   std::list<unsigned>::iterator r;
   unsigned n=0;
   for( r=regs.begin(); r!=regs.end();r++,n++ ) {
      unsigned reg = *r;
      unsigned bank = register_bank(reg,warp_id,m_num_banks,m_bank_warp_shift);
      if( m_arbiter.bank_idle(bank) ) {
          m_arbiter.allocate_bank_for_write(bank,op_t(warp_id,reg,m_num_banks,m_bank_warp_shift));
      } else {
          return false;
      }
   }
   return true;
}

void opndcoll_rfu_t::dispatch_ready_cu()
{
   for( unsigned p=0; p < m_dispatch_units.size(); ++p ) {
      dispatch_unit_t &du = m_dispatch_units[p];
      collector_unit_t *cu = du.find_ready();
      if( cu ) {
    	 for(unsigned i=0;i<(cu->get_num_operands()-cu->get_num_regs());i++){
   	      if(m_shader->get_config()->gpgpu_clock_gated_reg_file){
   	    	  unsigned active_count=0;
   	    	  for(unsigned i=0;i<m_shader->get_config()->warp_size;i=i+m_shader->get_config()->n_regfile_gating_group){
   	    		  for(unsigned j=0;j<m_shader->get_config()->n_regfile_gating_group;j++){
   	    			  if(cu->get_active_mask().test(i+j)){
   	    				  active_count+=m_shader->get_config()->n_regfile_gating_group;
   	    				  break;
   	    			  }
   	    		  }
   	    	  }
   	    	  m_shader->incnon_rf_operands(active_count);
   	      }else{
    		 m_shader->incnon_rf_operands(m_shader->get_config()->warp_size);//cu->get_active_count());
   	      }
    	}
         cu->dispatch();
      }
   }
}

void opndcoll_rfu_t::allocate_cu( unsigned port_num )
{
   input_port_t& inp = m_in_ports[port_num];
   for (unsigned i = 0; i < inp.m_in.size(); i++) {
       if( (*inp.m_in[i]).has_ready() ) {
          //find a free cu 
          for (unsigned j = 0; j < inp.m_cu_sets.size(); j++) {
              std::vector<collector_unit_t> & cu_set = m_cus[inp.m_cu_sets[j]];
	      bool allocated = false;
              for (unsigned k = 0; k < cu_set.size(); k++) {
                  if(cu_set[k].is_free()) {
                     collector_unit_t *cu = &cu_set[k];
                     allocated = cu->allocate(inp.m_in[i],inp.m_out[i]);
                     m_arbiter.add_read_requests(cu);
                     break;
                  }
              }
              if (allocated) break; //cu has been allocated, no need to search more.
          }
          break; // can only service a single input, if it failed it will fail for others.
       }
   }
}

void opndcoll_rfu_t::allocate_reads()
{
   // process read requests that do not have conflicts
   std::list<op_t> allocated = m_arbiter.allocate_reads();
   std::map<unsigned,op_t> read_ops;
   for( std::list<op_t>::iterator r=allocated.begin(); r!=allocated.end(); r++ ) {
      const op_t &rr = *r;
      unsigned reg = rr.get_reg();
      unsigned wid = rr.get_wid();
      unsigned bank = register_bank(reg,wid,m_num_banks,m_bank_warp_shift);
      m_arbiter.allocate_for_read(bank,rr);
      read_ops[bank] = rr;

      // send register value to history file for transaction dst
      if (rr.is_transaction_dst()) {
         m_history_file.backup_register_value(wid, reg, bank); 
      }
   }
   std::map<unsigned,op_t>::iterator r;
   for(r=read_ops.begin();r!=read_ops.end();++r ) {
      op_t &op = r->second;
      unsigned cu = op.get_oc_id();
      unsigned operand = op.get_operand();
      m_cu[cu]->collect_operand(operand);
      if(m_shader->get_config()->gpgpu_clock_gated_reg_file){
    	  unsigned active_count=0;
    	  for(unsigned i=0;i<m_shader->get_config()->warp_size;i=i+m_shader->get_config()->n_regfile_gating_group){
    		  for(unsigned j=0;j<m_shader->get_config()->n_regfile_gating_group;j++){
    			  if(op.get_active_mask().test(i+j)){
    				  active_count+=m_shader->get_config()->n_regfile_gating_group;
    				  break;
    			  }
    		  }
    	  }
    	  m_shader->incregfile_reads(active_count);
      }else{
    	  m_shader->incregfile_reads(m_shader->get_config()->warp_size);//op.get_active_count());
      }
  }
} 

bool opndcoll_rfu_t::collector_unit_t::ready() const 
{ 
   return (!m_free) && m_not_ready.none() && (*m_output_register).has_free(); 
}

void opndcoll_rfu_t::collector_unit_t::dump(FILE *fp, const shader_core_ctx *shader ) const
{
   if( m_free ) {
      fprintf(fp,"    <free>\n");
   } else {
      m_warp->print(fp);
      for( unsigned i=0; i < MAX_REG_OPERANDS*2; i++ ) {
         if( m_not_ready.test(i) ) {
            std::string r = m_src_op[i].get_reg_string();
            fprintf(fp,"    '%s' not ready\n", r.c_str() );
         }
      }
   }
}

void opndcoll_rfu_t::collector_unit_t::init( unsigned n, 
                                             unsigned num_banks, 
                                             unsigned log2_warp_size,
                                             const core_config *config,
                                             opndcoll_rfu_t *rfu ) 
{ 
   m_rfu=rfu;
   m_cuid=n; 
   m_num_banks=num_banks;
   assert(m_warp==NULL); 
   m_warp = new warp_inst_t(config);
   m_bank_warp_shift=log2_warp_size;
}

bool opndcoll_rfu_t::collector_unit_t::allocate( register_set* pipeline_reg_set, register_set* output_reg_set ) 
{
   assert(m_free);
   assert(m_not_ready.none());
   m_free = false;
   m_output_register = output_reg_set;
   warp_inst_t **pipeline_reg = pipeline_reg_set->get_ready();
   if( (pipeline_reg) and !((*pipeline_reg)->empty()) ) {
      m_warp_id = (*pipeline_reg)->warp_id();
      for( unsigned op=0; op < MAX_REG_OPERANDS; op++ ) {
         int reg_num = (*pipeline_reg)->arch_reg.src[op]; // this math needs to match that used in function_info::ptx_decode_inst
         if( reg_num >= 0 ) { // valid register
            m_src_op[op] = op_t( this, op, reg_num, m_num_banks, m_bank_warp_shift );
            m_not_ready.set(op);
         } else 
            m_src_op[op] = op_t();
      }
      //move_warp(m_warp,*pipeline_reg);
      pipeline_reg_set->move_out_to(m_warp);
      return true;
   }
   return false;
}

void opndcoll_rfu_t::collector_unit_t::dispatch()
{
   assert( m_not_ready.none() );
   //move_warp(*m_output_register,m_warp);
   m_output_register->move_in(m_warp);
   m_free=true;
   m_output_register = NULL;
   for( unsigned i=0; i<MAX_REG_OPERANDS*2;i++)
      m_src_op[i].reset();
}

simt_core_cluster::simt_core_cluster( class gpgpu_sim *gpu, 
                                      unsigned cluster_id, 
                                      const struct shader_core_config *config, 
                                      const struct memory_config *mem_config,
                                      shader_core_stats *stats, 
                                      class memory_stats_t *mstats )
{
    m_config = config;
    m_cta_issue_next_core=m_config->n_simt_cores_per_cluster-1; // this causes first launch to use hw cta 0
    m_cluster_id=cluster_id;
    m_gpu = gpu;
    m_stats = stats;
    m_memory_stats = mstats;
    m_core = new shader_core_ctx*[ config->n_simt_cores_per_cluster ];
    for( unsigned i=0; i < config->n_simt_cores_per_cluster; i++ ) {
        unsigned sid = m_config->cid_to_sid(i,m_cluster_id);
        m_core[i] = new shader_core_ctx(gpu,this,sid,m_cluster_id,config,mem_config,stats);
        m_core_sim_order.push_back(i); 
    }
}

void simt_core_cluster::core_cycle()
{
    for( std::list<unsigned>::iterator it = m_core_sim_order.begin(); it != m_core_sim_order.end(); ++it ) {
        m_core[*it]->cycle();
    }

    if (m_config->simt_core_sim_order == 1) {
        m_core_sim_order.splice(m_core_sim_order.end(), m_core_sim_order, m_core_sim_order.begin()); 
    }
}

void simt_core_cluster::core_profile_thread_states()
{
   for( unsigned i=0; i < m_config->n_simt_cores_per_cluster; i++ ) {
       m_core[i]->profile_thread_states();
   }
}

void simt_core_cluster::reinit()
{
    for( unsigned i=0; i < m_config->n_simt_cores_per_cluster; i++ ) 
        m_core[i]->reinit(0,m_config->n_thread_per_shader,true);
}

unsigned simt_core_cluster::max_cta( const kernel_info_t &kernel )
{
    return m_config->n_simt_cores_per_cluster * m_config->max_cta(kernel);
}

unsigned simt_core_cluster::get_not_completed() const
{
    unsigned not_completed=0;
    for( unsigned i=0; i < m_config->n_simt_cores_per_cluster; i++ ) 
        not_completed += m_core[i]->get_not_completed();
    return not_completed;
}

bool simt_core_cluster::has_io_pending() const 
{
    // cores have message to be processed or sent internally
    bool core_message_pending = false; 
    for( unsigned i=0; i < m_config->n_simt_cores_per_cluster; i++ ) 
        core_message_pending = core_message_pending or m_core[i]->has_message_pending();

    // there are messages from interconnect not directed to the cores 
    bool icnt_sink_busy = (not m_response_fifo.empty()); 

    unsigned not_completed = get_not_completed(); 

    if ((icnt_sink_busy or core_message_pending) and not_completed) {
       printf("[SIMT_CORE_CLUSTER %u] Response available/IO pending at empty cluster\n", m_cluster_id); 
    }
    return (icnt_sink_busy or core_message_pending);
}

void simt_core_cluster::print_not_completed( FILE *fp ) const
{
    for( unsigned i=0; i < m_config->n_simt_cores_per_cluster; i++ ) {
        unsigned not_completed=m_core[i]->get_not_completed();
        unsigned sid=m_config->cid_to_sid(i,m_cluster_id);
        fprintf(fp,"%u(%u) ", sid, not_completed );
    }
}

unsigned simt_core_cluster::get_n_active_cta() const
{
    unsigned n=0;
    for( unsigned i=0; i < m_config->n_simt_cores_per_cluster; i++ ) 
        n += m_core[i]->get_n_active_cta();
    return n;
}

unsigned simt_core_cluster::get_n_active_sms() const
{
    unsigned n=0;
    for( unsigned i=0; i < m_config->n_simt_cores_per_cluster; i++ )
        n += m_core[i]->isactive();
    return n;
}

unsigned simt_core_cluster::issue_block2core()
{
    unsigned num_blocks_issued=0;
    for( unsigned i=0; i < m_config->n_simt_cores_per_cluster; i++ ) {
        unsigned core = (i+m_cta_issue_next_core+1)%m_config->n_simt_cores_per_cluster;
        if( m_core[core]->get_not_completed() == 0 ) {
            if( m_core[core]->get_kernel() == NULL ) {
                kernel_info_t *k = m_gpu->select_kernel();
                if( k ) 
                    m_core[core]->set_kernel(k);
            }
        }
        kernel_info_t *kernel = m_core[core]->get_kernel();
        if( kernel && !kernel->no_more_ctas_to_run() && (m_core[core]->get_n_active_cta() < m_config->max_cta(*kernel)) ) {
            m_core[core]->issue_block2core(*kernel);
            num_blocks_issued++;
            m_cta_issue_next_core=core; 
            break;
        }
    }
    return num_blocks_issued;
}

void simt_core_cluster::cache_flush()
{
    for( unsigned i=0; i < m_config->n_simt_cores_per_cluster; i++ ) 
        m_core[i]->cache_flush();
}

bool simt_core_cluster::icnt_injection_buffer_full(unsigned size, bool write)
{
    unsigned request_size = size;
    if (!write) 
        request_size = READ_PACKET_SIZE;
    return ! ::icnt_has_buffer(m_cluster_id, request_size);
}

void simt_core_cluster::icnt_inject_request_packet(class mem_fetch *mf)
{
    // stats
    if (mf->get_is_write()) m_stats->made_write_mfs++;
    else m_stats->made_read_mfs++;
    switch (mf->get_access_type()) {
    case CONST_ACC_R: m_stats->gpgpu_n_mem_const++; break;
    case TEXTURE_ACC_R: m_stats->gpgpu_n_mem_texture++; break;
    case GLOBAL_ACC_R: m_stats->gpgpu_n_mem_read_global++; break;
    case GLOBAL_ACC_W: m_stats->gpgpu_n_mem_write_global++; break;
    case LOCAL_ACC_R: m_stats->gpgpu_n_mem_read_local++; break;
    case LOCAL_ACC_W: m_stats->gpgpu_n_mem_write_local++; break;
    case INST_ACC_R: m_stats->gpgpu_n_mem_read_inst++; break;
    case L1_WRBK_ACC: m_stats->gpgpu_n_mem_write_global++; break;
    case L2_WRBK_ACC: m_stats->gpgpu_n_mem_l2_writeback++; break;
    case L2_WR_ALLOC_R: m_stats->gpgpu_n_mem_l2_write_allocate++; break;
    case TR_MSG: 
        // TODO: add stats for frequency of message types here
        break;
    case TX_MSG: m_stats->gpgpu_n_tx_msg++; break;
    default: assert(0); break; 
    }

   // The packet size varies depending on the type of request: 
   // - For write request and atomic request, the packet contains the data 
   // - For read request (i.e. not write nor atomic), the packet only has control metadata
   unsigned int packet_size = mf->size();
   if (!mf->get_is_write() && !mf->isatomic() && !(mf->get_access_type() == TX_MSG)) {
      packet_size = mf->get_ctrl_size(); 
   }
   if (g_tm_options.m_use_logical_timestamp_based_tm) {
      if (mf->get_is_write() && mf->is_logical_tm_req()) {
         packet_size = mf->get_ctrl_size(); 
      }
   }
   m_stats->m_outgoing_traffic_stats->record_traffic(mf, packet_size); 
   unsigned destination = mf->get_sub_partition_id();
   mf->set_status(IN_ICNT_TO_MEM,gpu_sim_cycle+gpu_tot_sim_cycle);
   if (!mf->get_is_write() && !mf->isatomic() && !(mf->get_access_type() == TX_MSG)) 
      ::icnt_push(m_cluster_id, m_config->mem2device(destination), (void*)mf, mf->get_ctrl_size() );
   else 
      ::icnt_push(m_cluster_id, m_config->mem2device(destination), (void*)mf, mf->size());
}

void simt_core_cluster::icnt_cycle()
{
    if( !m_response_fifo.empty() ) {
        mem_fetch *mf = m_response_fifo.front();
        unsigned cid = m_config->sid_to_cid(mf->get_sid());
        if( mf->get_access_type() == INST_ACC_R ) {
            // instruction fetch response
            if( !m_core[cid]->fetch_unit_response_buffer_full() ) {
                m_response_fifo.pop_front();
                m_core[cid]->accept_fetch_response(mf);
            }
        } else {
            // data response
            if( !m_core[cid]->ldst_unit_response_buffer_full() ) {
                m_response_fifo.pop_front();
                m_memory_stats->memlatstat_read_done(mf);
                m_core[cid]->accept_ldst_unit_response(mf);
            }
        }
    }
    if( m_response_fifo.size() < m_config->n_simt_ejection_buffer_size ) {
        mem_fetch *mf = (mem_fetch*) ::icnt_pop(m_cluster_id);
        if (!mf) 
            return;
        assert(mf->get_tpc() == m_cluster_id);
        assert(mf->get_type() == READ_REPLY || mf->get_type() == WRITE_ACK  || 
               mf->get_access_type() == TR_MSG || mf->get_access_type() == TX_MSG);

        // The packet size varies depending on the type of request: 
        // - For read request and atomic request, the packet contains the data 
        // - For write-ack, the packet only has control metadata
        unsigned int packet_size = (mf->get_is_write())? mf->get_ctrl_size() : mf->size(); 
	//if (mf->get_type() == NEWLY_INSERTED_ADDR || mf->get_type() == REMOVED_ADDR) {
	//    packet_size += mf->get_early_abort_addr_set().size() * 4;
	//}
        m_stats->m_incoming_traffic_stats->record_traffic(mf, packet_size); 
        mf->set_status(IN_CLUSTER_TO_SHADER_QUEUE,gpu_sim_cycle+gpu_tot_sim_cycle);
        //m_memory_stats->memlatstat_read_done(mf,m_shader_config->max_warps_per_shader);
        m_response_fifo.push_back(mf);
        m_stats->n_mem_to_simt[m_cluster_id] += mf->get_num_flits(false); //FIXME: May double count traffic 
    }
}

void simt_core_cluster::get_pdom_stack_top_info( unsigned sid, unsigned tid, unsigned *pc, unsigned *rpc ) const
{
    unsigned cid = m_config->sid_to_cid(sid);
    m_core[cid]->get_pdom_stack_top_info(tid,pc,rpc);
}

void simt_core_cluster::display_pipeline( unsigned sid, FILE *fout, int print_mem, int mask )
{
    m_core[m_config->sid_to_cid(sid)]->display_pipeline(fout,print_mem,mask);

    fprintf(fout,"\n");
    fprintf(fout,"Cluster %u pipeline state\n", m_cluster_id );
    fprintf(fout,"Response FIFO (occupancy = %zu):\n", m_response_fifo.size() );
    for( std::list<mem_fetch*>::const_iterator i=m_response_fifo.begin(); i != m_response_fifo.end(); i++ ) {
        const mem_fetch *mf = *i;
        mf->print(fout);
    }
}

void simt_core_cluster::print_icache_stats( FILE *fp, unsigned& il1_accesses, unsigned& il1_misses ) const {
   for ( unsigned i = 0; i < m_config->n_simt_cores_per_cluster; ++i ) {
      m_core[ i ]->print_icache_stats( fp, il1_accesses, il1_misses );
   }
}

void simt_core_cluster::print_cache_stats( FILE *fp, unsigned& dl1_accesses, unsigned& dl1_misses ) const {
   for ( unsigned i = 0; i < m_config->n_simt_cores_per_cluster; ++i ) {
      m_core[ i ]->print_cache_stats( fp, dl1_accesses, dl1_misses );
   }
}

void simt_core_cluster::get_icnt_stats(long &n_simt_to_mem, long &n_mem_to_simt) const {
	long simt_to_mem=0;
	long mem_to_simt=0;
	for ( unsigned i = 0; i < m_config->n_simt_cores_per_cluster; ++i ) {
		m_core[i]->get_icnt_power_stats(simt_to_mem, mem_to_simt);
	}
	n_simt_to_mem = simt_to_mem;
	n_mem_to_simt = mem_to_simt;
}

void simt_core_cluster::get_cache_stats(cache_stats &cs) const{
    for ( unsigned i = 0; i < m_config->n_simt_cores_per_cluster; ++i ) {
        m_core[i]->get_cache_stats(cs);
    }
}

void simt_core_cluster::get_L1I_sub_stats(struct cache_sub_stats &css) const{
    struct cache_sub_stats temp_css;
    struct cache_sub_stats total_css;
    temp_css.clear();
    total_css.clear();
    for ( unsigned i = 0; i < m_config->n_simt_cores_per_cluster; ++i ) {
        m_core[i]->get_L1I_sub_stats(temp_css);
        total_css += temp_css;
    }
    css = total_css;
}
void simt_core_cluster::get_L1D_sub_stats(struct cache_sub_stats &css) const{
    struct cache_sub_stats temp_css;
    struct cache_sub_stats total_css;
    temp_css.clear();
    total_css.clear();
    for ( unsigned i = 0; i < m_config->n_simt_cores_per_cluster; ++i ) {
        m_core[i]->get_L1D_sub_stats(temp_css);
        total_css += temp_css;
    }
    css = total_css;
}
void simt_core_cluster::get_L1C_sub_stats(struct cache_sub_stats &css) const{
    struct cache_sub_stats temp_css;
    struct cache_sub_stats total_css;
    temp_css.clear();
    total_css.clear();
    for ( unsigned i = 0; i < m_config->n_simt_cores_per_cluster; ++i ) {
        m_core[i]->get_L1C_sub_stats(temp_css);
        total_css += temp_css;
    }
    css = total_css;
}
void simt_core_cluster::get_L1T_sub_stats(struct cache_sub_stats &css) const{
    struct cache_sub_stats temp_css;
    struct cache_sub_stats total_css;
    temp_css.clear();
    total_css.clear();
    for ( unsigned i = 0; i < m_config->n_simt_cores_per_cluster; ++i ) {
        m_core[i]->get_L1T_sub_stats(temp_css);
        total_css += temp_css;
    }
    css = total_css;
}

void shader_core_ctx::checkExecutionStatusAndUpdate(warp_inst_t &inst, unsigned t, unsigned tid)
{
    // Coherence Profiling
    bool is_tm_inst = (m_thread[tid]->is_in_transaction() != NULL);
    if ( this->m_config->coh_model && inst.space.is_global() && (!this->m_config->coh_tm_only || is_tm_inst) ) {
        unsigned hwtid = m_sid * m_config->n_thread_per_shader + tid; // global hw tid across all shader cores
        m_gpu->get_coherence_manager()->mem_access(hwtid, inst.get_addr(t), gpu_sim_cycle + gpu_tot_sim_cycle, inst.is_store(),
                                                   m_thread[tid]);
    }

    if( inst.has_callback(t) ) 
        m_warp[inst.warp_id()].inc_n_atomic();
    if( inst.has_logical_tm_callback(t) and !inst.m_tm_access_info.m_writelog_access.test(t) ) {
	m_warp[inst.warp_id()].inc_n_logical_tm_req();
	m_warp[inst.warp_id()].inc_n_logical_tm_req_per_thread(t);
    }
    if (inst.space.is_local() && (inst.is_load() || inst.is_store())) {
        new_addr_type localaddrs[MAX_ACCESSES_PER_INSN_PER_THREAD];
        unsigned num_addrs;
        num_addrs = translate_local_memaddr(inst.get_addr(t), tid, m_config->n_simt_clusters*m_config->n_simt_cores_per_cluster,
               inst.data_size, (new_addr_type*) localaddrs );
        inst.set_addr(t, (new_addr_type*) localaddrs, num_addrs);
    }
    if ( ptx_thread_done(tid) ) {
        m_warp[inst.warp_id()].set_completed(t);
        m_warp[inst.warp_id()].ibuffer_flush();
    }

    // PC-Histogram Update 
    unsigned warp_id = inst.warp_id(); 
    unsigned pc = inst.pc; 
    for (unsigned t = 0; t < m_config->warp_size; t++) {
        if (inst.active(t)) {
            int tid = warp_id * m_config->warp_size + t; 
            cflog_update_thread_pc(m_sid, tid, pc);  
        }
    }
}

void tx_log_walker_stats::print( FILE *fout ) const
{
   fprintf(fout, "TLW_n_atag_read = %u\n", n_atag_read); 
   fprintf(fout, "TLW_n_data_read = %u\n", n_data_read); 
   fprintf(fout, "TLW_n_atag_cachercf = %u\n", n_atag_cachercf); 
   fprintf(fout, "TLW_n_data_cachercf = %u\n", n_data_cachercf); 
   fprintf(fout, "TLW_n_atag_cachemiss = %u\n", n_atag_cachemiss); 
   fprintf(fout, "TLW_n_data_cachemiss = %u\n", n_data_cachemiss); 
   fprintf(fout, "TLW_n_cu_pass_msg = %u\n", n_cu_pass_msg);
   fprintf(fout, "TLW_n_warp_commit_attempt = %u\n", n_warp_commit_attempt);
   fprintf(fout, "TLW_n_warp_commit_read_only = %u\n", n_warp_commit_read_only);
   fprintf(fout, "TLW_n_pre_commit_validation_abort = %u\n", n_pre_commit_validation_abort);
   fprintf(fout, "TLW_n_pre_commit_validation_pass = %u\n", n_pre_commit_validation_pass);
   fprintf(fout, "TLW_n_intra_warp_conflicts_detected = %u\n", n_intra_warp_conflicts_detected); 
   fprintf(fout, "TLW_n_intra_warp_aborts_false_positive = %u\n", n_intra_warp_aborts_false_positive); 
   fprintf(fout, "TLW_n_intra_warp_complete_abort = %u\n", n_intra_warp_complete_abort); 

   m_intra_warp_pre_cd_active.fprint(fout); fprintf(fout, "\n"); 
   m_intra_warp_aborts.fprint(fout); fprintf(fout, "\n"); 
   m_ownership_table_size.fprint(fout); fprintf(fout, "\n"); 
   m_ownership_aliasing_depth_avg.fprint(fout); fprintf(fout, "\n"); 
   m_ownership_aliasing_depth_max.fprint(fout); fprintf(fout, "\n"); 
   m_ownership_aliasing_depth_usage.fprint(fout); fprintf(fout, "\n"); 
   m_intra_warp_cd_cycle_per_warp.fprint(fout); fprintf(fout, "\n"); 
   m_cu_allocation_retries.fprint(fout); fprintf(fout, "\n");
   m_out_message_queue_size.fprint(fout); fprintf(fout, "\n"); 
   m_out_txreply_queue_size.fprint(fout); fprintf(fout, "\n"); 
   m_coalesced_packet_size.fprint(fout); fprintf(fout, "\n"); 
   m_warp_read_log_size.fprint(fout); fprintf(fout, "\n"); 
   m_warp_write_log_size.fprint(fout); fprintf(fout, "\n"); 

   for (auto iter = m_sent_icnt_traffic.begin(); iter != m_sent_icnt_traffic.end(); ++iter) {
      fprintf(fout, "TLW_sent_icnt_traffic[%d] = %u\n", iter->first, *(iter->second)); 
   }
   unsigned TLW_intra_warp_cd_cycle_max = 0;
   for (auto iter = m_intra_warp_cd_cycle.begin(); iter != m_intra_warp_cd_cycle.end(); ++iter) {
      fprintf(fout, "TLW_intra_warp_cd_cycle[%d] = %u\n", iter->first, iter->second); 
      TLW_intra_warp_cd_cycle_max = std::max(TLW_intra_warp_cd_cycle_max, iter->second); 
   }
   fprintf(fout, "TLW_intra_warp_cd_cycle_max = %u\n", TLW_intra_warp_cd_cycle_max); 
}

bool TLW_logging = false; 

bool tlw_watched(int m_commit_id)
{
   return (m_commit_id <= 217288 and m_commit_id >= 217100);
}

tx_log_walker::tx_log_walker(shader_core_ctx *core, 
                             unsigned core_id, 
                             unsigned cluster_id, 
                             data_cache *L1D, 
                             mem_fetch_interface *&icnt, 
                             mem_fetch_allocator *mf_alloc,
                             const shader_core_config *core_config, 
                             const memory_config *memory_config, 
                             shader_core_stats &core_stats, 
                             tx_log_walker_stats &stats) 
   : m_core_config(core_config), m_memory_config(memory_config), m_core(core), m_core_stats(core_stats), 
     m_warp(m_core->get_warps()), 
     m_L1D(L1D), m_icnt(icnt), m_mf_alloc(mf_alloc), m_stats(stats), 
     m_sent_icnt_traffic(0), 
     m_core_id(core_id), m_cluster_id(cluster_id), 
     m_warp_size(m_core->get_config()->warp_size), 
     m_advance_skip_msg(false), 
     m_current_warp_id(-1), m_committing_warp(m_core->get_config()->max_warps_per_shader),
     m_coalescing_queue(m_memory_config->m_n_mem_sub_partition)
{ 
   if (TLW_logging) {
      char timeline_filename[20]; 
      snprintf(timeline_filename, sizeof(timeline_filename), "txlog-time%d.txt", m_core_id);
      m_timelinef = fopen(timeline_filename, "w"); 
   } else {
      m_timelinef = NULL; 
   }
   stats.m_sent_icnt_traffic[m_core_id] = &m_sent_icnt_traffic; 
}

tx_log_walker::~tx_log_walker()
{
   fclose(m_timelinef); 
}

bool tx_log_walker::commit_tx_t::read_log_sent() { return m_read_log_send_q.empty(); }
bool tx_log_walker::commit_tx_t::write_log_sent() { return m_write_log_send_q.empty(); }

// create the log send status according to log sizes in warp-wide transaction info 
void tx_log_walker::commit_tx_t::setup_log(const tm_warp_info &warp_info, bool read_log, bool write_log)
{
   if (g_tm_options.m_use_logical_timestamp_based_tm == false && g_tm_options.m_eager_warptm_enabled == false) { 
       if (read_log) {
          for (unsigned e = 0; e < warp_info.m_read_log_size; e++) {
             log_send_status_t log_send(e, READ_LOG_ACC); 
             m_read_log_send_q.push_back(log_send); 
          }
       }
   }

   if (write_log) {
      for (unsigned e = 0; e < warp_info.m_write_log_size; e++) {
         log_send_status_t log_send(e, WRITE_LOG_ACC); 
         m_write_log_send_q.push_back(log_send);
      }
   }
}

void tx_log_walker::commit_tx_t::delete_tm_manager()
{
   if (m_tm_manager == NULL) return; 

   if (m_tm_manager->dec_ref_count() == 0) {
      delete m_tm_manager;
   }
   m_tm_manager = NULL; 
}

void tx_log_walker::assign_tm_manager(int warp_id, int lane_id)
{
   int tid = warp_id * m_warp_size + lane_id; 
   tm_manager_inf *thread_tm_manager = m_core->get_func_thread_info(tid)->get_tm_manager(); 

   commit_tx_t &tp = m_committing_warp[warp_id].m_thread_state[lane_id]; 

   if (tp.m_tm_manager != NULL) {
      assert(thread_tm_manager == tp.m_tm_manager);
   } else {
      tp.m_tm_manager = thread_tm_manager; 
      tp.m_tm_manager->inc_ref_count(); 
   }

   if (m_core_config->tlw_use_logical_temporal_cd and tp.m_tm_manager->get_is_abort_need_clean() == false) {
       tp.m_tm_manager->commit_core_side();
   }

   if (tp.m_tm_manager->logical_tx_aborted()) {
       unsigned abort_count = tp.m_tm_manager->abort_count();
       unsigned long long retry_delay = (abort_count % ((rand() % 10) + 1)) * 500; 
       if (retry_delay > 5000) retry_delay = 5000; 
       //unsigned num_choices = abort_count * abort_count;
       //if (num_choices == 0) num_choices = 1;
       //unsigned long long retry_delay = (rand() % num_choices) * 200; 
       //if (retry_delay > 5000) retry_delay = (rand() % 25) *200; 
       m_committing_warp[warp_id].m_retry_delay = gpu_sim_cycle + gpu_tot_sim_cycle + retry_delay;
   }
}

void tx_log_walker::delete_ptx_thread_tm_manager(int warp_id, int lane_id)
{
   int tid = warp_id * m_warp_size + lane_id; 
   tm_manager_inf *thread_tm_manager = m_core->get_func_thread_info(tid)->get_tm_manager(); 

   assert (thread_tm_manager != NULL);
   if (thread_tm_manager->dec_ref_count() == 0) {
      delete thread_tm_manager; 
   } 
   m_core->get_func_thread_info(tid)->end_transaction(); 
}

void tx_log_walker::commit_tx_t::print(FILE *fout)
{
   fprintf(fout, "commit_id=%d state=%d pass=%c \n", m_commit_id, m_state, ((m_pass)? 'P':'F')); 
   fprintf(fout, "  pending_cu_reply=%02zx cu_pass=%02zx cu_fail=%02zx sent_cu_entry=%02zx cu_commit_pending=%02zx\n", 
           m_pending_cu_reply.to_ulong(), m_cu_pass.to_ulong(), m_cu_fail.to_ulong(), 
           m_sent_cu_entry.to_ulong(), m_cu_commit_pending.to_ulong()); 
   for (std::list<log_send_status_t>::iterator ilog = m_read_log_send_q.begin(); ilog != m_read_log_send_q.end(); ++ilog) {
      fprintf(fout, "  "); 
      ilog->print(fout); 
   }
   for (std::list<log_send_status_t>::iterator ilog = m_write_log_send_q.begin(); ilog != m_write_log_send_q.end(); ++ilog) {
      fprintf(fout, "  "); 
      ilog->print(fout); 
   }
}

void tx_log_walker::warp_commit_tx_t::print(FILE *fout)
{
   fprintf(fout, "warp_id=%d next_process_thread=%d retry_delay=%llu\n", m_inst.warp_id(), m_thread_processing, m_retry_delay ); 
   for (unsigned t = 0; t < m_thread_state.size(); t++) {
      if (m_thread_state[t].m_commit_id != -1) {
         fprintf(fout, "lane=%d ", t); 
         m_thread_state[t].print(fout); 
      }
   }
}

// read the <address, data> pair from log, and send it to commit unit. 
// return: true = entry sent, false = try again; cache_access = true if L1D cache was accessed 
bool tx_log_walker::send_log_entry( const warp_inst_t& inst, unsigned thread_id, int commit_id, 
                                    log_send_status_t &log_send, bool &cache_accessed, unsigned &cu_accessed, bool aborted)
{
   if (m_core_config->tlw_fast_log_read) {
      log_send.atag_read = true; 
      log_send.data_read = true; 
   }

   const unsigned addr_size = 4; 
   const unsigned word_size = 4; 
   const active_mask_t empty_mask;
   mem_access_byte_mask_t full_byte_mask; 
   full_byte_mask.set(); 
   unsigned atag_block_size = addr_size * m_warp_size; 
   unsigned data_block_size = word_size * m_warp_size; 
   unsigned warp_id = inst.warp_id(); 
   unsigned wtid = warp_id * m_core->get_config()->warp_size; 
   unsigned entry_id = log_send.entry_id; 
   addr_t log_offset = (log_send.log_type == READ_LOG_ACC)? tm_warp_info::read_log_offset : tm_warp_info::write_log_offset; 
   // warp_commit_tx_t &cmt_warp = m_committing_warp[warp_id]; 

   if (log_send.atag_read == false and log_send.atag_cachemiss == false) {
      // generate access to entry's address field
      addr_t atag_block = m_core->translate_local_memaddr(entry_id * 2 * word_size + log_offset, wtid, addr_size); 
      mem_access_t atag_access(LOCAL_ACC_R, atag_block, atag_block_size, false, empty_mask, full_byte_mask);
      mem_fetch *atag_mf = m_mf_alloc->alloc(inst, atag_access);

      std::list<cache_event> events;
      enum cache_request_status status = m_L1D->access(atag_mf->get_addr(),atag_mf,gpu_sim_cycle+gpu_tot_sim_cycle,events);
      m_stats.n_atag_read++; 

      bool write_sent = was_write_sent(events);
      bool read_sent = was_read_sent(events);
      if( write_sent ) { 
          m_core->inc_store_req( inst.warp_id() ); // eviction 
      }
      if ( status == HIT ) {
          assert( !read_sent );
          assert( !write_sent );
          m_L1D->mark_last_use(atag_mf->get_addr()); 
          delete atag_mf; 
          log_send.atag_read = true; 
      } else if ( status == RESERVATION_FAIL ) {
          assert( !read_sent );
          assert( !write_sent );
          delete atag_mf;
          m_stats.n_atag_cachercf++; 
      } else {
          assert( status == MISS || status == HIT_RESERVED );
          m_extra_mf_fields[atag_mf] = extra_mf_fields(warp_id, thread_id, log_send.log_type, ATAG_READ); 
          m_stats.n_atag_cachemiss++; 
          log_send.atag_cachemiss = true; // don't try again until this flag is cleared 
          // cmt_warp.signal_stalled(); 
      }
      if (m_timelinef) 
          fprintf(m_timelinef, "Log-read: warp%u RD %s-atag[%u] from [0x%08x], set=%u, outcome=%d @ %llu\n", 
                  warp_id, ((log_send.log_type == READ_LOG_ACC)? "RS":"WS"), entry_id, atag_block, 
                  m_core->get_config()->m_L1D_config.set_index(atag_block), status, 
                  gpu_sim_cycle + gpu_tot_sim_cycle); 
      cache_accessed = true; 
   }

   if (log_send.data_read == false and log_send.data_cachemiss == false) {
      // generate access to entry's data field
      addr_t data_block = m_core->translate_local_memaddr((entry_id * 2 + 1) * word_size + log_offset, wtid, word_size); 
      mem_access_t data_access(LOCAL_ACC_R, data_block, data_block_size, false, empty_mask, full_byte_mask);
      mem_fetch *data_mf = m_mf_alloc->alloc(inst, data_access);

      std::list<cache_event> events;
      enum cache_request_status status = m_L1D->access(data_mf->get_addr(),data_mf,gpu_sim_cycle+gpu_tot_sim_cycle,events);
      m_stats.n_data_read++; 

      bool write_sent = was_write_sent(events);
      bool read_sent = was_read_sent(events);
      if( write_sent ) { 
          m_core->inc_store_req( inst.warp_id() ); // eviction 
      }
      if ( status == HIT ) {
          assert( !read_sent );
          assert( !write_sent );
          m_L1D->mark_last_use(data_mf->get_addr()); 
          delete data_mf; 
          log_send.data_read = true; 
      } else if ( status == RESERVATION_FAIL ) { // try again later 
          assert( !read_sent );
          assert( !write_sent );
          delete data_mf;
          m_stats.n_data_cachercf++; 
      } else {
          assert( status == MISS || status == HIT_RESERVED );
          m_extra_mf_fields[data_mf] = extra_mf_fields(warp_id, thread_id, log_send.log_type, DATA_READ); 
          m_stats.n_data_cachemiss++; 
          log_send.data_cachemiss = true; // don't try again until this flag is cleared 
          // cmt_warp.signal_stalled(); 
      }
      if (m_timelinef) 
          fprintf(m_timelinef, "Log-read: warp%u RD %s-data[%u] from [0x%08x], set=%u, outcome=%d @ %llu\n", 
                  warp_id, ((log_send.log_type == READ_LOG_ACC)? "RS":"WS"), entry_id, data_block, 
                  m_core->get_config()->m_L1D_config.set_index(data_block), status, 
                  gpu_sim_cycle + gpu_tot_sim_cycle); 
      cache_accessed = true; 
   }

   // once the entry is read, send log entry to commit unit 
   if (log_send.atag_read and log_send.data_read and not log_send.sent) {
      // obtain value of address_tag 
      const tm_warp_info& warp_info = m_warp[warp_id].get_tm_warp_info(); 
      unsigned lane_id = thread_id % m_warp_size; 
      const tm_warp_info::tx_log_t &log = (log_send.log_type == READ_LOG_ACC)? warp_info.m_read_log : warp_info.m_write_log; 
      addr_t address_tag = log[entry_id].m_addr[lane_id];
      
      // create packet to corresponding commit unit 
      if (address_tag != 0) {
	 mem_fetch *wrset_mf = NULL;
	 if (aborted) {
            wrset_mf = create_tx_packet(((log_send.log_type == READ_LOG_ACC)? TX_READ_SET : TX_WRITE_SET), 
                                        inst, commit_id, -1, address_tag, addr_size, false);
	 } else {
            wrset_mf = create_tx_packet(((log_send.log_type == READ_LOG_ACC)? TX_READ_SET : TX_WRITE_SET), 
                                        inst, commit_id, -1, address_tag, addr_size + word_size, false);
	 }
         commit_tx_t &tp = m_committing_warp[warp_id].m_thread_state[lane_id];

         wrset_mf->set_tm_manager_ptr(tp.m_tm_manager); 
         cu_accessed = wrset_mf->get_sub_partition_id();
         if (log_send.log_type == WRITE_LOG_ACC)
            tp.m_cu_commit_pending.set(wrset_mf->get_sub_partition_id()); // setting the need for ack for commit 
         m_coalescing_queue[cu_accessed].push_back(wrset_mf);
      } 
	
      log_send.sent = true; 
   }

   log_send.validate(); 

   return log_send.sent; 
}

// simply transfer from coalescing queue to out message queue 
// - will be overloaded by derived class 
void tx_log_walker::send_coalesced_packet()
{
   unsigned n_packet_transfer = 0;
   for (size_t m = 0; m < m_coalescing_queue.size(); m++) {
      if (not m_coalescing_queue[m].empty()) {
         mem_fetch* wrset_mf = m_coalescing_queue[m].front(); 
         m_coalescing_queue[m].pop_front(); 
         m_out_message_queue.push_back(wrset_mf);
         n_packet_transfer++;
      }
   }
   assert(n_packet_transfer <= 1); 
}

// wake up log walker when L1D miss generated due to log walking returns 
void tx_log_walker::process_L1D_mem_fetch(mem_fetch *mf)
{
   assert(m_extra_mf_fields[mf].m_valid); 
   unsigned wid = m_extra_mf_fields[mf].m_wid; 
   unsigned lane_id = m_extra_mf_fields[mf].m_tid % m_warp_size; 
   enum log_acc_type_t log_type = m_extra_mf_fields[mf].m_log_type;
   enum log_acc_type_t acc_type = m_extra_mf_fields[mf].m_acc_type;

   assert(log_type == READ_LOG_ACC or log_type == WRITE_LOG_ACC); 

   // action pending in intra warp conflict detection means this fetch is for iwcd
   iwcd_uarch_info & uarch_info = m_committing_warp[wid].m_iwcd_uarch_info;
   bool intra_warp_cd_access = (uarch_info.event_queue_empty() == false); 
   if (intra_warp_cd_access) {
      iwcd_uarch_info::uarch_event & uevent = uarch_info.next_event_in_queue(); 
      assert(uevent.m_started == true); 
      uevent.m_done = true; 
      uarch_info.next_event_done(); 
      m_committing_warp[wid].m_stalled = false; 
   } else {
      log_send_status_t &log_send = (log_type == READ_LOG_ACC)? 
                                    m_committing_warp[wid].m_thread_state[lane_id].m_read_log_send_q.front() :
                                    m_committing_warp[wid].m_thread_state[lane_id].m_write_log_send_q.front();

      if (acc_type == DATA_READ) {
         assert(log_send.data_cachemiss == true); 
         log_send.data_cachemiss = false; 
         log_send.data_read = true;  // obtain the log data as it is written into the L1 cache
      } else if (acc_type == ATAG_READ) {
         assert(log_send.atag_cachemiss == true); 
         log_send.atag_cachemiss = false; 
         log_send.atag_read = true; // obtain the log address tag as it is written into the L1 cache
      } else {
         abort(); 
      }
      m_committing_warp[wid].m_stalled = false; 
   }
   m_extra_mf_fields.erase(mf); 
}

// called when entering write_cu_reply state to initialize reply vectors 
void tx_log_walker::commit_tx_t::enter_write_cu_reply(unsigned n_commit_unit)
{
   m_pending_cu_reply.reset();
   m_cu_pass.reset();
   m_cu_fail.reset();
   // If log entry was sent, mark as pending reply
   m_pending_cu_reply = m_sent_cu_entry;
   // assert(!m_sent_cu_entry.none()); why are there empty read sets??
   if(m_sent_cu_entry.none())
       m_state = IDLE;
   else
       m_state = WAIT_CU_REPLY;
}

// called when entering acq_cu_entries state to initialize all response vectors
void tx_log_walker::commit_tx_t::enter_acq_cu_entries(unsigned n_commit_unit, unsigned init_timeout)
{
   m_pending_cu_alloc.reset();
   m_cu_alloc_fail.reset();
   m_cu_alloc_pass.reset();
   // Wait for reply from all CUs
   for(unsigned cu=0; cu<n_commit_unit; cu++) {
      m_pending_cu_alloc.set(cu);
   }
   // Initialize timeout vector
   m_alloc_retry_increment = init_timeout;
   m_alloc_retry_timeout = gpu_tot_sim_cycle + gpu_sim_cycle;
}

// called when entering send_ack_cleanup state to initialize reply vectors 
void tx_log_walker::commit_tx_t::enter_send_ack_cleanup(unsigned n_commit_unit)
{
   m_cu_commit_pending.reset();
   // If log entry was sent, mark as pending reply
   m_cu_commit_pending = m_sent_cu_entry;
   // assert(!m_sent_cu_entry.none()); why are there empty read sets??
   if(m_sent_cu_entry.none())
       m_state = IDLE;
   else
       m_state = SEND_ACK_CLEANUP;
}

// handle CU_PASS and CU_FAIL - generate outcome of transaction when all pending replies has arrived
void tx_log_walker::process_cu_pass_fail(mem_fetch *mf) 
{
   // set the state structure of the committing thread accordingly
   int commit_id = mf->get_transaction_id();
   int tid = m_cid2tid[commit_id];
   int wid = tid / m_warp_size;
   int mpid = mf->get_sub_partition_id();
   warp_commit_tx_t &cmt_warp = m_committing_warp[wid];
   assert(cmt_warp.active());
   commit_tx_t &tp = cmt_warp.m_thread_state[tid % m_warp_size];

   assert(tp.m_state == WAIT_CU_REPLY);

   // set the pass-fail mask for the committing thread
   assert( tp.m_pending_cu_reply.test(mpid) == true ); // no redundant replies
   tp.m_pending_cu_reply.reset(mpid);
   if (mf->get_type() == CU_PASS) {
      tp.m_cu_pass.set(mpid);
   } else if (mf->get_type() == CU_FAIL) {
      tp.m_cu_fail.set(mpid);
   }

   if (tp.m_cu_reply_started) {
      tp.m_cu_reply_time_end = gpu_sim_cycle + gpu_tot_sim_cycle; 
   } else {
      tp.m_cu_reply_time_start = gpu_sim_cycle + gpu_tot_sim_cycle; 
      tp.m_cu_reply_started = true; 
   }

   if (tp.m_pending_cu_reply.none()) {
      assert( (tp.m_cu_pass & tp.m_cu_fail).none() );
      assert( (tp.m_cu_pass | tp.m_cu_fail) == tp.m_sent_cu_entry );
      tp.m_pass = tp.m_cu_fail.none(); // none of the commit unit fail == overall pass

      send_tx_pass_fail(cmt_warp, wid, tid); 

      if (m_timelinef) {
         fprintf(m_timelinef, "tx_cid = %d; cu_reply = (%u,%u)\n", tp.m_commit_id, tp.m_cu_reply_time_start, tp.m_cu_reply_time_end); 
      }
   }
}

// send TX_PASS or TX_FAIL message according to outcome of transaction for given thread
// also call tm_manager commit to update local_memory data 
void tx_log_walker::send_tx_pass_fail(warp_commit_tx_t &cmt_warp, int warp_id, int thread_id)
{
   commit_tx_t &tp = cmt_warp.m_thread_state[thread_id % m_warp_size];

   tp.m_state = SEND_ACK_CLEANUP;
   // send TX_PASS or TX_FAIL message
   for (unsigned cu = 0; cu < m_memory_config->m_n_mem_sub_partition; cu++) {
      if(tp.m_sent_cu_entry.test(cu)) {
         enum mf_type ack_type = (tp.m_pass)? TX_PASS : TX_FAIL;
         mem_fetch *tx_ack_mf = create_tx_packet(ack_type, cmt_warp.m_inst, tp.m_commit_id, cu, 0x600DACC, 0, false);
         if (m_core_config->tlw_prioritize_ack) {
            m_out_txreply_queue.push_back(tx_ack_mf); // WF: use a separate queue for higher priority
         } else {
            m_out_message_queue.push_back(tx_ack_mf); // WF: use the normal queue 
         }
      }
   }
   // call tm_manager commit here -- before reply arrives at CU
   // this ensures that the following sequence of events is correct:
   //  1. CU receive reply
   //  2. Commit updates sent for this TX
   //  3. Commit updates done for this TX  <-- this is where the commit update is supposed to be visible globally
   //  4. the revalidation of other TXs that follows this TX gets the latest value
   // if we defer the commit call to SEND_ACK_CLEANUP state handler, it may be called after #4,
   // causing revalidations to see the old value.
   if (m_core_config->timing_mode_vb_commit) {
      if (tp.m_pass) {
         tp.m_tm_manager->commit_core_side();
         tp.delete_tm_manager();
         delete_ptx_thread_tm_manager(warp_id, thread_id % m_warp_size);
      } else {
        tp.m_tm_manager->abort();
        unsigned long long retry_delay = tp.m_tm_manager->abort_count() * 500; 
        if (retry_delay > 5000) retry_delay = 5000; 
        cmt_warp.m_retry_delay = gpu_sim_cycle + gpu_tot_sim_cycle + retry_delay; 
        tp.delete_tm_manager(); // just remove the reference from TLW, thread still need tm_manager for retry
      }
   }

   if (not tp.m_pass) {
      tp.m_cu_commit_pending.reset(); 
   }
}

// handle CU_ALLOC_PASS and CU_ALLOC_FAIL - if fail, retry some time later 
void tx_log_walker::process_cu_alloc_reply(mem_fetch *mf)
{
   // set the state structure of the committing thread accordingly
   int commit_id = mf->get_transaction_id();
   int tid = m_cid2tid[commit_id];
   int wid = tid / m_warp_size;
   int mpid = mf->get_sub_partition_id();
   warp_commit_tx_t &cmt_warp = m_committing_warp[wid];
   assert(cmt_warp.active());
   commit_tx_t &tp = cmt_warp.m_thread_state[tid % m_warp_size];

   assert(tp.m_state == WAIT_CU_ALLOC_REPLY);

   assert( tp.m_pending_cu_alloc.test(mpid) == true ); // no redundant replies
   tp.m_pending_cu_alloc.reset(mpid);
   if(mf->get_type() == CU_ALLOC_PASS) {
      assert(!tp.m_cu_alloc_pass.test(mpid)); // wasn't already passed
      tp.m_cu_alloc_pass.set(mpid);
   } else if ( mf->get_type() == CU_ALLOC_FAIL) {
      assert(!tp.m_cu_alloc_fail.test(mpid)); // wasn't already failed
      tp.m_cu_alloc_fail.set(mpid);
   } else {
      abort();
   }

   // All allocation queries returned
   if(tp.m_pending_cu_alloc.none()) {
      assert( (tp.m_cu_alloc_pass & tp.m_cu_alloc_fail).none() );

      if(tp.m_cu_alloc_fail.none()) {
         // All entries allocated, start sending read set
         tp.m_state = SEND_RS;
         m_stats.m_cu_allocation_retries.add2bin(tp.m_alloc_retries);
      } else {
         // Could not allocate all entries yet, retry with increased timer
         tp.m_alloc_retries++;
         tp.m_pending_cu_alloc = tp.m_cu_alloc_fail; // Resend query to only failed CUs
         tp.m_cu_alloc_fail.reset();
         tp.m_alloc_retry_timeout = gpu_tot_sim_cycle + gpu_sim_cycle + tp.m_alloc_retry_increment;
         tp.m_alloc_retry_increment *= 2;
         if(tp.m_alloc_retry_increment > m_core_config->cu_alloc_max_timeout) {
            tp.m_alloc_retry_increment = m_core_config->cu_alloc_max_timeout;
         }
         tp.m_state = ACQ_CU_ENTRIES;
      }

   }
}

// handle CU_DONE_COMMIT message - unblock the recepient thread and check for potential error 
void tx_log_walker::process_cu_done_commit(mem_fetch *mf)
{
   // set the state structure of the committing thread accordingly
   int commit_id = mf->get_transaction_id();
   int tid = m_cid2tid[commit_id];
   int wid = tid / m_warp_size;
   int mpid = mf->get_sub_partition_id();
   warp_commit_tx_t &cmt_warp = m_committing_warp[wid];
   assert(cmt_warp.active());
   commit_tx_t &tp = cmt_warp.m_thread_state[tid % m_warp_size];

   // only get this when commit done ack traffic is modeled 
   assert(m_core_config->cu_commit_ack_traffic);
   // something is wrong if the same commit pending is reset more than once 
   assert(tp.m_cu_commit_pending.test(mpid) == true); 
   tp.m_cu_commit_pending.reset(mpid);
}

// process replies from commit unit 
// return true if this is a commit unit reply (and processed)
bool tx_log_walker::process_commit_unit_reply(mem_fetch *mf)
{
   // filter out the non-tx messages 
   if (mf->get_access_type() != TX_MSG) return false;

   switch (mf->get_type()) {
   case CU_PASS:
   case CU_FAIL:
      m_stats.n_cu_pass_msg++;
      process_cu_pass_fail(mf); 
      break; 
   case CU_DONE_COMMIT:
      process_cu_done_commit(mf); 
      break; 
   case CU_ALLOC_PASS:
   case CU_ALLOC_FAIL:
      process_cu_alloc_reply(mf); 
      break; 
   case NEWLY_INSERTED_ADDR:
   case REMOVED_ADDR:
      assert(0);
      break;
   default:
      assert(0 && "[TLW] Undefined Message Received!"); 
      break; 
   }

   // return true for intercepting it 
   return true; 
}

#define TX_PACKET_SIZE 8 
mem_fetch* tx_log_walker::create_tx_packet(enum mf_type type, const warp_inst_t &inst, int commit_id, unsigned dst_cu, address_type addr, unsigned size, bool wr)
{   
    mem_fetch* mf = new mem_fetch( mem_access_t(TX_MSG,addr,size,wr), 
                                   &inst, TX_PACKET_SIZE, 
                                   inst.warp_id(), m_core_id, m_cluster_id, 
                                   m_memory_config );
    mf->set_type( type );
    mf->set_is_transactional();
    mf->set_transaction_id( commit_id );

    if (dst_cu != (unsigned)-1) {
        mf->set_sub_partition_id( dst_cu ); // set the destination conflict table 
    }

    return mf; 
}

void tx_log_walker::generate_done_fill(const warp_inst_t &inst, commit_tx_t &tp)
{
   for (unsigned cu = 0; cu < m_memory_config->m_n_mem_sub_partition; cu++) {
      mem_fetch *donefill_mf = NULL;
      if (tp.m_sent_cu_entry.test(cu)) {
         donefill_mf = create_tx_packet(TX_DONE_FILL, inst, tp.m_commit_id, cu, 0xD07EFEEF, 0, false);
      } else {
         if (not m_advance_skip_msg) 
            donefill_mf = create_tx_packet(TX_SKIP, inst, tp.m_commit_id, cu, 0xD07EFEEF, 0, false);
      }
      if (donefill_mf != NULL) {
         donefill_mf->set_commit_pending_ptr(&(tp.m_cu_commit_pending)); 
         m_out_message_queue.push_back(donefill_mf); 
      }
   }
   if (m_timelinef and tlw_watched(tp.m_commit_id)) 
      fprintf(m_timelinef, "tx_cid = %d, SEND_WS->DONE_FILL @ %llu \n", tp.m_commit_id, gpu_sim_cycle + gpu_tot_sim_cycle); 
}

void tx_log_walker::generate_skip(const warp_inst_t &inst, commit_tx_t &tp, int lane_id)
{
   if (not m_advance_skip_msg or tp.m_advance_skip_sent) return;

   // generate sent_cu_entry via the log 
   commit_tx_t::commit_unit_reply_t using_cu_entry((unsigned long long)0);
   const tm_warp_info& warp_info = m_warp[inst.warp_id()].get_tm_warp_info(); 
   const tm_warp_info::tx_log_t &read_log = warp_info.m_read_log; 
   for (unsigned entry_id = 0; entry_id < read_log.size(); entry_id++) {
      addr_t address_tag = read_log[entry_id].m_addr[lane_id];
      if (address_tag != 0) {
         addrdec_t raw_addr; 
         m_memory_config->m_address_mapping.addrdec_tlx(address_tag, &raw_addr);
         using_cu_entry.set(raw_addr.chip); 
      }
   }
   const tm_warp_info::tx_log_t &write_log = warp_info.m_write_log; 
   for (unsigned entry_id = 0; entry_id < write_log.size(); entry_id++) {
      addr_t address_tag = write_log[entry_id].m_addr[lane_id];
      if (address_tag != 0) {
         addrdec_t raw_addr; 
         m_memory_config->m_address_mapping.addrdec_tlx(address_tag, &raw_addr);
         using_cu_entry.set(raw_addr.chip); 
      }
   }
      
   for (unsigned cu = 0; cu < m_memory_config->m_n_mem_sub_partition; cu++) {
      if (using_cu_entry.test(cu) == true) continue; // only send skip to CUs that will not be sent RS/WS
      mem_fetch *skip_mf = create_tx_packet( TX_SKIP, inst, tp.m_commit_id, cu, 0xD07EFEEF, 0, false);
      skip_mf->set_commit_pending_ptr(&(tp.m_cu_commit_pending)); 
      m_out_message_queue.push_back(skip_mf); 
   }

   tp.m_advance_skip_sent = true; 
}

void tx_log_walker::generate_cu_alloc(const warp_inst_t &inst, commit_tx_t &tp)
{
   for (unsigned cu = 0; cu < m_memory_config->m_n_mem_sub_partition; cu++) {
      if(tp.m_pending_cu_alloc.test(cu)) {
         mem_fetch *cualloc_mf = create_tx_packet( TX_CU_ALLOC, inst, tp.m_commit_id, cu, 0xD07EFEEF, 0, false);
         m_out_message_queue.push_back(cualloc_mf);
      }
   }
}

unsigned tx_log_walker::s_n_commited = 0; 

void tx_log_walker::out_message_queue_cycle()
{
   // send message to interconnect 
   if ( !m_out_txreply_queue.empty() ) {
      mem_fetch* out_msg = m_out_txreply_queue.front(); 
      if ( !m_icnt->full(out_msg->get_data_size(), out_msg->get_is_write()) ) {
         m_out_txreply_queue.pop_front();
         m_icnt->push(out_msg);
         m_sent_icnt_traffic += out_msg->get_num_flits(true); // icnt traffic for power model 
         if (m_timelinef and (out_msg->get_type() == TX_PASS or out_msg->get_type() == TX_FAIL)) 
            fprintf(m_timelinef, "tx_cid = %d, reply to %d @ %llu\n", out_msg->get_transaction_id(), out_msg->get_sub_partition_id(), gpu_sim_cycle + gpu_tot_sim_cycle); 
      }
   } else if ( !m_out_message_queue.empty() ) {
      mem_fetch* out_msg = m_out_message_queue.front(); 
      if ( !m_icnt->full(out_msg->get_data_size(), out_msg->get_is_write()) ) {
         m_out_message_queue.pop_front();
         m_icnt->push(out_msg);
         m_sent_icnt_traffic += out_msg->get_num_flits(true); // icnt traffic for power model 
         if (m_timelinef and (out_msg->get_type() == TX_PASS or out_msg->get_type() == TX_FAIL)) 
            fprintf(m_timelinef, "tx_cid = %d, reply to %d @ %llu (slow queue)\n", out_msg->get_transaction_id(), out_msg->get_sub_partition_id(), gpu_sim_cycle + gpu_tot_sim_cycle); 
         if (m_timelinef and tlw_watched(out_msg->get_transaction_id())) {
            if (out_msg->get_type() == TX_READ_SET) {
               fprintf(m_timelinef, "tx_cid = %d, TX_READ_SET to %d @ %llu (slow queue)\n", out_msg->get_transaction_id(), out_msg->get_sub_partition_id(), gpu_sim_cycle + gpu_tot_sim_cycle); 
            } else if (out_msg->get_type() == TX_WRITE_SET) {
               fprintf(m_timelinef, "tx_cid = %d, TX_WRITE_SET to %d @ %llu (slow queue)\n", out_msg->get_transaction_id(), out_msg->get_sub_partition_id(), gpu_sim_cycle + gpu_tot_sim_cycle); 
            } else if (out_msg->get_type() == TX_DONE_FILL) {
               fprintf(m_timelinef, "tx_cid = %d, TX_DONE_FILL to %d @ %llu (slow queue)\n", out_msg->get_transaction_id(), out_msg->get_sub_partition_id(), gpu_sim_cycle + gpu_tot_sim_cycle); 
            }
         }
      }
   }
   
   m_stats.m_out_message_queue_size.add2bin(m_out_message_queue.size());
   m_stats.m_out_txreply_queue_size.add2bin(m_out_txreply_queue.size());
}

bool tx_log_walker::has_out_message() const  
{ 
   return (not (m_out_txreply_queue.empty() and m_out_message_queue.empty())); 
}

void tx_log_walker::acquire_cid( unsigned wid, unsigned thread_id, commit_tx_t &tp )
{
   tp.m_commit_id = warp_commit_tx_t::alloc_commit_id();
   tp.m_alloc_time = gpu_tot_sim_cycle + gpu_sim_cycle;
   tp.setup_log(m_warp[wid].get_tm_warp_info(), true, true); 
   m_cid2tid[tp.m_commit_id] = thread_id;
   if (m_timelinef and tlw_watched(tp.m_commit_id)) 
      fprintf(m_timelinef, "tx_cid = %d, ACQ_CID->SEND_RS @ %llu \n", tp.m_commit_id, gpu_sim_cycle + gpu_tot_sim_cycle); 

   // Skip allocation handshakes for infinite cache
   if(m_core_config->cu_finite)
      tp.m_state = ACQ_CU_ENTRIES;
   else if (m_core_config->tlw_use_logical_temporal_cd || g_tm_options.m_eager_warptm_enabled) {
      assert(tp.read_log_sent());
      tp.m_state = SEND_WS;
   } else {
      tp.m_state = SEND_RS;
   }

   // Set initial state of CU allocation vector
   tp.enter_acq_cu_entries(m_memory_config->m_n_mem_sub_partition, m_core_config->cu_alloc_init_timeout);
}

void tx_log_walker::cycle()
{
   out_message_queue_cycle();

   if (m_bg_commit_warps.empty()) return; // no warp committing in background 

   int wid = m_bg_commit_warps.front(); 
   bool cache_accessed = false;
   unsigned cu_accessed = -1;
   bool yield_next_warp = false;

   warp_commit_tx_t &cmt_warp = m_committing_warp[wid]; 
   assert(cmt_warp.active()); 
   commit_tx_t &tp = cmt_warp.m_thread_state[cmt_warp.m_thread_processing]; 
   switch (tp.m_state) {
   case INTRA_WARP_CD: 
      assert(0); 
      break; 
   case ACQ_CID: 
      acquire_cid(wid, wid * m_warp_size + cmt_warp.m_thread_processing, tp); 

      cmt_warp.next_thread(); 
      break;
   case ACQ_CU_ENTRIES:
      // Send or resend allocation requests
      if(tp.m_alloc_retry_timeout <= gpu_tot_sim_cycle + gpu_sim_cycle) {
         assert(tp.m_pending_cu_alloc.any());
         generate_cu_alloc(cmt_warp.m_inst, tp);
         tp.m_state = WAIT_CU_ALLOC_REPLY;
      }
      cmt_warp.next_thread();
      yield_next_warp = true;
      break;
   case WAIT_CU_ALLOC_REPLY:
      // Waiting for allocation handshake reply from CUs
      assert(tp.m_pending_cu_alloc.any());
      cmt_warp.next_thread();
      yield_next_warp = true;
      break;
   case SEND_RS: {
      generate_skip(cmt_warp.m_inst, tp, cmt_warp.m_thread_processing); 
      // generate access to L1D and send read-log out to commit unit 
      if (tp.read_log_sent()) {
         printf("[TX_LOG_WALKER]: Empty Read-Set Transaction: CommitID = %d core%d warp%d\n", tp.m_commit_id, m_core_id, wid); 
         tp.m_state = SEND_WS;
      } else {
         bool rentry_sent = send_log_entry(cmt_warp.m_inst, cmt_warp.m_thread_processing, 
                                           tp.m_commit_id, tp.read_log_front(), cache_accessed, cu_accessed);
         if (rentry_sent) {
            if (m_timelinef and tlw_watched(tp.m_commit_id)) 
               fprintf(m_timelinef, "tx_cid = %d, SEND_RS @ %llu \n", tp.m_commit_id, gpu_sim_cycle + gpu_tot_sim_cycle); 
            if(cu_accessed != (unsigned) -1)
                tp.m_sent_cu_entry.set(cu_accessed);
            send_coalesced_packet(); 
            tp.next_read_log_send(); 
            cmt_warp.next_thread(); 
            if (tp.read_log_sent()) {
               tp.m_state = SEND_WS;
               if (m_timelinef and tlw_watched(tp.m_commit_id)) 
                  fprintf(m_timelinef, "tx_cid = %d, SEND_RS->SEND_WS @ %llu \n", tp.m_commit_id, gpu_sim_cycle + gpu_tot_sim_cycle); 
            }
         } else {
            if (tp.read_log_front().atag_cachemiss || tp.read_log_front().data_cachemiss) {
               yield_next_warp = true; 
            }  
         }
      }
      } break; 
   case SEND_WS: {
      // generate access to L1D and send write-log out to commit unit 
      if (tp.write_log_sent()) {
         // empty write log -- this is normal 
         // send DONE_FILL message to commit unit
         generate_done_fill(cmt_warp.m_inst, tp);
         tp.enter_write_cu_reply(m_memory_config->m_n_mem_sub_partition); 
         cmt_warp.next_thread(); 
      } else {
         bool wentry_sent = send_log_entry(cmt_warp.m_inst, cmt_warp.m_thread_processing, 
                                           tp.m_commit_id, tp.write_log_front(), cache_accessed, cu_accessed);
         if (wentry_sent) {
            if (m_timelinef and tlw_watched(tp.m_commit_id)) 
               fprintf(m_timelinef, "tx_cid = %d, SEND_WS @ %llu \n", tp.m_commit_id, gpu_sim_cycle + gpu_tot_sim_cycle); 
            if(cu_accessed != (unsigned) -1)
                 tp.m_sent_cu_entry.set(cu_accessed);
            send_coalesced_packet(); 
            tp.next_write_log_send(); 
            if (tp.write_log_sent()) {
               // send DONE_FILL message to commit unit
               generate_done_fill(cmt_warp.m_inst, tp);
               tp.enter_write_cu_reply(m_memory_config->m_n_mem_sub_partition); 
               cmt_warp.next_thread(); 
            } else 
               cmt_warp.next_thread(); 
         } else {
            if (tp.write_log_front().atag_cachemiss || tp.write_log_front().data_cachemiss) {
               yield_next_warp = true; 
            }  
         } 
      }
      } break; 
   case WAIT_CU_REPLY: 
      // tp.m_pass = true; 
      // tp.m_state = SEND_ACK_CLEANUP;
      assert(!tp.m_pending_cu_reply.none());
      cmt_warp.next_thread();
      break; 
   case SEND_ACK_CLEANUP: 
      if (tp.m_cu_commit_pending.any() or 
          (cmt_warp.m_retry_delay != 0 and gpu_sim_cycle + gpu_tot_sim_cycle <= cmt_warp.m_retry_delay)) {
         if (cmt_warp.all_sent()) {
            yield_next_warp = true; 
         } else {
            cmt_warp.next_thread_to_non_waiting();
         }
      } else {
         tp.m_state = IDLE; 
         //s_n_commited += 1;
         cmt_warp.next_thread(); 
      }
      break; 
   default: assert(0); 
   }; 

   if (cmt_warp.all_commit()) {
      // finish commit push the commit instruction to finish queue to unlock scoreboard at writeback stage 
      m_finish_commit_q.push_front(m_committing_warp[wid].m_inst); 
      m_bg_commit_warps.pop_front(); 
      m_committing_warp[wid].reset(); 
      m_warp[wid].get_tm_warp_info().reset(); 

      m_core->commit_warp_cleanup(wid); 

   } else if (yield_next_warp or cmt_warp.all_sent()) {
      // all request from this warp is sent yield to other warps to send logs  
      m_bg_commit_warps.pop_front(); 
      m_bg_commit_warps.push_back(wid); 
   }

}

void tx_log_walker::print(FILE *fout)
{
   fprintf(fout, "Committing Warp: ");
   for (std::list<int>::iterator ibgwarp = m_bg_commit_warps.begin(); ibgwarp != m_bg_commit_warps.end(); ++ibgwarp) {
      fprintf(fout, "%d ", *ibgwarp); 
   }
   fprintf(fout, "\n"); 
   fprintf(fout, "Finish Commit Queue: ");
   for (std::list<warp_inst_t>::iterator ifwarp = m_finish_commit_q.begin(); ifwarp != m_finish_commit_q.end(); ++ifwarp) {
      fprintf(fout, "%d ", ifwarp->warp_id()); 
   }
   fprintf(fout, "\n"); 
}

bool tx_log_walker::commit_ready()
{
   return (!m_finish_commit_q.empty()); 
}

warp_inst_t& tx_log_walker::next_commit_ready()
{
   return m_finish_commit_q.back(); 
}

void tx_log_walker::pop_commit_ready()
{
   m_finish_commit_q.pop_back();
}

// return true if given thread is doing a nested tx commit, which should be ignored by timing model  
// for timing mode commit, it also decrement the tx nesting level 
bool tx_log_walker::is_nested_tx_commit(const warp_inst_t &inst, int lane_id)
{
   assert(inst.active(lane_id)); 
   int warp_id = inst.warp_id(); 

   int tid = warp_id * m_warp_size + lane_id; 
   tm_manager_inf *thread_tm_manager = m_core->get_func_thread_info(tid)->get_tm_manager();

   if (m_core_config->timing_mode_vb_commit) {
      assert(thread_tm_manager->nesting_level() > 0); 
      if (thread_tm_manager->nesting_level() > 1) {
         // nested tx 
         assert(m_committing_warp[warp_id].m_active == false); // this warp should not be in the middle of commit 
         // just call this for timing mode commit to decrement the nesting level 
         bool committed = thread_tm_manager->commit(true); 
         assert(committed == false); // it should not commit 
         return true; 
      } else {
         // tx at base level 
         return false; 
      }
   } else {
      if (thread_tm_manager == NULL) {
         // tm_manager is set to NULL --> transaction called commit() at base-level and passed validation
         return false; 
      } else {
	 if (g_tm_options.m_eager_warptm_enabled) {
             if (thread_tm_manager->nesting_level()) {
	         return true;
	     } else {
	         return false;
	     }
	 }

         // transaction is either nested or aborted -- cannot determine which one it is with nesting level
         // HACK: check the PC - if it is reset, then it will not be the one after __tcommit()
         // this will not work if other instructions in the thread can run in parallel with commit 
         unsigned thread_pc = m_core->get_func_thread_info(tid)->get_pc(); 
         unsigned post_commit_pc = inst.pc + inst.isize; 
         if (thread_pc == post_commit_pc) {
            // nested commit 
            return true; 
         } else {
            // aborted tx 
            return true; 
         }
      }
   }
}

bool tx_log_walker::is_nested_tx_commit(const warp_inst_t &inst)
{
   active_mask_t nested_commit;
   active_mask_t active_thd;
   for (unsigned t = 0; t < m_warp_size; t++) {
      if (inst.active(t)) {
         active_thd.set(t); 
         if (is_nested_tx_commit(inst, t)) {
            nested_commit.set(t); 
         }
      }
   }
   if (nested_commit.any()) {
      // ensure the warp as a whole is nested commit 
      assert(nested_commit == active_thd); 
   } // else nothing in nested_commit is set 

   return (nested_commit.any());
}

// collect the statistics for microarchitecture activity due to intra warp conflict detection 
void tx_log_walker::collect_intra_warpcd_uarch_activity_stats(iwcd_uarch_info &uarch_activity) 
{
   unsigned operation_cycles = uarch_activity.operation_cycles(); 
   m_stats.m_intra_warp_cd_cycle[m_core_id] += operation_cycles; 
   m_stats.m_intra_warp_cd_cycle_per_warp.add2bin(operation_cycles); 
}

// detect and resolve live locking in intra warp conflict detection, return the new abort mask 
active_mask_t tx_log_walker::resolve_intra_warp_cd(warp_inst_t &inst, active_mask_t abort_mask)
{
   if (g_tm_options.m_early_abort_enabled) {
      early_abort_checking(inst, abort_mask);
   }
   // live lock resolve: just commit the first active thread, abort the rest 
   if (abort_mask == inst.get_active_mask()) {
      const unsigned warp_size = m_core_config->warp_size; 
      for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
         if (abort_mask.test(lane_id) == true) {
            abort_mask.reset(lane_id); 
            break; 
         }
      }
      m_stats.n_intra_warp_complete_abort += 1; 
   }
   return abort_mask; 
}

// detection conflicts within a warp and resolve them with aborts
void tx_log_walker::intra_warp_conflict_detection(warp_inst_t &inst, iwcd_uarch_info &uarch_activity) 
{
   active_mask_t conflict_free_active_mask = inst.get_active_mask(); 

   m_stats.m_intra_warp_pre_cd_active.add2bin(inst.active_count()); 

   unsigned warp_id = inst.warp_id(); 

   active_mask_t abort_mask_perfect = perfect_intra_warp_conflict_detection(inst); 

   active_mask_t abort_mask; 
   // only do the detection if more than one thread is active for commit 
   if (inst.active_count() > 1) {
      switch (m_core_config->tlw_intra_warp_cd_type) {
      case 0: 
         abort_mask = abort_mask_perfect; break; 
      case 1: // mark and check, no priority resolve
         abort_mask = mark_check_intra_warp_conflict_detection(inst, uarch_activity, false); break; 
      case 2: // mark and check with bloom filter, no priority resolve 
         abort_mask = mark_check_intra_warp_conflict_detection(inst, uarch_activity, true); break; 
      case 3: // mark and check without bloom filter, priority resolve 
         abort_mask = mark_priority_check_intra_warp_conflict_detection(inst, uarch_activity, false); break; 
      case 4: // mark and check without bloom filter, priority resolve 
         abort_mask = mark_priority_check_intra_warp_conflict_detection(inst, uarch_activity, true); break; 
      case 5: // recency bloom filter (works more like a normal bloom filter)
         abort_mask = recency_bloom_filter_intra_warp_conflict_detection(inst, uarch_activity); break; 
      case 6: // prefix-sum bloom filter array 
         abort_mask = prefix_sum_bloom_filter_intra_warp_conflict_detection(inst, uarch_activity); break; 
      default: 
         assert(0 && "Unknown intra-warp conflict detection"); 
      } 
   }

   m_stats.m_intra_warp_aborts.add2bin(abort_mask.count()); 

   // aborted transaction has to be active 
   active_mask_t invalid_abort = abort_mask & ~inst.get_active_mask(); 
   assert(invalid_abort.any() == false); 

   // transactions that is aborted due to false positives
   active_mask_t false_abort = abort_mask & ~abort_mask_perfect; 
   m_stats.n_intra_warp_aborts_false_positive += false_abort.count();

   if (inst.is_warp_level and abort_mask.any()) {
      // intra warp conflict not allowed for warp level transaction could indicate error 
      if ( g_debug_execution > 4 ) 
         printf("[TX-Log Walker] Warning: Intra-warp conflict detected in core %d warp %u.\n", 
                m_core->get_sid(), warp_id); 
   }

   // detect conflicts among the transactions pairware across the active lanes 
   // resolve conflict by aborting the lane with higher lane id 
   for (unsigned t = 0; t < inst.warp_size(); t++) {
      if (inst.active(t) == false) continue; 
      if (abort_mask.test(t) == false) continue; 
      int t_tid = warp_id * m_warp_size + t; 
      tm_manager_inf *t_tm_manager = m_core->get_func_thread_info(t_tid)->get_tm_manager(); 

      t_tm_manager->abort();
      m_stats.n_intra_warp_conflicts_detected += 1; 
      conflict_free_active_mask.reset(t); 
      // clear the mask for the aborted transactions
      inst.set_not_active(t); 
      // clean up thread state bookkeeping // TODO: put this up in a shader_core_ctx member function
      m_core->get_thread_ctx(t_tid)->m_in_pipeline -= 1;
      if(m_core->get_thread_ctx(t_tid)->m_in_pipeline < 0) {
         printf("Error: sid=%d wid=%d tid=%d \n", m_core->get_sid(), inst.warp_id(), t_tid);
      }
      assert(m_core->get_thread_ctx(t_tid)->m_in_pipeline >= 0);
      if (t_tm_manager->get_is_abort_need_clean() == false)
	  inst.set_not_issued(t);
   }
   assert(inst.get_active_mask() == conflict_free_active_mask); 
}

// perfect conflict detection within a warp, return the mask for threads to be aborted 
active_mask_t tx_log_walker::perfect_intra_warp_conflict_detection(warp_inst_t &inst) 
{
   active_mask_t conflict_free_active_mask = inst.get_active_mask(); 
   active_mask_t abort_mask; 

   unsigned warp_id = inst.warp_id(); 

   // detect conflicts among the transactions pairware across the active lanes 
   // resolve conflict by aborting the lane with higher lane id 
   for (unsigned t = 0; t < inst.warp_size(); t++) {
      if (inst.active(t) == false) continue; 
      if (abort_mask.test(t) == true) continue; // no need to resolve conflict for an aborted transaction
      int t_tid = warp_id * m_warp_size + t; 
      tm_manager_inf *t_tm_manager = m_core->get_func_thread_info(t_tid)->get_tm_manager(); 
      for (unsigned s = t + 1; s < inst.warp_size(); s++) {
         if (inst.active(s) == false) continue; 
         if (abort_mask.test(s) == true) continue; // no need to abort an aborted transaction
         int s_tid = warp_id * m_warp_size + s; 
         tm_manager_inf *s_tm_manager = m_core->get_func_thread_info(s_tid)->get_tm_manager(); 

         if (t_tm_manager->has_conflict_with(s_tm_manager) == true) {
            abort_mask.set(s); 
         }
      }
   }
   assert(inst.get_active_mask() == conflict_free_active_mask); 
   return abort_mask; 
}

#include "../cuda-sim/hashfunc.h"

class ownership_table 
{
public:
   ownership_table(unsigned size, bool use_bloom_filter) 
      : m_table_size( (use_bloom_filter)? (size / 2) : size ), 
        m_table(m_table_size, s_null_id), m_table_second_hash(m_table_size, s_null_id), 
        m_bloom_filter(use_bloom_filter) 
      { } 

   // mark the thread's ownership of the address region corresponding to address_tag 
   void mark(addr_t address_tag, int thread_id) 
   {
      m_infinite_table[address_tag] = thread_id; 
      if (infinite() == false) {
         addr_t hashed_addr = hash_address_prime(address_tag); 
         m_table[hashed_addr] = thread_id; 
         addr_t hashed_addr2 = hash_address_second(address_tag); 
         m_table_second_hash[hashed_addr2] = thread_id; 
      }
   }

   // check the ownership of a address region 
   bool check(addr_t address_tag, int thread_id) 
   {
      int owner_thread = get_owner(address_tag); 
      if (owner_thread == s_null_id) 
         return true; 
      else 
         return (thread_id == owner_thread); 
   }

   // return owner of the address region where address_tag belongs 
   int get_owner(addr_t address_tag) 
   {
      int owner_thread = get_owner_infinite(address_tag); 
      if (infinite() == false) {
         addr_t hashed_addr = hash_address_prime(address_tag); 
         int owner_thread_prime = m_table[hashed_addr]; 
         if (m_bloom_filter) {
            addr_t hashed_addr2 = hash_address_second(address_tag); 
            int owner_thread_second = m_table_second_hash[hashed_addr2]; 
            // give priority to threads from lower lane (and return null_id if either is)
            owner_thread = std::min(owner_thread_prime, owner_thread_second); 
         } else {
            owner_thread = owner_thread_prime; 
         }
      }
      return owner_thread; 
   }

   size_t get_size() const 
   {
      return m_infinite_table.size(); 
   }

   void get_aliasing_depth( int &avg, int &max, int &usage ) const 
   {
      max = 0; 
      std::vector<int> aliasing_depth(m_table.size(), 0); 
      for (auto i_entry = m_infinite_table.begin(); i_entry != m_infinite_table.end(); ++i_entry) {
         addr_t hashed_addr = hash_address_prime(i_entry->first); 
         aliasing_depth[hashed_addr] += 1; 
         max = std::max(max, aliasing_depth[hashed_addr]); 
      }

      // calculate the average depth from non-zero entries 
      int sum = 0; 
      int non_zero_entries = 0; 
      for (unsigned i = 0; i < aliasing_depth.size(); i++) {
         if (aliasing_depth[i] > 0) {
            sum += aliasing_depth[i]; 
            non_zero_entries += 1; 
         }
      }
      avg = sum / non_zero_entries; 
      usage = non_zero_entries; 
   }

   void print_table(FILE *fout) GPGPU_SIM_DEBUG_FUNCTION
   {
      for (auto i_entry = m_infinite_table.begin(); i_entry != m_infinite_table.end(); ++i_entry) {
         addr_t hashed_addr = hash_address_prime(i_entry->first); 
         int owner1 = m_table[hashed_addr]; 
         addr_t hashed_addr2 = hash_address_second(i_entry->first); 
         int owner2 = m_table_second_hash[hashed_addr2]; 
         fprintf(fout, "[0x%08x] %4d --> (%4u, %4d) (%4u, %4d)\n", 
                 i_entry->first, i_entry->second, hashed_addr, owner1, hashed_addr2, owner2); 
      }
   }

protected: 
   // the table that marks the ownership of address regions by a thread 
   size_t m_table_size; 
   std::vector<int> m_table; 
   std::vector<int> m_table_second_hash; 
   bool m_bloom_filter; 
   tr1_hash_map<addr_t, int> m_infinite_table; 
   static const int s_null_id = -1; 

   // hash the given address into a position in the ownership table 
   addr_t hash_address(addr_t address_tag, addr_t odd_hash_multiple) const 
   {
      const addr_t table_size = m_table.size(); 
      addr_t hashed_addr = address_tag % table_size; 
      hashed_addr += (address_tag / table_size) * odd_hash_multiple; 
      hashed_addr %= table_size; 
      return hashed_addr; 
   }

   addr_t hash_address_prime(addr_t address_tag) const 
   {
      const addr_t odd_hash_multiple = 17; 
      return hash_address(address_tag, odd_hash_multiple); 
   }

   addr_t hash_address_second(addr_t address_tag) const 
   {
      const addr_t odd_hash_multiple = 27; 
      return hash_address(address_tag, odd_hash_multiple); 
   }

   // retrieve owner from the infinite ownership table 
   int get_owner_infinite(addr_t address_tag) 
   {
      int owner_thread; 
      auto i_owner = m_infinite_table.find(address_tag); 
      if (i_owner != m_infinite_table.end()) {
         owner_thread = i_owner->second; 
      } else {
         m_infinite_table[address_tag] = s_null_id; 
         owner_thread = s_null_id; 
      }
      return owner_thread; 
   }

   // if table has infinite capacity 
   bool infinite() const { return (m_table.size() == 0); }
}; 

// mark and check conflict resolution within a warp, return the mask for threads to be aborted 
active_mask_t tx_log_walker::mark_check_intra_warp_conflict_detection(warp_inst_t &inst, 
                                                                      iwcd_uarch_info &uarch_activity, 
                                                                      bool use_bloom_filter)
{
   active_mask_t abort_mask; 

   unsigned warp_id = inst.warp_id(); 
   const unsigned warp_size = m_core_config->warp_size; 
   const tm_warp_info& warp_info = m_warp[warp_id].get_tm_warp_info(); 

   const unsigned table_size = m_core_config->tlw_mark_check_ownership_size; 
   ownership_table ownership(table_size, use_bloom_filter); 
   unsigned smem_acc_per_mark = (use_bloom_filter)? 2 : 1; 

   // Initialization of ownership table 
   if (table_size > 0) {
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, table_size / (warp_size * 4)); 
   }

   // Mark write-set 
   for (unsigned wt = 0; wt < warp_info.m_write_log_size; wt++) {
      for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
         if (inst.active(lane_id) == false) continue;  // committed or aborted thread
         addr_t address_tag = warp_info.m_write_log[wt].m_addr[lane_id]; 
         if (address_tag != 0) {
            ownership.mark(address_tag, lane_id); 
         }
      }
      uarch_activity.queue_event(iwcd_uarch_info::WRITE_LOG_LOAD, wt); 
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, smem_acc_per_mark); 
   }

   // Check read-set
   for (unsigned rd = 0; rd < warp_info.m_read_log_size; rd++) {
      for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
         if (inst.active(lane_id) == false) continue;  // committed or aborted thread
         addr_t address_tag = warp_info.m_read_log[rd].m_addr[lane_id]; 
         if (address_tag != 0) {
            bool conflict_free = ownership.check(address_tag, lane_id); 
            if (conflict_free == false) {
               abort_mask.set(lane_id); 
            }
         }
      }
      uarch_activity.queue_event(iwcd_uarch_info::READ_LOG_LOAD, rd); 
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, smem_acc_per_mark); 
   }

   // Check write-set 
   for (unsigned wt = 0; wt < warp_info.m_write_log_size; wt++) {
      for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
         if (inst.active(lane_id) == false) continue;  // committed or aborted thread
         addr_t address_tag = warp_info.m_write_log[wt].m_addr[lane_id]; 
         if (address_tag != 0) {
            bool conflict_free = ownership.check(address_tag, lane_id); 
            if (conflict_free == false) {
               abort_mask.set(lane_id); 
            }
         }
      }
      uarch_activity.queue_event(iwcd_uarch_info::WRITE_LOG_LOAD, wt); 
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, smem_acc_per_mark); 
   }

   abort_mask = resolve_intra_warp_cd(inst, abort_mask); 

   m_stats.m_ownership_table_size.add2bin(ownership.get_size()); 
   if (table_size != 0) {
      int avg, max, usage; 
      ownership.get_aliasing_depth(avg, max, usage); 
      m_stats.m_ownership_aliasing_depth_avg.add2bin(avg); 
      m_stats.m_ownership_aliasing_depth_max.add2bin(max); 
      m_stats.m_ownership_aliasing_depth_usage.add2bin(usage); 
   }

   collect_intra_warpcd_uarch_activity_stats(uarch_activity); 

   return abort_mask; 
}

// mark and priority check conflict resolution within a warp, return the mask for threads to be aborted 
active_mask_t tx_log_walker::mark_priority_check_intra_warp_conflict_detection(warp_inst_t &inst, 
                                                                               iwcd_uarch_info &uarch_activity, 
                                                                               bool use_bloom_filter)
{
   active_mask_t abort_mask; 

   unsigned warp_id = inst.warp_id(); 
   const unsigned warp_size = m_core_config->warp_size; 
   const tm_warp_info& warp_info = m_warp[warp_id].get_tm_warp_info(); 

   const unsigned table_size = m_core_config->tlw_mark_check_ownership_size; 
   ownership_table ownership(table_size, use_bloom_filter); 
   unsigned smem_acc_per_mark = (use_bloom_filter)? 2 : 1; 

   // Initialization of ownership table 
   if (table_size > 0) {
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, table_size / (warp_size * 4)); 
   }

   // Priority Check and Mark write-set - after this each entry should contain
   // conservatively the earliest writer
   for (unsigned wt = 0; wt < warp_info.m_write_log_size; wt++) {
      bool marking_entry = false; 
      for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
         if (inst.active(lane_id) == false) continue;  // committed or aborted thread
         addr_t address_tag = warp_info.m_write_log[wt].m_addr[lane_id]; 
         if (address_tag != 0) {
            int lane_thread_id = lane_id; 
            int owner_thread = ownership.get_owner(address_tag); 
            // assert(owner_thread != -1); 
            if (owner_thread == lane_thread_id) continue; 
            if (owner_thread > lane_thread_id or owner_thread == -1) {
               ownership.mark(address_tag, lane_thread_id); 
               marking_entry = true; 
            }
         }
      }
      uarch_activity.queue_event(iwcd_uarch_info::WRITE_LOG_LOAD, wt); 
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, smem_acc_per_mark); 
      if (marking_entry) {
         uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, smem_acc_per_mark); 
      }
   }

   // Check read-set
   for (unsigned rd = 0; rd < warp_info.m_read_log_size; rd++) {
      for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
         if (inst.active(lane_id) == false) continue;  // committed or aborted thread
         addr_t address_tag = warp_info.m_read_log[rd].m_addr[lane_id]; 
         if (address_tag != 0) {
            int lane_thread_id = lane_id; 
            int owner_thread = ownership.get_owner(address_tag); 
            if (owner_thread == -1) continue; 
            if (lane_thread_id > owner_thread) {
               abort_mask.set(lane_thread_id); 
            } 
            // (lane_thread_id <= owner_thread) means that the lane_thread
            // should see the value prior to the owner_thread commit
         }
      }
      uarch_activity.queue_event(iwcd_uarch_info::READ_LOG_LOAD, rd); 
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, smem_acc_per_mark); 
   }

   // Check write-set 
   for (unsigned wt = 0; wt < warp_info.m_write_log_size; wt++) {
      for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
         if (inst.active(lane_id) == false) continue;  // committed or aborted thread
         addr_t address_tag = warp_info.m_write_log[wt].m_addr[lane_id]; 
         if (address_tag != 0) {
            bool conflict_free = ownership.check(address_tag, lane_id); 
            if (conflict_free == false) {
               abort_mask.set(lane_id); 
            }
         }
      }
      uarch_activity.queue_event(iwcd_uarch_info::WRITE_LOG_LOAD, wt); 
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, smem_acc_per_mark); 
   }

   abort_mask = resolve_intra_warp_cd(inst, abort_mask); 

   m_stats.m_ownership_table_size.add2bin(ownership.get_size()); 
   if (table_size != 0) {
      int avg, max, usage; 
      ownership.get_aliasing_depth(avg, max, usage); 
      m_stats.m_ownership_aliasing_depth_avg.add2bin(avg); 
      m_stats.m_ownership_aliasing_depth_max.add2bin(max); 
      m_stats.m_ownership_aliasing_depth_usage.add2bin(usage); 
   }

   collect_intra_warpcd_uarch_activity_stats(uarch_activity); 

   return abort_mask; 
}

// resolve conflict with recency bloom filter within a warp, return the mask for threads to be aborted 
active_mask_t tx_log_walker::recency_bloom_filter_intra_warp_conflict_detection(warp_inst_t &inst, 
                                                                                iwcd_uarch_info &uarch_activity)
{
   active_mask_t abort_mask; 

   unsigned warp_id = inst.warp_id(); 
   const unsigned warp_size = m_core_config->warp_size; 
   const tm_warp_info& warp_info = m_warp[warp_id].get_tm_warp_info(); 

   std::vector<int> funct_ids(4, 0); 
   funct_ids[0] = 0; 
   funct_ids[1] = 1; 
   funct_ids[2] = 2; 
   funct_ids[3] = 3; 
   const unsigned table_size = m_core_config->tlw_mark_check_ownership_size; 
   const unsigned bf_hashes = 4; 
   versioning_bloomfilter version_bf(table_size / bf_hashes, funct_ids, bf_hashes); 

   for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
      if (inst.active(lane_id) == false) continue; // skip inactive lanes 

      // Initialization of bloom filter 
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, table_size / (warp_size * 32)); // each thread can clear 32-bit

      bool conflict_free = true; 
      unsigned int transaction_id = lane_id + 1; 
      // Check read-set
      for (unsigned rd = 0; rd < warp_info.m_read_log_size; rd++) {
         addr_t address_tag = warp_info.m_read_log[rd].m_addr[lane_id]; 
         uarch_activity.queue_event(iwcd_uarch_info::READ_LOG_LOAD, rd); 
         if (address_tag != 0) {
            unsigned last_writer = version_bf.get_version(address_tag); 
            uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, bf_hashes); 
            if (last_writer > 0) {
               conflict_free = false; 
            }
         }
      }
      // Check write-set
      for (unsigned wt = 0; wt < warp_info.m_write_log_size; wt++) {
         addr_t address_tag = warp_info.m_write_log[wt].m_addr[lane_id]; 
         uarch_activity.queue_event(iwcd_uarch_info::WRITE_LOG_LOAD, wt); 
         if (address_tag != 0) {
            unsigned last_writer = version_bf.get_version(address_tag); 
            uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, bf_hashes); 
            if (last_writer > 0) {
               conflict_free = false; 
            }
         }
      }
      // Mark write-set 
      if (conflict_free) {
         for (unsigned wt = 0; wt < warp_info.m_write_log_size; wt++) {
            addr_t address_tag = warp_info.m_write_log[wt].m_addr[lane_id]; 
            uarch_activity.queue_event(iwcd_uarch_info::WRITE_LOG_LOAD, wt); 
            if (address_tag != 0) {
               version_bf.update_version(address_tag, transaction_id); 
               uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, bf_hashes); 
            }
         }
      } else {
         abort_mask.set(lane_id); 
      }
   }

   abort_mask = resolve_intra_warp_cd(inst, abort_mask); 

   collect_intra_warpcd_uarch_activity_stats(uarch_activity); 

   return abort_mask; 
}

// resolve conflict with prefix-sum generated bloom filters within a warp, return the mask for threads to be aborted 
active_mask_t tx_log_walker::prefix_sum_bloom_filter_intra_warp_conflict_detection(warp_inst_t &inst, 
                                                                                   iwcd_uarch_info &uarch_activity)
{
   active_mask_t abort_mask; 

   unsigned warp_id = inst.warp_id(); 
   const unsigned warp_size = m_core_config->warp_size; 
   const tm_warp_info& warp_info = m_warp[warp_id].get_tm_warp_info(); 

   std::vector<int> funct_ids(4, 0); 
   funct_ids[0] = 0; 
   funct_ids[1] = 1; 
   funct_ids[2] = 2; 
   funct_ids[3] = 3; 
   const unsigned table_size = m_core_config->tlw_mark_check_ownership_size; 
   const unsigned bf_hashes = 2; 
   // Using recency bloom filter as a simple bloom filter 
   std::vector<versioning_bloomfilter> version_bf(32, versioning_bloomfilter(table_size / bf_hashes, funct_ids, bf_hashes)); 

   // Initialization of bloom filter 
   uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, table_size / 32); // each thread can clear 32-bit

   // Create individual bloom filter from write-set 
   for (unsigned wt = 0; wt < warp_info.m_write_log_size; wt++) {
      for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
         if (inst.active(lane_id) == false) continue;  // committed or aborted thread
         addr_t address_tag = warp_info.m_write_log[wt].m_addr[lane_id]; 
         if (address_tag != 0) {
            for (unsigned later_thread = lane_id + 1; later_thread < warp_size; later_thread++) {
               version_bf[later_thread].update_version(address_tag, lane_id + 1); 
            }
         }
      }
      uarch_activity.queue_event(iwcd_uarch_info::WRITE_LOG_LOAD, wt); 
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, bf_hashes); 
   }

   // timing overhead for prefix-sum (in log(n) steps) 
   uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, (table_size / 32) * 5 * 2); 

   // Check write-set against bloom filter that contains write-sets from all prior lanes 
   for (unsigned wt = 0; wt < warp_info.m_write_log_size; wt++) {
      for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
         if (inst.active(lane_id) == false) continue;  // committed or aborted thwrite
         addr_t address_tag = warp_info.m_write_log[wt].m_addr[lane_id]; 
         if (address_tag != 0) {
            unsigned int last_writer = version_bf[lane_id].get_version(address_tag);
            if (last_writer != 0) {
               abort_mask.set(lane_id); 
            }
         }
      }
      uarch_activity.queue_event(iwcd_uarch_info::WRITE_LOG_LOAD, wt); 
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, bf_hashes); 
   }

   // Check read-set against bloom filter that contains write-sets from all prior lanes 
   for (unsigned rd = 0; rd < warp_info.m_read_log_size; rd++) {
      for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
         if (inst.active(lane_id) == false) continue;  // committed or aborted thread
         addr_t address_tag = warp_info.m_read_log[rd].m_addr[lane_id]; 
         if (address_tag != 0) {
            unsigned int last_writer = version_bf[lane_id].get_version(address_tag);
            if (last_writer != 0) {
               abort_mask.set(lane_id); 
            }
         }
      }
      uarch_activity.queue_event(iwcd_uarch_info::READ_LOG_LOAD, rd); 
      uarch_activity.queue_event(iwcd_uarch_info::SMEM_ACCESS, bf_hashes); 
   }

   abort_mask = resolve_intra_warp_cd(inst, abort_mask); 

   collect_intra_warpcd_uarch_activity_stats(uarch_activity); 

   return abort_mask; 
}

// each thread in the warp do a validation (without timing/traffic) and abort if fail
void tx_log_walker::pre_commit_validation(warp_inst_t &inst) 
{
   // TODO: implement warp level check for logical tm
   active_mask_t conflict_free_active_mask = inst.get_active_mask(); 

   // temporal conflict detection does not generate traffic at commit 
   bool use_temporal_cd = m_core_config->tlw_use_temporal_cd; 

   unsigned warp_id = inst.warp_id(); 
  
   bool warp_level_pass = true; 
   bool warp_level_read_only = true; 
   if (inst.is_warp_level) {
      if (m_core_config->tlw_use_logical_temporal_cd) {
          unsigned index = m_core_id * (m_core_config->max_warps_per_shader) + warp_id;
	  warp_level_pass = !(logical_temporal_conflict_detector::get_singleton().warp_level_conflict_exist(index));
	  for (unsigned t = 0; t < inst.warp_size(); t++) {
             int t_tid = warp_id * m_warp_size + t; 
             tm_manager_inf *t_tm_manager = m_core->get_func_thread_info(t_tid)->get_tm_manager();
	     if (t_tm_manager && (t_tm_manager->get_n_write() > 0 || t_tm_manager->get_is_abort_need_clean())) {
	         warp_level_read_only = false;
	     } 
	  }
      } else {
          for (unsigned t = 0; t < inst.warp_size(); t++) {
             if (inst.active(t) == false) continue; 
             int t_tid = warp_id * m_warp_size + t; 
             tm_manager_inf *t_tm_manager = m_core->get_func_thread_info(t_tid)->get_tm_manager(); 

             if (t_tm_manager->validate_all(use_temporal_cd) == false) {
                warp_level_pass = false; 
             }

             if (t_tm_manager->get_n_write() > 0) {
                warp_level_read_only = false; 
             }
          }
      }
   }

   for (unsigned t = 0; t < inst.warp_size(); t++) {
      if (inst.active(t) == false) continue; 
      int t_tid = warp_id * m_warp_size + t;
      tm_manager_inf *t_tm_manager = m_core->get_func_thread_info(t_tid)->get_tm_manager();
      
      bool thread_level_pass = t_tm_manager->validate_all(use_temporal_cd); 
      bool thread_level_read_only = (t_tm_manager->get_n_write() == 0); 
      bool tx_pass = (inst.is_warp_level)? warp_level_pass : thread_level_pass; 
      bool tx_read_only = (inst.is_warp_level)? warp_level_read_only : thread_level_read_only; 

      if (tx_pass == false) {
         if (m_core_config->tlw_tcd_whitelist_only == false) {
            t_tm_manager->abort(); 
            conflict_free_active_mask.reset(t); 
            // clear the mask for the aborted transactions
            inst.set_not_active(t); 
            // clean up thread state bookkeeping // TODO: put this up in a shader_core_ctx member function
            m_core->get_thread_ctx(t_tid)->m_in_pipeline -= 1;
            if(m_core->get_thread_ctx(t_tid)->m_in_pipeline < 0) {
               printf("Error: sid=%d wid=%d tid=%d \n", m_core->get_sid(), inst.warp_id(), t_tid);
            }
            assert(m_core->get_thread_ctx(t_tid)->m_in_pipeline >= 0);
            m_stats.n_pre_commit_validation_abort += 1;
	    if (t_tm_manager->get_is_abort_need_clean() == false)
		inst.set_not_issued(t); 
         }
      } else if (tx_read_only) {
         // read only transaction that passed validation
         // - defer whitelisting transaction for warp level (not all threads in a warp is read-only)
         t_tm_manager->commit_core_side(); 
         conflict_free_active_mask.reset(t); 
         // clear the mask for the committed read-only transactions
         inst.set_not_active(t);
	 inst.set_not_issued(t); 
         delete_ptx_thread_tm_manager(warp_id, t);
         // clean up thread state bookkeeping // TODO: put this up in a shader_core_ctx member function
         m_core->get_thread_ctx(t_tid)->m_in_pipeline -= 1;
         if(m_core->get_thread_ctx(t_tid)->m_in_pipeline < 0) {
            printf("Error: sid=%d wid=%d tid=%d \n", m_core->get_sid(), inst.warp_id(), t_tid);
         }
         assert(m_core->get_thread_ctx(t_tid)->m_in_pipeline >= 0);
         m_stats.n_pre_commit_validation_pass += 1; 
      }
   }
   assert(inst.get_active_mask() == conflict_free_active_mask); 
}

// undirty every cache line that contains the log 
void tx_log_walker::clear_log_cache_usage(unsigned warp_id, const tm_warp_info &warp_info, bool read_log) 
{
   const unsigned addr_size = 4; 
   const unsigned word_size = 4; 
   unsigned wtid = warp_id * m_core->get_config()->warp_size; 
   addr_t log_offset = (read_log)? tm_warp_info::read_log_offset : tm_warp_info::write_log_offset; 
   unsigned num_entries = (read_log)? warp_info.m_read_log.size() : warp_info.m_write_log.size(); 

   for (unsigned entry_id = 0; entry_id < num_entries; entry_id++) {
      // clear cache line storing entry's address field
      addr_t atag_block = m_core->translate_local_memaddr(entry_id * 2 * word_size + log_offset, wtid, addr_size); 
      m_L1D->mark_last_use(atag_block, false); 

      // clear cache line storing entry's data field
      addr_t data_block = m_core->translate_local_memaddr((entry_id * 2 + 1) * word_size + log_offset, wtid, word_size); 
      m_L1D->mark_last_use(data_block, false); 
   }
}

bool tx_log_walker::need_do_commit(warp_inst_t &inst) 
{
    if (m_core_config->tlw_use_logical_temporal_cd == false and m_core_config->timing_mode_vb_commit == true)
	assert(inst.get_active_mask() == inst.get_issued_mask());
    if (m_core_config->tlw_use_logical_temporal_cd)
        return (inst.active_count() > 0 || inst.issued_count() > 0);
    else 
        return inst.active_count() > 0;
}

bool tx_log_walker::process_commit(warp_inst_t &inst, 
                                   mem_stage_stall_type &stall_reason, 
                                   mem_stage_access_type &access_type)
{
   // skip if this is not commit 
   if (inst.empty() || inst.is_tcommit == false) return true; 

   if (need_do_commit(inst) == false || m_core_config->skip_tx_log_walker || is_nested_tx_commit(inst)) {
      // should not happen when timing mode is completely implemented 
      m_finish_commit_q.push_front(inst); // need to do this to release the commit lock 
      return true; 
   }
   
   m_stats.n_warp_commit_attempt += 1; 

   unsigned wid = inst.warp_id(); 

   m_stats.m_warp_read_log_size.add2bin(m_warp[wid].get_tm_warp_info().m_read_log_size); 
   m_stats.m_warp_write_log_size.add2bin(m_warp[wid].get_tm_warp_info().m_write_log_size); 

   // pre commit validation for limit study 
   if (m_core_config->tlw_pre_commit_validation) {
      bool read_only_tx = (m_warp[wid].get_tm_warp_info().m_write_log_size == 0);
      pre_commit_validation(inst); 
     
      if (g_tm_options.m_eager_warptm_enabled) {
	  assert(g_tm_options.m_value_based_eager_cr);
          for(unsigned t = 0; t < m_warp_size; t++) {
              // ideally do the revalidation, in case some read value was overwritten after value based checking
	      if (inst.issued(t)) {
                  unsigned tid=m_warp_size*inst.warp_id()+t;
	          tm_manager_inf *t_tm_manager = m_core->get_func_thread_info(tid)->get_tm_manager();
		  assert(t_tm_manager != NULL); 
                  if (t_tm_manager->validate_all(false) == false) {
		      t_tm_manager->abort(); 
                      inst.set_not_active(t); 
                      // clean up thread state bookkeeping // TODO: put this up in a shader_core_ctx member function
                      m_core->get_thread_ctx(tid)->m_in_pipeline -= 1;
                      if(m_core->get_thread_ctx(tid)->m_in_pipeline < 0) {
                         printf("Error: sid=%d wid=%d tid=%d \n", m_core->get_sid(), inst.warp_id(), tid);
                      }
                      assert(m_core->get_thread_ctx(tid)->m_in_pipeline >= 0);
	              if (t_tm_manager->get_is_abort_need_clean() == false)
		          inst.set_not_issued(t); 
		  } 
	      }
	  }
      } 

      if (need_do_commit(inst) == false) {
         m_finish_commit_q.push_front(inst); // need to do this to release the commit lock 
         clear_log_cache_usage(wid, m_warp[wid].get_tm_warp_info(), true); 
         clear_log_cache_usage(wid, m_warp[wid].get_tm_warp_info(), false); 
	 if (m_core->has_paused_threads(wid)) {
	     assert(g_tm_options.m_pause_and_go_enabled);
	 } else {
             m_warp[wid].get_tm_warp_info().reset(); 
	 }
         m_core->commit_warp_cleanup(wid); 
         return true; 
      }
      if (read_only_tx) {
         if (m_core_config->tlw_tcd_whitelist_only == false) {  // Is this right? I think if you whitelist readonly TX, readonly TX should be commited at core side 
            m_stats.n_warp_commit_read_only += 1; 
            for (unsigned t = 0; t < inst.warp_size(); t++) {
               if (inst.active(t) == false) continue; 
               int t_tid = wid * m_warp_size + t; 
               tm_manager_inf *t_tm_manager = m_core->get_func_thread_info(t_tid)->get_tm_manager(); 
               t_tm_manager->commit_core_side(); 
               delete_ptx_thread_tm_manager(wid, t);
               m_stats.n_pre_commit_validation_pass += 1; 
            }
            clear_log_cache_usage(wid, m_warp[wid].get_tm_warp_info(), true); 
            m_finish_commit_q.push_front(inst); // need to do this to release the commit lock 
	    if (m_core->has_paused_threads(wid)) {
	        assert(g_tm_options.m_pause_and_go_enabled);
	    } else {
               m_warp[wid].get_tm_warp_info().reset(); 
	    } 
            m_core->commit_warp_cleanup(wid); 
            return true; 
         }
      }
   }

   warp_commit_tx_t &cmt_warp = m_committing_warp[wid]; 

   if (cmt_warp.active() == false) {
      if (m_core_config->tlw_intra_warp_conflict_detection) {
         cmt_warp.m_iwcd_uarch_info.reset(); 
         intra_warp_conflict_detection(inst, cmt_warp.m_iwcd_uarch_info); 
         if (m_core_config->tlw_timing_model == 0) {
            cmt_warp.m_iwcd_uarch_info.reset(); 
         }
      }

      cmt_warp.init(inst); // copy the instruction into the structure and initialize 
      if (g_tm_options.m_eager_warptm_enabled) {
          for (unsigned lane = 0; lane < m_warp_size; lane++) {
	      if (inst.issued(lane)) {
                  int tid = wid * m_warp_size + lane; 
                  tm_manager_inf *thread_tm_manager = m_core->get_func_thread_info(tid)->get_tm_manager();
	          assert(thread_tm_manager); 
                  thread_tm_manager->validate_or_crash();
                  thread_tm_manager->commit_core_side();
	          delete_ptx_thread_tm_manager(wid, lane);
	      }
	  }
      }

      if (m_core_config->timing_mode_vb_commit) {
         // pass in the tm manager 
         for (unsigned lane = 0; lane < m_warp_size; lane++) {
            if (inst.issued(lane)) {
               assign_tm_manager(wid, lane); 
            } 
         }
      }

      m_bg_commit_warps.push_back(wid);
   } else {
      assert(cmt_warp.has(inst)); 
   }

   stall_reason = NO_RC_FAIL;
   return true; 
}

unsigned tx_log_walker::warp_commit_tx_t::s_next_commit_id = 1; // reserve 0?
unsigned int tx_log_walker::warp_commit_tx_t::alloc_commit_id() { return s_next_commit_id++; }

// set the active threads state to ACQ_CID
void tx_log_walker::warp_commit_tx_t::init(const warp_inst_t &inst) 
{
   assert(m_active == false); 
   m_active = true;
   m_need_logical_tm_restart = false; 

   m_inst = inst; // make copy of the commit instruction (for unlocking scoreboard)
   m_intra_warp_cd_cycle_pending = m_iwcd_uarch_info.operation_cycles(); // intra warp cd timing 

   m_thread_state.resize(inst.warp_size()); 
   for (unsigned t = 0; t < m_thread_state.size(); t++) {
      m_thread_state[t].reset(); 
      if (inst.issued(t)) {
         // initialize state: acquiring commit id (not getting it yet)
         if (m_intra_warp_cd_cycle_pending == 0) {
            m_thread_state[t].m_state = ACQ_CID; 
         } else {
            m_thread_state[t].m_state = INTRA_WARP_CD; 
         }
         // simplified: pick one thread and assign commit id to it magically 
         if (m_thread_processing == -1) {
            m_thread_processing = t; 
         }
      }
   }
}

int tx_log_walker::warp_commit_tx_t::perform_intra_warp_cd_cycle() 
{
   m_intra_warp_cd_cycle_pending--; 
   assert(m_intra_warp_cd_cycle_pending >= 0); 
   return m_intra_warp_cd_cycle_pending; 
}

void tx_log_walker::warp_commit_tx_t::signal_stalled() 
{
   m_stalled = true; 
}

bool tx_log_walker::warp_commit_tx_t::has(const warp_inst_t &inst) 
{
   if (m_active) {
      return (m_inst.warp_id() == inst.warp_id());
   } 
   return false; 
}

// advance thread pointer to next active thread 
void tx_log_walker::warp_commit_tx_t::next_thread()
{
   for (unsigned t = 0; t < m_inst.warp_size(); t++) {
      m_thread_processing += 1; 
      m_thread_processing &= m_inst.warp_size() - 1; 
      // if (m_inst.active(m_thread_processing) == true) break; 
      if (m_thread_state[m_thread_processing].m_state != IDLE) break; 
   }
}

// advance thread pointer to next non-waiting active thread 
void tx_log_walker::warp_commit_tx_t::next_thread_to_non_waiting()
{
   for (unsigned t = 0; t < m_inst.warp_size(); t++) {
      m_thread_processing += 1; 
      m_thread_processing &= m_inst.warp_size() - 1; 
      if (m_thread_state[m_thread_processing].m_state != IDLE and 
          m_thread_state[m_thread_processing].m_state != WAIT_CU_REPLY and 
          m_thread_state[m_thread_processing].m_state != SEND_ACK_CLEANUP 
          ) break; 
   }
}

bool tx_log_walker::warp_commit_tx_t::all_sent()
{
   for (unsigned t = 0; t < m_inst.warp_size(); t++) {
      if (m_thread_state[t].in_sending_state())
         return false; 
   }
   return true; 
}

bool tx_log_walker::warp_commit_tx_t::all_commit()
{
   for (unsigned t = 0; t < m_inst.warp_size(); t++) {
      if (m_thread_state[t].m_state != IDLE)
         return false; 
      // there should be no commit ack pending for transactions that are done
      assert(m_thread_state[t].m_cu_commit_pending.none()); 
   }
   return true; 
}

bool tx_log_walker::warp_commit_tx_t::all_replied()
{
   // if there is any pending reply, then return false 
   for (unsigned t = 0; t < m_inst.warp_size(); t++) {
      if (m_thread_state[t].m_pending_cu_reply.any())
         return false; 
   }
   return true; 
}

int tx_log_walker::cid2tid(int commit_id) const
{
   commit_id_thread_lookup::const_iterator iTid; 
   iTid = m_cid2tid.find(commit_id); 
   if (iTid != m_cid2tid.end()) {
      return iTid->second; 
   } else {
      return -1; 
   }
}


tx_log_walker::warp_commit_tx_t* tx_log_walker::get_warp_ctp(int wid)
{
   return &(m_committing_warp[wid]); 
}

tx_log_walker::commit_tx_t* tx_log_walker::get_ctp(int tid)
{
   int wid = tid / m_warp_size; 
   warp_commit_tx_t &wctp = m_committing_warp[wid];

   int land_id = tid % m_warp_size; 
   return &(wctp.m_thread_state[land_id]); 
}


tx_log_walker_warpc::tx_log_walker_warpc(shader_core_ctx *core, 
                                         unsigned core_id, 
                                         unsigned cluster_id, 
                                         data_cache *L1D, 
                                         mem_fetch_interface *&icnt, 
                                         mem_fetch_allocator *mf_alloc,
                                         const shader_core_config *core_config, 
                                         const memory_config *memory_config, 
                                         shader_core_stats &core_stats, 
                                         tx_log_walker_stats &stats)
: tx_log_walker(core, core_id, cluster_id, L1D, icnt, mf_alloc, core_config, memory_config, core_stats, stats)
{
   // assert(m_core_config->tlw_fast_log_read == true); 
   assert(m_core_config->cu_finite == false); 
}

tx_log_walker_warpc::~tx_log_walker_warpc()
{
}

void tx_log_walker_warpc::done_send_rs(warp_commit_tx_t &cmt_warp) 
{
   // transition whole warp to SEND_WS
   for (unsigned t = 0; t < m_warp_size; t++) {
      commit_tx_t &ct = cmt_warp.m_thread_state[t];
      if (ct.m_state == IDLE) continue; 
      assert(ct.read_log_sent()); // make sure this is true across the whole warp 
      assert(ct.m_state == SEND_RS); 
      ct.m_state = SEND_WS;
      if (m_timelinef and tlw_watched(ct.m_commit_id)) 
         fprintf(m_timelinef, "tx_cid = %d, SEND_RS->SEND_WS @ %llu \n", 
                 ct.m_commit_id, gpu_sim_cycle + gpu_tot_sim_cycle); 
   }
}

void tx_log_walker_warpc::done_send_ws(warp_commit_tx_t &cmt_warp)
{
   // transition whole warp to WAIT_CU_REPLY
   for (unsigned t = 0; t < m_warp_size; t++) {
      commit_tx_t &ct = cmt_warp.m_thread_state[t];
      if (ct.m_state == IDLE) continue; 
      assert(ct.write_log_sent()); // make sure this is true across the whole warp 
      assert(ct.m_state == SEND_WS);
      // send DONE_FILL message to commit unit
      generate_done_fill_coalesced(cmt_warp.m_inst, ct);
      if (g_tm_options.m_eager_warptm_enabled) {
          ct.enter_send_ack_cleanup(m_memory_config->m_n_mem_sub_partition);
      } else {
          ct.enter_write_cu_reply(m_memory_config->m_n_mem_sub_partition); 
      }
   }

   if (g_tm_options.m_eager_warptm_enabled) {
       cmt_warp.next_thread();
   }

   send_coalesced_packet(); 
}

// simulate a cycle of intra warp conflict detection algorithm 
// return true to indicate yield to next warp
bool tx_log_walker_warpc::intra_warp_cd_cycle(int wid, warp_commit_tx_t &cmt_warp)
{
   if (cmt_warp.perform_intra_warp_cd_cycle() == 0) {
      for (unsigned t = 0; t < m_warp_size; t++) {
         commit_tx_t &ct = cmt_warp.m_thread_state[t];
         assert(ct.m_state == IDLE or ct.m_state == INTRA_WARP_CD); 
         if (ct.m_state == INTRA_WARP_CD) {
            ct.m_state = ACQ_CID; 
         }
      }
   }
   return false; 
}

void tx_log_walker_warpc::access_log(iwcd_uarch_info::uarch_event & uevent, int warp_id, warp_commit_tx_t &cmt_warp) 
{
   if (m_core_config->tlw_fast_log_read) {
       uevent.m_started = true; 
       uevent.m_done = true; 
   }

   const unsigned addr_size = 4; 
   const unsigned word_size = 4; 
   const active_mask_t empty_mask;
   mem_access_byte_mask_t full_byte_mask; 
   full_byte_mask.set(); 
   unsigned atag_block_size = addr_size * m_warp_size; 
   unsigned wtid = warp_id * m_core->get_config()->warp_size; 
   unsigned entry_id = uevent.m_metadata; 
   assert(uevent.m_type == iwcd_uarch_info::READ_LOG_LOAD || uevent.m_type == iwcd_uarch_info::WRITE_LOG_LOAD); 
   bool read_log_acc = (uevent.m_type == iwcd_uarch_info::READ_LOG_LOAD); 
   addr_t log_offset = (read_log_acc)? tm_warp_info::read_log_offset : tm_warp_info::write_log_offset; 

   // generate access to entry's address field
   addr_t atag_block = m_core->translate_local_memaddr(entry_id * 2 * word_size + log_offset, wtid, addr_size); 
   mem_access_t atag_access(LOCAL_ACC_R, atag_block, atag_block_size, false, empty_mask, full_byte_mask);
   mem_fetch *atag_mf = m_mf_alloc->alloc(cmt_warp.m_inst, atag_access);

   std::list<cache_event> events;
   enum cache_request_status status = m_L1D->access(atag_mf->get_addr(),atag_mf,gpu_sim_cycle+gpu_tot_sim_cycle,events);
   m_stats.n_atag_read++; 

   bool write_sent = was_write_sent(events);
   bool read_sent = was_read_sent(events);
   if( write_sent ) 
       m_core->inc_store_req( warp_id ); // eviction 
   if ( status == HIT ) {
       assert( !read_sent );
       assert( !write_sent );
       delete atag_mf; 
       uevent.m_started = true; 
       uevent.m_done = true; 
   } else if ( status == RESERVATION_FAIL ) {
       assert( !read_sent );
       assert( !write_sent );
       delete atag_mf;
       m_stats.n_atag_cachercf++; 
       // not started, try again next cycle 
   } else {
       enum log_acc_type_t log_type = (read_log_acc)? READ_LOG_ACC : WRITE_LOG_ACC; 
       assert( status == MISS || status == HIT_RESERVED );
       m_extra_mf_fields[atag_mf] = extra_mf_fields(warp_id, wtid, log_type, ATAG_READ); 
       m_stats.n_atag_cachemiss++; 
       uevent.m_started = true; 
       cmt_warp.signal_stalled(); 
   }
   if (m_timelinef) 
       fprintf(m_timelinef, "Log-read: warp%u RD %s-atag[%u] from [0x%08x], set=%u, outcome=%d @ %llu\n", 
               warp_id, ((read_log_acc)? "RS":"WS"), entry_id, atag_block, 
               m_core->get_config()->m_L1D_config.set_index(atag_block), status, 
               gpu_sim_cycle + gpu_tot_sim_cycle); 
}

bool tx_log_walker_warpc::intra_warp_cd_detail_cycle(int wid, warp_commit_tx_t &cmt_warp)
{
   iwcd_uarch_info &uarch_info = cmt_warp.m_iwcd_uarch_info; 

   // perform the next operation in queue 
   if (uarch_info.event_queue_empty() == false) {
      assert(m_intra_warp_cd_warps.count(wid) != 0);  // should have allocated resource 
      iwcd_uarch_info::uarch_event & uevent = uarch_info.next_event_in_queue(); 
      if (uevent.m_type == iwcd_uarch_info::SMEM_ACCESS) { 
         assert(uevent.m_metadata > 0); 
         uevent.m_metadata -= 1; 
         m_core_stats.gpgpu_n_shmem_bank_access[m_core_id]++;
         if (uevent.m_metadata == 0) {
            uevent.m_done = true; 
         }
         uevent.m_started = true; 
      } else {
         // try to access load if the cache was stalled or this is the first cycle for event 
         if (uevent.m_started == false) {
            access_log(uevent, wid, cmt_warp); 
         }
      }
      if (uevent.m_done) {
         uarch_info.next_event_done(); 
      }
   }

   // if all operation done, move to next state 
   if (uarch_info.event_queue_empty()) {
      for (unsigned t = 0; t < m_warp_size; t++) {
         commit_tx_t &ct = cmt_warp.m_thread_state[t];
         assert(ct.m_state == IDLE or ct.m_state == INTRA_WARP_CD); 
         if (ct.m_state == INTRA_WARP_CD) {
            ct.m_state = ACQ_CID; 
         }
      }
      m_intra_warp_cd_warps.erase(wid); 
   }
   return false; 
}

// allocate intra warp conflict detection resource (scratchpad memory for ownership table/bloom filters) 
void tx_log_walker_warpc::alloc_intra_warp_cd_resource() 
{
   // check for available resource 
   const size_t intra_warp_cd_limit = m_core_config->tlw_intra_warp_cd_concurrency; 
   if (m_intra_warp_cd_warps.size() >= intra_warp_cd_limit) return; 

   for (auto i_wid = m_bg_commit_warps.begin(); i_wid != m_bg_commit_warps.end(); ++i_wid) {
      int wid = *i_wid; 
      if (m_intra_warp_cd_warps.count(wid) != 0) continue; // already using resource 

      warp_commit_tx_t &cmt_warp = m_committing_warp[wid]; 
      assert(cmt_warp.active()); 

      // assign resource to warp 
      if (cmt_warp.m_iwcd_uarch_info.event_queue_empty() == false) {
         m_intra_warp_cd_warps.insert(wid); 
         if (m_intra_warp_cd_warps.size() >= intra_warp_cd_limit) 
            break; 
      }
   }
}

// return the highest priority warp that is ready for action 
int tx_log_walker_warpc::select_active_warp() 
{
   for (auto i_wid = m_bg_commit_warps.begin(); i_wid != m_bg_commit_warps.end(); ++i_wid) {
      int wid = *i_wid; 
      warp_commit_tx_t &cmt_warp = m_committing_warp[wid]; 
      assert(cmt_warp.active()); 

      if (cmt_warp.all_commit()) {
	  assert(g_tm_options.m_logical_timestamp_dynamic_concurrency_enabled);
          assert(cmt_warp.need_logical_tm_restart());
	  if (m_core->is_masked_tm_token(wid) == false) {
	      return wid;
	  } else {
	      continue;
	  }
      }

      // skip stalled warps 
      if (cmt_warp.stalled()) continue; 

      // skip warps waiting for smem 
      if (cmt_warp.m_iwcd_uarch_info.event_queue_empty() == false) {
         if (m_intra_warp_cd_warps.count(wid) == 0) continue; 
      }

      return wid; 
   }
   return m_bg_commit_warps.front(); 
}

void tx_log_walker_warpc::cycle()
{
   out_message_queue_cycle();

   if (m_bg_commit_warps.empty()) return; // no warp committing in background 

   alloc_intra_warp_cd_resource(); 
   int wid = select_active_warp(); 
   bool cache_accessed = false;
   unsigned cu_accessed = -1;
   bool yield_next_warp = false;

   warp_commit_tx_t &cmt_warp = m_committing_warp[wid]; 
   assert(cmt_warp.active()); 
   commit_tx_t &tp = cmt_warp.m_thread_state[cmt_warp.m_thread_processing]; 
   
   switch (tp.m_state) {
   case INTRA_WARP_CD: 
      if (m_core_config->tlw_timing_model == 1) {
         yield_next_warp = intra_warp_cd_cycle(wid, cmt_warp); 
      } else if (m_core_config->tlw_timing_model == 2) {
         yield_next_warp = intra_warp_cd_detail_cycle(wid, cmt_warp); 
      }
      break; 
   case ACQ_CID: 
      // acquire commit ID for all active threads/transactions in this warp in one cycle 
      for (unsigned t = 0; t < m_warp_size; t++) {
         commit_tx_t &ct = cmt_warp.m_thread_state[t];
         assert(ct.m_state == IDLE or ct.m_state == ACQ_CID); 
         if (ct.m_state == ACQ_CID) {
            acquire_cid(wid, wid * m_warp_size + t, ct); 
         }
      }
      break;
   case ACQ_CU_ENTRIES:
   case WAIT_CU_ALLOC_REPLY:
      assert(0); 
      break;
   case SEND_RS: {
      // generate access to L1D and send read-log out to commit unit 
      if (tp.read_log_sent()) {
         printf("[TX_LOG_WALKER]: Empty Read-Set Transaction: CommitID = %d core%d warp%d\n", 
                tp.m_commit_id, m_core_id, wid); 
         // transition whole warp to SEND_WS
         done_send_rs(cmt_warp); 
      } else {
         bool warp_rentry_sent = false; 
         for (unsigned t = 0; t < m_warp_size; t++) {
            commit_tx_t &ct = cmt_warp.m_thread_state[t];
            if (ct.m_state == IDLE) continue; 
            assert(ct.m_state == SEND_RS); 
	    cu_accessed = (unsigned)-1;
            bool rentry_sent = send_log_entry(cmt_warp.m_inst, t, 
                                              ct.m_commit_id, ct.read_log_front(), cache_accessed, cu_accessed);
            if (rentry_sent) {
               warp_rentry_sent = true; 
               if (m_timelinef and tlw_watched(ct.m_commit_id)) 
                  fprintf(m_timelinef, "tx_cid = %d, SEND_RS @ %llu \n", 
                          ct.m_commit_id, gpu_sim_cycle + gpu_tot_sim_cycle); 
               if(cu_accessed != (unsigned) -1)
                   ct.m_sent_cu_entry.set(cu_accessed);
               ct.next_read_log_send(); 
               // inform the rest of the threads in this warp to not read the cache again 
               if (&ct == &tp) {
                  for (unsigned s = 0; s < m_warp_size; s++) {
                     commit_tx_t &cs = cmt_warp.m_thread_state[s];
                     if (&cs == &tp or cs.m_state == IDLE) continue; 
                     cs.read_log_front().atag_read = true; 
                     cs.read_log_front().data_read = true; 
                  }
               }
            } else {
               break; // only generate one cache miss is enough to fetch the whole line 
            }
         }
         if (warp_rentry_sent) {
            send_coalesced_packet(); 
            if (tp.read_log_sent()) {
               // transition whole warp to SEND_WS
               done_send_rs(cmt_warp); 
            }
         } else {
            if (tp.read_log_front().atag_cachemiss || tp.read_log_front().data_cachemiss) {
               yield_next_warp = true; 
            }  
         }
      }
      } break; 
   case SEND_WS: {
      // generate access to L1D and send write-log out to commit unit 
      if (tp.write_log_sent()) {
         // empty write log -- this is possible  
         // transition whole warp to WAIT_CU_REPLY
         done_send_ws(cmt_warp); 
      } else {
         bool warp_wentry_sent = false; 
         for (unsigned t = 0; t < m_warp_size; t++) {
            commit_tx_t &ct = cmt_warp.m_thread_state[t];
            if (ct.m_state == IDLE) continue; 
	    cu_accessed = (unsigned)-1;
            bool wentry_sent = send_log_entry(cmt_warp.m_inst, t, 
                                              ct.m_commit_id, ct.write_log_front(), cache_accessed, cu_accessed);
            if (wentry_sent) {
               warp_wentry_sent = true; 
               if (m_timelinef and tlw_watched(ct.m_commit_id)) 
                  fprintf(m_timelinef, "tx_cid = %d, SEND_WS @ %llu \n", 
                          ct.m_commit_id, gpu_sim_cycle + gpu_tot_sim_cycle); 
               if(cu_accessed != (unsigned) -1) {
                    ct.m_sent_cu_entry.set(cu_accessed);
               }
               ct.next_write_log_send(); 
               // inform the rest of the threads in this warp to not read the cache again 
               if (&ct == &tp) {
                  for (unsigned s = 0; s < m_warp_size; s++) {
                     commit_tx_t &cs = cmt_warp.m_thread_state[s];
                     if (&cs == &tp or cs.m_state == IDLE) continue; 
                     cs.write_log_front().atag_read = true; 
                     cs.write_log_front().data_read = true; 
                  }
               }
            } else {
               break; // only generate one cache miss is enough to fetch the whole line 
            }
         }
         if (warp_wentry_sent) {
            send_coalesced_packet(); 
            if (tp.write_log_sent()) {
               // transition whole warp to WAIT_CU_REPLY
               done_send_ws(cmt_warp); 
            } 
         } else {
            if (tp.write_log_front().atag_cachemiss || tp.write_log_front().data_cachemiss) {
               yield_next_warp = true; 
            }  
         } 
      }
      } break; 
   case WAIT_CU_REPLY: 
      // tp.m_pass = true; 
      // tp.m_state = SEND_ACK_CLEANUP;
      if (m_core_config->tlw_intra_warp_conflict_detection == false) {
         assert(!tp.m_pending_cu_reply.none());
      }
      cmt_warp.next_thread();
      break; 
   case SEND_ACK_CLEANUP: 
      if (tp.m_cu_commit_pending.any() or 
          (cmt_warp.m_retry_delay != 0 and gpu_sim_cycle + gpu_tot_sim_cycle <= cmt_warp.m_retry_delay)) {
         if (cmt_warp.all_sent()) {
            yield_next_warp = true; 
         } else {
            cmt_warp.next_thread_to_non_waiting();
         }
      } else {
         tp.m_state = IDLE; 
         s_n_commited += 1;
         cmt_warp.next_thread();
      }
      break; 
   default:
      assert(0); 
   }; 

   if (cmt_warp.all_commit()) {
      // finish commit push the commit instruction to finish queue to unlock scoreboard at writeback stage 
      m_finish_commit_q.push_front(m_committing_warp[wid].m_inst); 
      m_bg_commit_warps.remove(wid); 
      m_committing_warp[wid].reset(); 
      if (m_core->has_paused_threads(wid)) {
          assert(g_tm_options.m_pause_and_go_enabled); 
      } else {
          m_warp[wid].get_tm_warp_info().reset();
      }

      m_core->commit_warp_cleanup(wid); 

   } else if (yield_next_warp or cmt_warp.all_sent()) {
      // all request from this warp is sent yield to other warps to send logs  
      m_bg_commit_warps.remove(wid); 
      m_bg_commit_warps.push_back(wid); 
   }
}

void tx_log_walker_warpc::generate_done_fill_coalesced(const warp_inst_t &inst, commit_tx_t &tp)
{
   for (unsigned cu = 0; cu < m_memory_config->m_n_mem_sub_partition; cu++) {
      mem_fetch *donefill_mf = NULL;
      if (tp.m_sent_cu_entry.test(cu)) {
         donefill_mf = create_tx_packet(TX_DONE_FILL, inst, tp.m_commit_id, cu, 0xD07EFEEF, 0, false);
      } else {
         donefill_mf = create_tx_packet(TX_SKIP, inst, tp.m_commit_id, cu, 0xD07EFEEF, 0, false);
      }
      assert(donefill_mf != NULL);
      donefill_mf->set_commit_pending_ptr(&(tp.m_cu_commit_pending)); 
      m_coalescing_queue[cu].push_back(donefill_mf); 
   }
   if (m_timelinef and tlw_watched(tp.m_commit_id)) 
      fprintf(m_timelinef, "tx_cid = %d, SEND_WS->DONE_FILL @ %llu \n", 
              tp.m_commit_id, gpu_sim_cycle + gpu_tot_sim_cycle); 
}

void tx_log_walker_warpc::send_coalesced_packet()
{
   #if 0
   unsigned n_packet_transfer = 0;
   for (size_t m = 0; m < m_coalescing_queue.size(); m++) {
      // TODO: coalesce these packets to the same memory partition into a single packet 
      while (not m_coalescing_queue[m].empty()) {
         mem_fetch* wrset_mf = m_coalescing_queue[m].front(); 
         m_coalescing_queue[m].pop_front(); 
         m_out_message_queue.push_back(wrset_mf);
         n_packet_transfer++;
      }
   }
   #else
   unsigned n_packet_transfer = 0;
   for (size_t m = 0; m < m_coalescing_queue.size(); m++) {
      // coalesce these packets to the same memory partition into a single packet 
      if (not m_coalescing_queue[m].empty()) {
         mem_fetch* wrset_mf = m_coalescing_queue[m].front(); 

         enum mf_type packet_type = wrset_mf->get_type(); 
         if (packet_type == TX_SKIP) packet_type = TX_DONE_FILL;
         if (packet_type == TX_FAIL) packet_type = TX_PASS;
	 unsigned packet_size = 0;
	 for (auto iter = m_coalescing_queue[m].begin(); iter != m_coalescing_queue[m].end(); iter++) {
             packet_size += wrset_mf->get_data_size();
	 }
         m_stats.m_coalesced_packet_size.add2bin(m_coalescing_queue[m].size()); 
         unsigned commit_id = wrset_mf->get_transaction_id(); 

         mem_fetch *coalesced_mf = create_tx_packet( packet_type, wrset_mf->get_inst(), commit_id, m, 0xD07EFEEF, packet_size, false);

         while (not m_coalescing_queue[m].empty()) {
            mem_fetch* mf = m_coalescing_queue[m].front();
            if (packet_type == TX_DONE_FILL) {
               assert(mf->get_type() == TX_DONE_FILL or mf->get_type() == TX_SKIP); 
            } else if (packet_type == TX_PASS) {
               assert(mf->get_type() == TX_PASS or mf->get_type() == TX_FAIL); 
            } else {
               assert(mf->get_type() == packet_type); 
            }
            coalesced_mf->append_coalesced_packet(mf); 
            m_coalescing_queue[m].pop_front(); 
         }

         m_out_message_queue.push_back(coalesced_mf);
         n_packet_transfer++;
      }
   }
   #endif
}

// send TX_PASS or TX_FAIL message according to outcome of transaction for given warp 
// also call tm_manager commit to update local_memory data 
void tx_log_walker_warpc::send_tx_pass_fail(warp_commit_tx_t &cmt_warp, int warp_id, int thread_id)
{
   if (m_core_config->tlw_intra_warp_conflict_detection == false) {
      tx_log_walker::send_tx_pass_fail(cmt_warp, warp_id, thread_id); 
      return; 
   }

   if (not cmt_warp.all_replied()) return; // defer rely generation until the whole warp was validated 

   // warp level transaction - whole warp has to pass unanimously 
   if (cmt_warp.m_inst.is_warp_level) {
      bool whole_warp_pass = true; 
      for (unsigned lane = 0; lane < m_warp_size; lane++) {
         commit_tx_t &tp = cmt_warp.m_thread_state[lane];
         if (tp.m_commit_id == -1) continue; // ignore inactive lane
         if (tp.m_pass == false) {
            whole_warp_pass = false; 
         }
      }
      // set each thread's outcome based on the overall outcome  
      for (unsigned lane = 0; lane < m_warp_size; lane++) {
         commit_tx_t &tp = cmt_warp.m_thread_state[lane];
         if (tp.m_commit_id == -1) continue; // ignore inactive lane
         tp.m_pass = whole_warp_pass; 
      } 
   }

   // send TX_PASS or TX_FAIL message to all involved commit units 
   for (unsigned cu = 0; cu < m_memory_config->m_n_mem_sub_partition; cu++) {
      for (unsigned lane = 0; lane < m_warp_size; lane++) {
         commit_tx_t &tp = cmt_warp.m_thread_state[lane];
         if (tp.m_commit_id == -1) continue; // ignore inactive lane

         tp.m_state = SEND_ACK_CLEANUP;
         if(tp.m_sent_cu_entry.test(cu)) {
            enum mf_type ack_type = (tp.m_pass)? TX_PASS : TX_FAIL;
            mem_fetch *tx_ack_mf = create_tx_packet(ack_type, cmt_warp.m_inst, tp.m_commit_id, cu, 0x600DACC, 0, false);
            m_coalescing_queue[cu].push_back(tx_ack_mf); // WF: send packet to coalescing queue for packaging 
         }
      }
   }
   send_coalesced_packet(); 

   // call tm_manager commit here -- before reply arrives at CU
   // this ensures that the following sequence of events is correct:
   //  1. CU receive reply
   //  2. Commit updates sent for this TX
   //  3. Commit updates done for this TX  <-- this is where the commit update is supposed to be visible globally
   //  4. the revalidation of other TXs that follows this TX gets the latest value
   // if we defer the commit call to SEND_ACK_CLEANUP state handler, it may be called after #4,
   // causing revalidations to see the old value.
   if (m_core_config->timing_mode_vb_commit) {
      for (unsigned lane = 0; lane < m_warp_size; lane++) {
         commit_tx_t &tp = cmt_warp.m_thread_state[lane];
         if (tp.m_commit_id == -1) continue; // ignore inactive lane

         if (tp.m_pass) {
            tp.m_tm_manager->commit_core_side();
            tp.delete_tm_manager();
            delete_ptx_thread_tm_manager(warp_id, lane);
         } else {
           tp.m_tm_manager->abort();
           unsigned long long retry_delay = tp.m_tm_manager->abort_count() * 500; 
           if (retry_delay > 5000) retry_delay = 5000; 
           cmt_warp.m_retry_delay = gpu_sim_cycle + gpu_tot_sim_cycle + retry_delay; 
           tp.delete_tm_manager(); // just remove the reference from TLW, thread still need tm_manager for retry
         }
      }
   }

   for (unsigned lane = 0; lane < m_warp_size; lane++) {
      commit_tx_t &tp = cmt_warp.m_thread_state[lane];
      if (tp.m_commit_id == -1) continue; // ignore inactive lane

      if (not tp.m_pass) {
         tp.m_cu_commit_pending.reset(); 
      }
   }
}

// process replies (possibly coalesced ones) from commit unit 
// return true if this is a commit unit reply (and processed)
bool tx_log_walker_warpc::process_commit_unit_reply(mem_fetch *mf)
{
   // filter out the non-tx messages 
   if (mf->get_access_type() != TX_MSG) return false;

   switch (mf->get_type()) {
   case CU_PASS:
   case CU_FAIL:
      m_stats.n_cu_pass_msg++;
      if (mf->has_coalesced_packet()) {
         assert(m_core_config->tlw_intra_warp_conflict_detection); 
         while (mf->has_coalesced_packet()) {
            mem_fetch * scalar_msg = mf->next_coalesced_packet(); 
            process_cu_pass_fail(scalar_msg); 
            mf->pop_coalesced_packet(); 
            delete scalar_msg; 
         }
      } else {
         process_cu_pass_fail(mf); 
      }
      break; 
   case CU_DONE_COMMIT:
      if (mf->has_coalesced_packet()) {
         assert(m_core_config->tlw_intra_warp_conflict_detection); 
         while (mf->has_coalesced_packet()) {
            mem_fetch * scalar_msg = mf->next_coalesced_packet(); 
            process_cu_done_commit(scalar_msg); 
            mf->pop_coalesced_packet(); 
            delete scalar_msg; 
         }
      } else {
         process_cu_done_commit(mf); 
      }
      break; 
   case CU_ALLOC_PASS:
   case CU_ALLOC_FAIL:
      process_cu_alloc_reply(mf); 
      break;
   case NEWLY_INSERTED_ADDR:
   case REMOVED_ADDR:
      process_early_abort_msg(mf);
      break; 
   default:
      assert(0 && "[TLW] Undefined Message Received!"); 
      break; 
   }

   // return true for intercepting it 
   return true; 
}

void tx_log_walker_warpc::print(FILE *fout)
{
   tx_log_walker::print(fout); 
   fprintf(fout, "Warps in Intra Warp CD: "); 
   for (auto i_wid = m_intra_warp_cd_warps.begin(); i_wid != m_intra_warp_cd_warps.end(); ++i_wid) {
      fprintf(fout, "%d ", *i_wid); 
   }
   fprintf(fout, "\n"); 
}

tx_log_walker_warpc_logical::tx_log_walker_warpc_logical(shader_core_ctx *core, 
                                                         unsigned core_id, 
                                                         unsigned cluster_id, 
                                                         data_cache *L1D, 
                                                         mem_fetch_interface *&icnt, 
                                                         mem_fetch_allocator *mf_alloc,
                                                         const shader_core_config *core_config, 
                                                         const memory_config *memory_config, 
                                                         shader_core_stats &core_stats, 
                                                         tx_log_walker_stats &stats)
: tx_log_walker_warpc(core, core_id, cluster_id, L1D, icnt, mf_alloc, core_config, memory_config, core_stats, stats)
{
   // assert(m_core_config->tlw_fast_log_read == true);
   assert(m_core_config->cu_finite == false);
   assert(m_core_config->timing_mode_vb_commit == true); 
}

void tx_log_walker_warpc_logical::cycle() {
   out_message_queue_cycle();

   if (m_bg_commit_warps.empty()) return; // no warp committing in background 

   alloc_intra_warp_cd_resource(); 
   bool cache_accessed = false;
   unsigned cu_accessed = -1;
   bool yield_next_warp = false;

   int wid = select_active_warp();
   
   while (m_committing_warp[wid].all_commit()) {
       assert(g_tm_options.m_logical_timestamp_dynamic_concurrency_enabled);
       assert(m_committing_warp[wid].need_logical_tm_restart());
       if (m_core->is_masked_tm_token(wid) == false) {
           m_finish_commit_q.push_front(m_committing_warp[wid].m_inst); 
           m_bg_commit_warps.remove(wid); 
           m_committing_warp[wid].reset(); 
           m_warp[wid].get_tm_warp_info().reset();
           m_core->commit_warp_cleanup(wid); 
       } else {
           return;
       }
     
       if (m_bg_commit_warps.empty()) return;

       wid = select_active_warp();
   }

   warp_commit_tx_t &cmt_warp = m_committing_warp[wid]; 
   assert(cmt_warp.active()); 
   commit_tx_t &tp = cmt_warp.m_thread_state[cmt_warp.m_thread_processing];
   switch (tp.m_state) {
   case INTRA_WARP_CD: 
      if (m_core_config->tlw_timing_model == 1) {
         yield_next_warp = intra_warp_cd_cycle(wid, cmt_warp); 
      } else if (m_core_config->tlw_timing_model == 2) {
         yield_next_warp = intra_warp_cd_detail_cycle(wid, cmt_warp); 
      }
      break; 
   case ACQ_CID: 
      // acquire commit ID for all active threads/transactions in this warp in one cycle 
      for (unsigned t = 0; t < m_warp_size; t++) {
         commit_tx_t &ct = cmt_warp.m_thread_state[t];
         assert(ct.m_state == IDLE or ct.m_state == ACQ_CID); 
         if (ct.m_state == ACQ_CID) {
            acquire_cid(wid, wid * m_warp_size + t, ct);
         }
      }
      break;
   case ACQ_CU_ENTRIES:
   case WAIT_CU_ALLOC_REPLY:
   case SEND_RS: 
      assert(0); 
      break;
   case SEND_WS: {
      // generate access to L1D and send write-log out to commit unit 
      if (tp.write_log_sent()) {
         // empty write log -- this is possible  
         // transition whole warp to WAIT_CU_REPLY
         done_send_ws(cmt_warp); 
      } else {
         bool warp_wentry_sent = false; 
         for (unsigned t = 0; t < m_warp_size; t++) {
            commit_tx_t &ct = cmt_warp.m_thread_state[t];
            if (ct.m_state == IDLE) continue;
	    cu_accessed= -1;
	    bool aborted = ct.m_tm_manager->get_is_abort_need_clean();
            bool wentry_sent = send_log_entry(cmt_warp.m_inst, t, 
                                              ct.m_commit_id, ct.write_log_front(), cache_accessed, cu_accessed, aborted);
            if (wentry_sent) {
               warp_wentry_sent = true; 
               if (m_timelinef and tlw_watched(ct.m_commit_id)) 
                  fprintf(m_timelinef, "tx_cid = %d, SEND_WS @ %llu \n", 
                          ct.m_commit_id, gpu_sim_cycle + gpu_tot_sim_cycle); 
               if(cu_accessed != (unsigned) -1)
                    ct.m_sent_cu_entry.set(cu_accessed);
               ct.next_write_log_send(); 
               // inform the rest of the threads in this warp to not read the cache again 
               if (&ct == &tp) {
                  for (unsigned s = 0; s < m_warp_size; s++) {
                     commit_tx_t &cs = cmt_warp.m_thread_state[s];
                     if (&cs == &tp or cs.m_state == IDLE) continue; 
                     cs.write_log_front().atag_read = true; 
                     cs.write_log_front().data_read = true; 
                  }
               }
            } else {
               break; // only generate one cache miss is enough to fetch the whole line 
            }
         }
         if (warp_wentry_sent) {
            send_coalesced_packet(); 
            if (tp.write_log_sent()) {
               // transition whole warp to WAIT_CU_REPLY
               done_send_ws(cmt_warp); 
            } 
         } else {
            if (tp.write_log_front().atag_cachemiss || tp.write_log_front().data_cachemiss) {
               yield_next_warp = true; 
            }  
         } 
      }
      } break; 
   case WAIT_CU_REPLY: 
      assert(0);
      break; 
   case SEND_ACK_CLEANUP: 
      if (tp.m_cu_commit_pending.any() or  
          (tp.m_tm_manager->get_is_abort_need_clean() and cmt_warp.m_retry_delay != 0 and gpu_sim_cycle + gpu_tot_sim_cycle <= cmt_warp.m_retry_delay)) {
         if (cmt_warp.all_sent()) {
            yield_next_warp = true; 
         } else {
            cmt_warp.next_thread_to_non_waiting();
         }
      } else {
         tp.m_state = IDLE;
	 if (tp.m_tm_manager->get_is_abort_need_clean()) {
             cmt_warp.m_need_logical_tm_restart = true;
	     tp.m_tm_manager->clear_is_abort_need_clean();
	     tp.m_tm_manager->clear_write_data();
	     assert(tp.m_tm_manager->owned_addr.empty());
	     tp.delete_tm_manager();
	 } else {
             s_n_commited += 1;
             tp.m_tm_manager->get_ptx_thread()->tm_commit();
	     tp.m_tm_manager->clear_write_data();
	     assert(tp.m_tm_manager->owned_addr.empty());
	     tp.delete_tm_manager();
	     delete_ptx_thread_tm_manager(wid, cmt_warp.m_thread_processing);
	 }
         cmt_warp.next_thread(); 
      }
      break; 
   default: assert(0); 
   }; 

   if (cmt_warp.all_commit()) { 
      if (cmt_warp.need_logical_tm_restart() and m_core->could_mask_tm_token(wid)) {
	 m_core->mask_tm_token(wid);
      } else { 
         // finish commit push the commit instruction to finish queue to unlock scoreboard at writeback stage 
         m_finish_commit_q.push_front(m_committing_warp[wid].m_inst); 
         m_bg_commit_warps.remove(wid); 
         m_committing_warp[wid].reset(); 
         m_warp[wid].get_tm_warp_info().reset();
         
         m_core->commit_warp_cleanup(wid); 
      }
   } else if (yield_next_warp or cmt_warp.all_sent()) {
      // all request from this warp is sent yield to other warps to send logs  
      m_bg_commit_warps.remove(wid); 
      m_bg_commit_warps.push_back(wid); 
   }
}

// process replies (possibly coalesced ones) from commit unit 
// return true if this is a commit unit reply (and processed)
bool tx_log_walker_warpc_logical::process_commit_unit_reply(mem_fetch *mf)
{
   // filter out the non-tx messages 
   if (mf->get_access_type() != TX_MSG) return false;

   switch (mf->get_type()) {
   case CU_PASS:
   case CU_FAIL:
      assert(0);
      break; 
   case CU_DONE_COMMIT:
      if (mf->has_coalesced_packet()) {
         assert(m_core_config->tlw_intra_warp_conflict_detection); 
         while (mf->has_coalesced_packet()) {
            mem_fetch * scalar_msg = mf->next_coalesced_packet(); 
            process_cu_done_commit(scalar_msg); 
            mf->pop_coalesced_packet(); 
            delete scalar_msg; 
         }
      } else {
         process_cu_done_commit(mf); 
      }
      break; 
   case CU_ALLOC_PASS:
   case CU_ALLOC_FAIL:
      assert(0); 
      break; 
   case NEWLY_INSERTED_ADDR:
   case REMOVED_ADDR:
      assert(0);
      break;
   default:
      assert(0 && "[TLW] Undefined Message Received!"); 
      break; 
   }

   // return true for intercepting it 
   return true; 
}

void tx_log_walker_warpc_logical::done_send_ws(warp_commit_tx_t &cmt_warp)
{
   // transition whole warp to SEND_ACK_CLEANUP
   for (unsigned t = 0; t < m_warp_size; t++) {
      commit_tx_t &ct = cmt_warp.m_thread_state[t];
      if (ct.m_state == IDLE) continue; 
      assert(ct.write_log_sent()); // make sure this is true across the whole warp 
      assert(ct.m_state == SEND_WS);
      // send DONE_FILL message to commit unit
      generate_done_fill_coalesced(cmt_warp.m_inst, ct);
      ct.enter_send_ack_cleanup(m_memory_config->m_n_mem_sub_partition);
   }
   send_coalesced_packet(); 
}

void shader_core_ctx::init_aborted_tx_pts(unsigned wid) {
    if (m_config->tlw_use_logical_temporal_cd == false) return;
    unsigned index = m_sid * m_config->max_warps_per_shader + wid;
    if (logical_temporal_conflict_detector::get_singleton().warp_level_conflict_exist(index)) {
        logical_temporal_conflict_detector::get_singleton().advance_warp_pts(m_sid, index);
    } else {
        logical_temporal_conflict_detector::get_singleton().inc_warp_pts(m_sid, index);
    }
    // Because tx could be aborted because of intra_warp confliction, so need to do following thing even though warp_level_conflict_exist() is true
    for (unsigned t = 0; t < m_warp_size; t++) {
        int t_tid = wid * m_warp_size + t;
        tm_manager_inf *t_tm_manager = get_func_thread_info(t_tid)->get_tm_manager();
        if (t_tm_manager) {
            t_tm_manager->init_aborted_tx_pts();
        }
    }
}

void shader_core_ctx::check_num_aborts() {
    bool dynamic_concurrency_enabled = g_tm_options.m_logical_timestamp_dynamic_concurrency_enabled;
    if (dynamic_concurrency_enabled) {
        unsigned exec_phase_length = g_tm_options.m_logical_timestamp_exec_phase_length;
        if ((gpu_sim_cycle + gpu_tot_sim_cycle) % exec_phase_length == 0) {
            unsigned long long prev_num_aborts = g_tm_global_statistics.m_n_prev_aborts;
            unsigned long long current_num_aborts = g_tm_global_statistics.m_n_aborts;
            g_tm_global_statistics.m_n_prev_aborts = current_num_aborts;
            int num_aborts_limit = g_tm_options.m_logical_timestamp_num_aborts_limit;
	    int num_aborts_inc = current_num_aborts - prev_num_aborts;
            if (num_aborts_inc > num_aborts_limit) {
		m_over_num_aborts_limit = true;
	    } else { 
		m_over_num_aborts_limit = false;
		m_scoreboard->recoverMaskedTMToken();
	    }
        }
    } else {
        assert(m_over_num_aborts_limit == false);
    }
    g_tm_global_statistics.m_num_masked_tm_token.add2bin(num_masked_tm_token());
}

// Functions for LSU HPCA2016 Early Abort paper
bool tx_log_walker::is_conflictAddrTable_full() {
    return m_conflict_address_table.size() >= g_tm_options.m_conflict_address_table_size; 
}

bool tx_log_walker::hit_in_conflictAddrTable(addr_t waddr, bool rd) {
    if (m_conflict_address_table.count(waddr) == 0) return false;
    bool waddr_read = m_conflict_address_table[waddr].first;
    bool waddr_written = m_conflict_address_table[waddr].second;
    assert(waddr_read == true || waddr_written == true);
    return !rd || waddr_written;
}

void tx_log_walker::process_early_abort_msg(mem_fetch *mf) {
    assert(g_tm_options.m_lsu_hpca_enabled == true);

    std::set<addr_t> addr_set = mf->get_early_abort_addr_set();
    assert(addr_set.size() > 0);
    bool is_rd = mf->is_early_abort_read();
    bool is_inserted = mf->is_early_abort_inserted();
    if (is_inserted) {
        for (auto iter = addr_set.begin(); iter != addr_set.end(); iter++) {
	    addr_t addr = *iter;
	    if (m_conflict_address_table.count(addr) > 0 || !is_conflictAddrTable_full()) {
	        if (is_rd) 
		    m_conflict_address_table[addr].first = true;
		else 
		    m_conflict_address_table[addr].second = true;
	    } 
	}
    } else {
        for (auto iter = addr_set.begin(); iter != addr_set.end(); iter++) {
	    addr_t addr = *iter;
	    if (m_conflict_address_table.count(addr) > 0) {
	        if (is_rd) 
	            m_conflict_address_table[addr].first = false;
	        else 
	            m_conflict_address_table[addr].second = false;
	    }
	    if (!m_conflict_address_table[addr].first && !m_conflict_address_table[addr].second)
	        m_conflict_address_table.erase(addr);
	}
    }
    g_tm_global_statistics.m_conflict_address_table_size.add2bin(m_conflict_address_table.size());
}

void tx_log_walker::early_abort_checking(warp_inst_t &inst, active_mask_t &intra_warp_abort_mask) {
    // Overlap early abort checking with intra warp conflict checking totally, but not sure whehter this is really true
    unsigned warp_id = inst.warp_id();
    const unsigned warp_size = m_core_config->warp_size;
    const tm_warp_info& warp_info = m_warp[warp_id].get_tm_warp_info();

    for (unsigned rd = 0; rd < warp_info.m_read_log_size; rd++) {
	unsigned check_cycles = 0;
        for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
            if (inst.active(lane_id) == false) continue;
            if (intra_warp_abort_mask.test(lane_id)) continue;
	    addr_t address_tag = warp_info.m_read_log[rd].m_addr[lane_id];
	    if (address_tag != 0) {
		check_cycles++;
	        if (hit_in_conflictAddrTable(address_tag, true)) {
		    intra_warp_abort_mask.set(lane_id);
		    g_tm_global_statistics.m_tot_early_aborts++;
		}
	    }
        }
	if (check_cycles > 0)
	    g_tm_global_statistics.m_conflict_address_table_cycles.add2bin(check_cycles);
    }
    
    for (unsigned wt = 0; wt < warp_info.m_write_log_size; wt++) {
	unsigned check_cycles = 0;
        for (unsigned lane_id = 0; lane_id < warp_size; lane_id++) {
            if (inst.active(lane_id) == false) continue;
            if (intra_warp_abort_mask.test(lane_id)) continue;
	    addr_t address_tag = warp_info.m_write_log[wt].m_addr[lane_id];
	    if (address_tag != 0) {
		check_cycles++;
	        if (hit_in_conflictAddrTable(address_tag, false)) {
		    intra_warp_abort_mask.set(lane_id);
		    g_tm_global_statistics.m_tot_early_aborts++;
	        }
	    }
        }
	if (check_cycles > 0)
	    g_tm_global_statistics.m_conflict_address_table_cycles.add2bin(check_cycles);
    }
}

void ldst_unit::pause_and_go(mem_fetch *mf) {
    if (g_tm_options.m_pause_and_go_enabled == false) return;

    unsigned wid = mf->get_inst().warp_id();
    new_addr_type addr = mf->get_addr();
    const std::map<new_addr_type, active_mask_t> word_active_mask = mf->get_word_active_mask();
    active_mask_t word_active;
    word_active.reset();
    std::map<new_addr_type, active_mask_t>::const_iterator iter;
    for (iter = word_active_mask.begin(); iter != word_active_mask.end(); iter++) {
        new_addr_type waddr = iter->first;
	assert(waddr >= addr);
        assert((waddr-addr)%4 == 0);
	assert(iter->second.any());
        if (m_TLW->hit_in_conflictAddrTable(waddr, !mf->is_write())) {
            word_active = word_active | iter->second;	
	}
    }
    if (word_active.any()) {
        m_core->pause_and_go(wid, word_active); 	
    }
}
