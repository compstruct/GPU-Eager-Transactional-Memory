/***************************************** 
 
  tm_manager.h
 
  encapsulates data required by transactional memory
  includes restore point, memory values, functionality
  for manipulating g_thread and tm state
 
 
****************************************/


#ifndef __TM_MANAGER_H
#define __TM_MANAGER_H

// #define DEBUG_TM

#include "../abstract_hardware_model.h" 
#include "../option_parser.h"
#include "ptx_sim.h"
#include "memory.h"
#include "../gpgpu-sim/visualizer.h"

#include <list>
#include <vector>
#include <set>
#include <unordered_set>
#include <map>
#include <algorithm> 
#include <math.h>

class ptx_thread_info; 
class tm_manager_inf
{
public:
   static tm_manager_inf* create_tm_manager( ptx_thread_info *thread, bool timing_mode ); 

   tm_manager_inf( ptx_thread_info *thread, bool timing_mode ); // do not call this outside tm_manager.cc
   virtual ~tm_manager_inf(); 

   // for keeping tm_manager alive until all commit units are done
   int inc_ref_count();
   int dec_ref_count();
   int get_ref_count(); 

   virtual void start() = 0;
   virtual bool tm_access( memory_space* mem, memory_space_t space, bool rd, addr_t addr, void* vp, int nbytes, tm_access_uarch_info& uarch_info, mem_fetch *mf, bool &update_logical_info ) = 0; // return false if validation failed 
   virtual void abort() = 0;
   virtual bool commit( bool auto_self_abort ) = 0; // return false if commit-validation failed 
   virtual void accessmode( int readmode, int writemode ) = 0; 
   virtual void add_rollback_insn( unsigned insn_count ) = 0; // track the number of rolled back instruction at abort
   virtual void add_committed_insn( unsigned insn_count ) = 0; // track the number of committed instructions at commit
   unsigned tuid() const { return m_thread_uid; }
   unsigned uid() const { return m_uid; }
   unsigned sid() const { return m_thread_sc; }
   unsigned wid() const { return m_thread_hwwid; }
   unsigned nesting_level() { return m_nesting_level;}
   unsigned abort_count() { return m_abort_count; }
   bool watched() const; 

   virtual bool get_read_conflict_detection() const = 0; 
   virtual bool get_write_conflict_detection() const = 0;
   virtual bool get_version_management() const = 0; 

   virtual unsigned get_n_read() const = 0; // number of read requiring conflict detection 
   virtual unsigned get_n_write() const = 0; // number of write requiring buffering 

   // interface for timing model validation and commit 
   virtual bool validate_addr( addr_t addr ) = 0; // validate a single word 
   virtual void commit_addr( addr_t addr ) = 0; // commit a single word 
   virtual void commit_core_side( ) = 0; // commit a transaction on the core side 
   virtual void validate_or_crash( ) = 0; // validate a transaction and crash if it is not valid

   virtual bool has_conflict_with( tm_manager_inf * other_tx ) = 0; // detect conflict between this transaction and the other 
   virtual bool validate_all( bool useTemporalCD ) = 0; // validate entire read-set (return true if pass)

   // warp-level transaction helper functions 
   virtual void set_is_warp_level() { m_is_warp_level = true; }
   virtual bool get_is_warp_level() { return m_is_warp_level; }
   virtual void share_gmem_view( tm_manager_inf* other_tx ) = 0; // share the global memory view with the given transaction 

   virtual void set_is_abort_need_clean() { m_is_abort_need_clean = true; }
   virtual void clear_is_abort_need_clean() { m_is_abort_need_clean = false; }
   virtual bool get_is_abort_need_clean() { return m_is_abort_need_clean; }

   virtual void update_logical_info(addr_t addr, bool rd, int nbytes, memory_space_t space) = 0;

   virtual bool logical_tx_aborted() = 0;
   virtual void init_aborted_tx_pts() = 0;

   virtual void set_tm_raw_info(addr_t addr, memory_space *mem, memory_space_t space, bool rd, tm_access_uarch_info& uarch_info, size_t size) = 0;

   virtual void clear_write_data() = 0;

   ptx_thread_info *get_ptx_thread() { return m_thread; }

   std::map<addr_t, unsigned> owned_addr;

protected:
   unsigned m_uid;
   bool m_timing_mode;
   unsigned m_nesting_level;
   ptx_thread_info *m_thread;
   
   unsigned m_thread_uid;
   unsigned m_thread_sc;
   unsigned m_thread_hwwid;
   unsigned m_thread_hwtid;

   int m_ref_count; 

   unsigned m_abort_count; 

   bool m_is_warp_level; // true if this transaction is part of a warp-level transaction

   bool m_is_abort_need_clean; // in logical timestamp besed tm manager, aborted TX need to clean number of writing

   // unsigned long long m_start_cycle; // when the transaction called txbegin()
   // unsigned long long m_first_read_cycle; // when the transaction first load from memory 
};

void tm_reg_options(option_parser_t opp); 
void tm_sample_conflict_footprint(); // called every cycle 
void tm_dump_profile(); // called at the end of simulation 
void tm_statistics(FILE *fout); 
void tm_statistics_visualizer( gzFile visualizer_file ); 

#endif


