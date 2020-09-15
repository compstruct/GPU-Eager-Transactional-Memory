
#include "l2cache.h"
#include "commit_unit.h"
#include "mem_fetch.h"
#include "histogram.h"
#include "gpu-sim.h"
#include "../cuda-sim/tm_manager.h"

#include <algorithm>

class commit_unit_options : public OptionChecker 
{
public:
   bool m_dummy_mode;
   bool m_detect_conflicting_cid; 
   bool m_fast_match; 
   int m_gen_L2_acc; 
   int m_rop_latency;
   bool m_check_read_set_version; 
   bool m_fail_at_revalidation;
   bool m_vwait_nostall; 

   unsigned int m_input_queue_length;
   unsigned int m_fcd_mode;
   unsigned int m_delayed_fcd_cd_mode;
   unsigned int m_overclock_hazard_detect; 
   bool m_warp_level_hazard_detect; 

   bool m_ideal_L2_validation; 
   bool m_ideal_L2_commit; 

   bool m_use_bloomfilter; 
   unsigned int m_bloomfilter_size; 
   unsigned int m_bloomfilter_n_func; 
   std::vector<int> m_bloomfilter_func_id;

   unsigned m_conflict_table_hash_sets;
   unsigned m_conflict_table_hash_ways;
   unsigned m_conflict_table_bf_size;
   unsigned m_conflict_table_bf_n_funcs;
   unsigned m_conflict_table_granularity; 

   bool m_dump_timestamps; 

   bool m_parallel_process_coalesced_input; 
   bool m_coalesce_reply; 
   bool m_coalesce_mem_op; 

   unsigned m_coalesce_block_size; 

   commit_unit_options() 
      : m_bloomfilter_func_id(4)
   {
      m_fast_match = true;
   }
   void reg_options(option_parser_t opp);

   virtual void check_n_derive(); 
};

void commit_unit_options::reg_options(option_parser_t opp)
{
   option_parser_register(opp, "-cu_dummy_mode", OPT_BOOL, &m_dummy_mode, 
               "dummy mode commit unit that just sends CU_PASS at receiving a TX_DONE_FILL (default = off)",
               "0");
   option_parser_register(opp, "-cu_detect_conflicting_cid", OPT_BOOL, &m_detect_conflicting_cid, 
               "detect conflict among committing transactions (default = on)",
               "1");
   option_parser_register(opp, "-cu_gen_L2_acc", OPT_INT32, &m_gen_L2_acc, 
               "generate L2 traffic for (0=none, 1=validate_only, 2=commit_only, 3=both)",
               "3");
   option_parser_register(opp, "-cu_rop_latency", OPT_INT32, &m_rop_latency, 
               "ROP latency for commit unit (default=115)",
               "115");
   option_parser_register(opp, "-cu_check_rs_version", OPT_BOOL, &m_check_read_set_version, 
               "Check read set version when a commit entry commit a value/change state to RETIRE (default = off)",
               "0");

   option_parser_register(opp, "-cu_use_bloomfilter", OPT_BOOL, &m_use_bloomfilter, 
               "Use bloomfilter for conflict detection between committing tx (default = off = run in shadow)",
               "0");
   option_parser_register(opp, "-cu_bloomfilter_size", OPT_UINT32, &m_bloomfilter_size, 
               "Number of bits in each hash function in commit unit bloomfilter (default=64)",
               "64");
   option_parser_register(opp, "-cu_bloomfilter_n_func", OPT_UINT32, &m_bloomfilter_n_func, 
               "Number of bits in each hash function in commit unit bloomfilter (default=64)",
               "4");

   option_parser_register(opp, "-cu_fail_at_revalidation", OPT_BOOL, &m_fail_at_revalidation, 
               "Just fail the validation instead of revalidation (default = off = do revalidate)",
               "0");
   option_parser_register(opp, "-cu_vwait_nostall", OPT_BOOL, &m_vwait_nostall, 
               "Allow pass pointer to advance through validation wait to overlap validation and ack wait (default = off)",
               "0");
   option_parser_register(opp, "-cu_dump_timestamps", OPT_BOOL, &m_dump_timestamps, 
               "Dump the commit unit entry state transition timestamp in each commit unit to file (default = off)",
               "0");
   option_parser_register(opp, "-cu_input_queue_length", OPT_UINT32, &m_input_queue_length, 
               "Input message queue length in a commit unit (default=64)",
               "64");
   option_parser_register(opp, "-cu_fcd_mode", OPT_UINT32, &m_fcd_mode,
                  "Fast conflict detection mode. 0 = BF, 1 = Delayed FCD (default = 0)",
                  "0");
   option_parser_register(opp, "-cu_delayed_fcd_cd_mode", OPT_UINT32, &m_delayed_fcd_cd_mode,
                     "Delayed FCD conflict detector. 0 = perfect, 1 = Hash+BF(default = 1)",
                     "1");
   option_parser_register(opp, "-cu_overclock_hazard_detect", OPT_UINT32, &m_overclock_hazard_detect,
                     "Overclock ratio for delayed FCD conflict (hazard) detector (default = 1)",
                     "1");
   option_parser_register(opp, "-cu_warp_level_hazard_detect", OPT_BOOL, &m_warp_level_hazard_detect, 
                        "Enable warp-level hazard detection (default = off)",
                        "0");

   option_parser_register(opp, "-cu_conflict_table_hash_sets", OPT_UINT32, &m_conflict_table_hash_sets,
                  "Number of sets in conflict table hash (default=256)",
                  "256");
   option_parser_register(opp, "-cu_conflict_table_hash_ways", OPT_UINT32, &m_conflict_table_hash_ways,
                  "Number of ways in conflict table hash (default=4)",
                  "4");
   option_parser_register(opp, "-cu_conflict_table_bf_size", OPT_UINT32, &m_conflict_table_bf_size,
                     "Number of entries per hash in conflict bloom filter (default=256)",
                     "256");
   option_parser_register(opp, "-cu_conflict_table_bf_n_funcs", OPT_UINT32, &m_conflict_table_bf_n_funcs,
                     "Number of hash functions for BF (1-4) (default=4)",
                     "4");
   option_parser_register(opp, "-cu_conflict_table_granularity", OPT_UINT32, &m_conflict_table_granularity,
                     "Ganularity of conflict table (default=4B)",
                     "4");

   option_parser_register(opp, "-cu_ideal_L2_validation", OPT_BOOL, &m_ideal_L2_validation, 
               "Validation always hit L2 (default = off)",
               "0");
   option_parser_register(opp, "-cu_ideal_L2_commit", OPT_BOOL, &m_ideal_L2_commit, 
               "Commit write always hit L2 (default = off)",
               "0");

   option_parser_register(opp, "-cu_parallel_process_coalesced_input", OPT_BOOL, &m_parallel_process_coalesced_input, 
               "process input to commit unit in parallel (default = off)",
               "0");
   option_parser_register(opp, "-cu_coalesce_reply", OPT_BOOL, &m_coalesce_reply, 
               "coalesce reply messages (CU_PASS/FAIL/COMMIT_DONE) from commit unit (default = off)",
               "0");
   option_parser_register(opp, "-cu_coalesce_mem_op", OPT_BOOL, &m_coalesce_mem_op, 
               "coalesce memory operations (validations/commits) from commit unit (default = off)",
               "0");
   option_parser_register(opp, "-cu_coalesce_block_size", OPT_UINT32, &m_coalesce_block_size,
                     "Maximum size of each coalesced access from commit unit (default=32B)",
                     "32");

}

void commit_unit_options::check_n_derive()
{
   if (m_coalesce_reply) {
      assert(m_parallel_process_coalesced_input == true); 
   }
   if (m_warp_level_hazard_detect) {
      assert(m_parallel_process_coalesced_input == true); 
   }
   assert(m_overclock_hazard_detect > 0); 
}

static const char* commit_state_str[] = {
   "UNUSED", 
   "FILL",
   "HAZARD_DETECT", 
   "VALIDATION_WAIT", 
   "REVALIDATION_WAIT", 
   "PASS",
   "FAIL",
   "PASS_ACK_WAIT",
   "COMMIT_READY",
   "COMMIT_SENT",
   "RETIRED"
};

class commit_unit_stats
{
public:
   pow2_histogram m_validation_latency; 
   pow2_histogram m_commit_latency; 

   pow2_histogram m_entry_lifetime; 

   pow2_histogram m_unused_time;
   pow2_histogram m_fill_time;
   pow2_histogram m_validation_wait_time;
   pow2_histogram m_revalidation_wait_time;
   pow2_histogram m_pass_fail_time; 
   pow2_histogram m_ack_wait_time; 
   pow2_histogram m_commit_time; 
   pow2_histogram m_retire_time;

   pow2_histogram m_distance_fcd_pass;
   pow2_histogram m_distance_retire_head; 
   pow2_histogram m_active_entries;
   pow2_histogram m_active_entries_have_rs;
   pow2_histogram m_active_entries_have_ws;
   pow2_histogram m_active_entries_need_rs;
   pow2_histogram m_active_entries_need_ws;
   pow2_histogram m_conflict_table_size;

   pow2_histogram m_read_buffer_usage; 
   pow2_histogram m_write_buffer_usage; 

   pow2_histogram m_revalidation_distance;

   unsigned long long m_validation_L2_access;
   unsigned long long m_validation_L2_hit;
   unsigned long long m_commit_L2_access;
   unsigned long long m_commit_L2_hit;

   unsigned long long m_bloomfilter_detections; 
   unsigned long long m_bloomfilter_hit; 
   unsigned long long m_bloomfilter_false_positive; 

   unsigned long long m_conflict_table_hash_hits;
   unsigned long long m_conflict_table_hash_misses;
   unsigned long long m_conflict_table_hash_evictions;
   unsigned long long m_conflict_table_bf_true_positives;
   unsigned long long m_conflict_table_bf_false_positives;
   unsigned long long m_conflict_table_bf_true_negatives;

   struct cid_pointer_stats {
      std::string m_name;
      std::vector<unsigned int> m_stall_reason; 
      pow2_histogram m_stall_duration; // how long as the pointer been stuck 
      cid_pointer_stats (const char *name) 
         : m_name(name), m_stall_reason(N_COMMIT_STATE), m_stall_duration(m_name + "stall_duration")
      { }
      void print(FILE *fout); 
   };

   cid_pointer_stats m_cid_fcd_stats;
   cid_pointer_stats m_cid_pass_stats; 
   cid_pointer_stats m_cid_commit_stats; 
   cid_pointer_stats m_cid_retire_stats; 

   pow2_histogram m_input_queue_size; 
   pow2_histogram m_response_queue_size; 
   pow2_histogram m_validation_queue_size; 
   pow2_histogram m_commit_queue_size; 

   unsigned long long m_read_set_access_raw;
   unsigned long long m_read_set_access_coalesced;
   unsigned long long m_write_set_access_raw;
   unsigned long long m_write_set_access_coalesced;

   commit_unit_stats()
      : m_validation_latency("cu_validation_latency"),
        m_commit_latency("cu_commit_latency"), 
        m_entry_lifetime("cu_entry_lifetime"),
        m_unused_time("cu_unused_time"),
        m_fill_time("cu_fill_time"),
        m_validation_wait_time("cu_validation_wait_time"),
        m_revalidation_wait_time("cu_revalidation_wait_time"),
        m_pass_fail_time("cu_pass_fail_time"), 
        m_ack_wait_time("cu_ack_wait_time"),
        m_commit_time("cu_commit_time"), 
        m_retire_time("cu_retire_time"),
        m_distance_fcd_pass("cu_distance_fcd_pass"),
        m_distance_retire_head("cu_distance_retire_head"),
        m_active_entries("cu_active_entries"),
        m_active_entries_have_rs("cu_active_entries_have_rs"),
        m_active_entries_have_ws("cu_active_entries_have_ws"),
        m_active_entries_need_rs("cu_active_entries_need_rs"),
        m_active_entries_need_ws("cu_active_entries_need_ws"),
        m_conflict_table_size("cu_conflict_table_size"),
        m_read_buffer_usage("cu_read_buffer_usage"), 
        m_write_buffer_usage("cu_write_buffer_usage"),
        m_revalidation_distance("cu_revalidation_distance"),
        m_validation_L2_access(0), 
        m_validation_L2_hit(0), 
        m_commit_L2_access(0), 
        m_commit_L2_hit(0), 
        m_bloomfilter_detections(0),
        m_bloomfilter_hit(0), 
        m_bloomfilter_false_positive(0),
        m_conflict_table_hash_hits(0),
        m_conflict_table_hash_misses(0),
        m_conflict_table_hash_evictions(0),
        m_conflict_table_bf_true_positives(0),
        m_conflict_table_bf_false_positives(0),
        m_conflict_table_bf_true_negatives(0),
        m_cid_fcd_stats("cid_fcd"),
        m_cid_pass_stats("cid_pass"), 
        m_cid_commit_stats("cid_commit"), 
        m_cid_retire_stats("cid_retire"),
        m_input_queue_size("cu_input_queue_size"),
        m_response_queue_size("cu_response_queue_size"), 
        m_validation_queue_size("cu_validation_queue_size"), 
        m_commit_queue_size("cu_commit_queue_size"), 
        m_read_set_access_raw(0),
        m_read_set_access_coalesced(0),
        m_write_set_access_raw(0),
        m_write_set_access_coalesced(0)
   { }

   void print(FILE *fout); 
};

void commit_entry::at_retire_ptr(class commit_unit_stats &stats, unsigned time)
{
   stats.m_entry_lifetime.add2bin(time - m_alloc_time); 

   if(m_fill_time > 0) { // Not a skip
      stats.m_unused_time.add2bin(m_fill_time - m_alloc_time);
      stats.m_fill_time.add2bin(m_validation_wait_time - m_fill_time); // time diff between first RS msg to DONE_FILL msg

      // Time in validation wait
      if(m_revalidation_wait_time > 0) // Went to REVAL_WAIT
         stats.m_validation_wait_time.add2bin(m_revalidation_wait_time - m_validation_wait_time);
      else // VAL_WAIT to PASS/FAIL
         stats.m_validation_wait_time.add2bin(m_pass_fail_time - m_validation_wait_time);

      // Time in revalidation wait state
      if(m_revalidation_wait_time > 0) {
         if(m_ack_wait_time > 0) // Went to pass ack
            stats.m_revalidation_wait_time.add2bin(m_ack_wait_time - m_revalidation_wait_time);
         else // Went to retired
            stats.m_revalidation_wait_time.add2bin(m_retired_time - m_revalidation_wait_time);
      } else {
         stats.m_revalidation_wait_time.add2bin(0);
      }

      // Time in pass/fail state
      if(m_revalidation_wait_time == 0) // Went to pass or fail state (didn't go to revalidate)
      {
         if (m_ack_wait_time == 0) // FAIL
            stats.m_pass_fail_time.add2bin(m_retired_time - m_pass_fail_time); // time when entry just wait for pass ptr arrive
         else // PASS
            stats.m_pass_fail_time.add2bin(m_ack_wait_time - m_pass_fail_time); // time when entry just wait for pass ptr arrive
      } else {
         stats.m_pass_fail_time.add2bin(0);
      }

      // Time in pass act wait state
      if(m_ack_wait_time > 0) { // Reached pass_ack
         if(m_commit_ready_time > 0) // Went to commit
            stats.m_ack_wait_time.add2bin(m_commit_ready_time - m_ack_wait_time);
         else // Went to retired
            stats.m_ack_wait_time.add2bin(m_retired_time - m_ack_wait_time);
      } else {
         stats.m_ack_wait_time.add2bin(0);
      }

      // Commit time
      if(m_commit_ready_time > 0) {
         stats.m_commit_time.add2bin(m_retired_time - m_commit_ready_time);
      } else {
         stats.m_commit_time.add2bin(0);
      }

      // sample buffer usage 
      stats.m_read_buffer_usage.add2bin(read_set().get_linear_buffer().size());
      stats.m_write_buffer_usage.add2bin(write_set().get_linear_buffer().size());

      // revalidation was set in this entry 
      if (m_revalidate_set) {
         stats.m_revalidation_distance.add2bin(m_commit_id - m_youngest_conflicting_commit_id);
      }

   } else {
      // Was a skip
      stats.m_unused_time.add2bin(m_retired_time - m_alloc_time);
      stats.m_fill_time.add2bin(0);
      stats.m_validation_wait_time.add2bin(0);
      stats.m_revalidation_wait_time.add2bin(0);
      stats.m_pass_fail_time.add2bin(0);
      stats.m_ack_wait_time.add2bin(0);
      stats.m_commit_time.add2bin(0);
   }

   // Retire to deallocation (retire ptr)
   stats.m_retire_time.add2bin(time - m_retired_time);
}

void commit_entry::at_retire_ptr_logical(class commit_unit_stats &stats, unsigned time)
{
   stats.m_entry_lifetime.add2bin(time - m_alloc_time); 

   if(m_fill_time > 0) { // Not a skip
      stats.m_unused_time.add2bin(m_fill_time - m_alloc_time);

      // Commit time
      if(m_commit_ready_time > 0) {
         stats.m_fill_time.add2bin(m_commit_ready_time - m_fill_time); // time diff between first RS msg to DONE_FILL msg
         stats.m_commit_time.add2bin(m_retired_time - m_commit_ready_time);
      } else {
	 stats.m_fill_time.add2bin(0);
         stats.m_commit_time.add2bin(0);
      }

      // sample buffer usage 
      stats.m_read_buffer_usage.add2bin(read_set().get_linear_buffer().size());
      stats.m_write_buffer_usage.add2bin(write_set().get_linear_buffer().size());

   } else {
      // Was a skip
      stats.m_unused_time.add2bin(m_retired_time - m_alloc_time);
      stats.m_fill_time.add2bin(0);
      stats.m_commit_time.add2bin(0);
   }

   // Retire to deallocation (retire ptr)
   stats.m_retire_time.add2bin(time - m_retired_time);
}

void commit_unit_stats::print(FILE *fout)
{
   m_validation_latency.fprint(fout); fprintf(fout, "\n"); 
   m_commit_latency.fprint(fout); fprintf(fout, "\n"); 

   m_entry_lifetime.fprint(fout); fprintf(fout, "\n"); 

   m_unused_time.fprint(fout); fprintf(fout, "\n");
   m_fill_time.fprint(fout); fprintf(fout, "\n"); 
   m_validation_wait_time.fprint(fout); fprintf(fout, "\n"); 
   m_revalidation_wait_time.fprint(fout); fprintf(fout, "\n");
   m_pass_fail_time.fprint(fout); fprintf(fout, "\n"); 
   m_ack_wait_time.fprint(fout); fprintf(fout, "\n"); 
   m_commit_time.fprint(fout); fprintf(fout, "\n");
   m_retire_time.fprint(fout); fprintf(fout, "\n");

   m_distance_fcd_pass.fprint(fout); fprintf(fout, "\n");
   m_distance_retire_head.fprint(fout); fprintf(fout, "\n"); 
   m_active_entries.fprint(fout); fprintf(fout, "\n");
   m_active_entries_have_rs.fprint(fout); fprintf(fout, "\n");
   m_active_entries_have_ws.fprint(fout); fprintf(fout, "\n");
   m_active_entries_need_rs.fprint(fout); fprintf(fout, "\n");
   m_active_entries_need_ws.fprint(fout); fprintf(fout, "\n");
   m_conflict_table_size.fprint(fout); fprintf(fout, "\n");

   m_read_buffer_usage.fprint(fout); fprintf(fout, "\n"); 
   m_write_buffer_usage.fprint(fout); fprintf(fout, "\n"); 

   m_revalidation_distance.fprint(fout); fprintf(fout, "\n");

   fprintf(fout, "cu_validation_L2_access = %llu\n", m_validation_L2_access);
   fprintf(fout, "cu_validation_L2_hit = %llu\n", m_validation_L2_hit);
   fprintf(fout, "cu_commit_L2_access = %llu\n", m_commit_L2_access);
   fprintf(fout, "cu_commit_L2_hit = %llu\n", m_commit_L2_hit);

   fprintf(fout, "cu_bloomfilter_detections = %llu\n", m_bloomfilter_detections); 
   fprintf(fout, "cu_bloomfilter_hit = %llu\n", m_bloomfilter_hit); 
   fprintf(fout, "cu_bloomfilter_false_positive = %llu\n", m_bloomfilter_false_positive); 

   fprintf(fout, "cu_conflict_table_hash_hits = %llu\n", m_conflict_table_hash_hits);
   fprintf(fout, "cu_conflict_table_hash_misses = %llu\n", m_conflict_table_hash_misses);
   fprintf(fout, "cu_conflict_table_hash_evictions = %llu\n", m_conflict_table_hash_evictions);
   fprintf(fout, "cu_conflict_table_bf_true_positives = %llu\n", m_conflict_table_bf_true_positives);
   fprintf(fout, "cu_conflict_table_bf_false_positives = %llu\n", m_conflict_table_bf_false_positives);
   fprintf(fout, "cu_conflict_table_bf_true_negative = %llu\n", m_conflict_table_bf_true_negatives);

   m_cid_fcd_stats.print(fout);
   m_cid_pass_stats.print(fout); 
   m_cid_commit_stats.print(fout); 
   m_cid_retire_stats.print(fout); 

   m_input_queue_size.fprint(fout); fprintf(fout, "\n"); 
   m_response_queue_size.fprint(fout); fprintf(fout, "\n"); 
   m_validation_queue_size.fprint(fout); fprintf(fout, "\n"); 
   m_commit_queue_size.fprint(fout); fprintf(fout, "\n"); 

   fprintf(fout, "cu_read_set_access_raw = %llu\n", m_read_set_access_raw); 
   fprintf(fout, "cu_read_set_access_coalesced = %llu\n", m_read_set_access_coalesced); 
   fprintf(fout, "cu_write_set_access_raw = %llu\n", m_write_set_access_raw); 
   fprintf(fout, "cu_write_set_access_coalesced = %llu\n", m_write_set_access_coalesced); 
}

void commit_unit_stats::cid_pointer_stats::print(FILE *fout)
{
   for (unsigned int s = 0; s < m_stall_reason.size(); s++) {
      fprintf(fout, "%s_stall_reason[%s] = %u\n", m_name.c_str(), commit_state_str[s], m_stall_reason[s]);
   }
   m_stall_duration.fprint(fout); fprintf(fout, "\n");
}

static commit_unit_stats g_cu_stats; 
static commit_unit_options g_cu_options; 

void commit_unit_reg_options(option_parser_t opp)
{
   g_cu_options.reg_options(opp);
}

void commit_unit_statistics(FILE *fout)
{
   g_cu_stats.print(fout);
}


/////////////////////////////////////////////////////////////////////////////////////////

cu_access_set::cu_access_set()
   : m_linear_buffer_limit(-1), m_bloomfilter(NULL)
{ }

cu_access_set::~cu_access_set()
{
   if (m_bloomfilter) 
      delete m_bloomfilter; 
}

// prematurally deallocate the bloom filter to save memory 
void cu_access_set::delete_bloomfilter()
{
   if (m_bloomfilter) {
      delete m_bloomfilter;
      m_bloomfilter = NULL;
   }
}

bool cu_access_set::overflow() const 
{
   if (m_linear_buffer_limit == -1) return false; 
   return ((int)m_linear_buffer.size() > m_linear_buffer_limit); 
}

// append the linear buffer and update the bloom filter 
void cu_access_set::append(new_addr_type addr)
{
   m_linear_buffer.push_back(addr); 
   if (m_addr_hashtable.find(addr) == m_addr_hashtable.end()) 
      m_addr_hashtable.insert(std::make_pair(addr, -1)); // otherwise just keep the existing version number 
   // update bloom filter 
   if (m_bloomfilter == NULL) {
      m_bloomfilter = new bloomfilter(g_cu_options.m_bloomfilter_size, g_cu_options.m_bloomfilter_func_id, g_cu_options.m_bloomfilter_n_func, false);
   }
   m_bloomfilter->add(addr); 
}

void cu_access_set::reset()
{
   m_linear_buffer.clear(); 
}

// check if this access set buffer contains a given address 
bool cu_access_set::match(new_addr_type addr) const
{
   bool perfect_match; 
   if (g_cu_options.m_fast_match) {
      // search in hash table 
      addr_hashset_t::const_iterator iAddr; 
      iAddr = m_addr_hashtable.find(addr); 

      perfect_match = (iAddr != m_addr_hashtable.end()); 
   } else {
      // search in linear buffer for now 
      linear_buffer_t::const_iterator iAddr; 
      iAddr = std::find(m_linear_buffer.begin(), m_linear_buffer.end(), addr); 

      perfect_match = (iAddr != m_linear_buffer.end()); 
   }

   bool bf_match; 
   if (m_bloomfilter != NULL) {
      bf_match = m_bloomfilter->match(addr); 
   } else {
      bf_match = false; 
   }
   if (bf_match) 
      g_cu_stats.m_bloomfilter_hit += 1; 
   if (perfect_match) {
      assert(bf_match == true); 
   } else {
      if (bf_match) 
         g_cu_stats.m_bloomfilter_false_positive += 1; 
   }

   if (g_cu_options.m_use_bloomfilter) {
      return bf_match; 
   } else {
      return perfect_match; 
   }
}

void cu_access_set::update_version(new_addr_type addr, int version)
{
   addr_hashset_t::iterator iVersion = m_addr_hashtable.find(addr); 
   assert(iVersion != m_addr_hashtable.end()); 
   iVersion->second = version;
}

int cu_access_set::get_version(new_addr_type addr) const
{
   addr_hashset_t::const_iterator iVersion = m_addr_hashtable.find(addr); 
   assert(iVersion != m_addr_hashtable.end()); 
   return iVersion->second; 
}

void cu_access_set::print(FILE *fout) const 
{
   linear_buffer_t::const_iterator iAddr; 
   for (iAddr = m_linear_buffer.begin(); iAddr != m_linear_buffer.end(); ++iAddr) {
      fprintf(fout, "%#08llx(%d) ", *iAddr, m_addr_hashtable.find(*iAddr)->second); 
   }
   fprintf(fout, "\n"); 
}

/////////////////////////////////////////////////////////////////////////////////////////

commit_entry::commit_entry(int commit_id)
   : m_commit_id(commit_id), m_wid(-1), m_sid(-1), m_tpc(-1), 
     m_state(UNUSED), m_n_validation_pending(0), m_fail(false), m_revalidate(false), m_youngest_conflicting_commit_id(-1),
     m_retire_ptr_at_fill(-1), m_n_commit_write_pending(0), 
     m_reply_sent(false), m_final_pass(false),
     m_revalidate_set(false), m_skip_entry(false), m_commit_ack_sent(false), m_delayfcd_reads_checked(0), m_delayfcd_writes_stored(0),
     m_tm_manager(NULL), m_commit_pending_flag(NULL)
{ 
   m_alloc_time = gpu_sim_cycle + gpu_tot_sim_cycle; 
   m_fill_time = 0; 
   m_validation_wait_time = 0;
   m_revalidation_wait_time = 0;
   m_pass_fail_time = 0;
   m_ack_wait_time = 0; 
   m_commit_ready_time = 0;
   m_commit_sent_time = 0;
   m_retired_time = 0; 
   m_timestamp_file = NULL; 
}

void commit_entry::set_tx_origin(int sid, int tpc, int wid) 
{
   if (m_sid == -1) {
      m_sid = sid; 
   } else {
      assert(m_sid == sid); 
   }
   if (m_tpc == -1) {
      m_tpc = tpc; 
   } else {
      assert(m_tpc == tpc); 
   }
   if (wid == -1) return; 
   if (m_wid == -1) {
      m_wid = wid;
   } else {
      assert(m_wid == wid); 
   }
}

void commit_entry::set_state(enum commit_state state)
{
   m_state = state; 

   unsigned time = gpu_sim_cycle + gpu_tot_sim_cycle; 

   switch(state) {
       case FILL:               m_fill_time = time;               break;

       case VALIDATION_WAIT:    m_validation_wait_time = time;    break;
       case REVALIDATION_WAIT:  m_revalidation_wait_time = time;  break;

       case PASS:
       case FAIL:               m_pass_fail_time = time;          break;

       case PASS_ACK_WAIT:      m_ack_wait_time = time;           break;

       case COMMIT_READY:       m_commit_ready_time = time;       break;
       case COMMIT_SENT:        m_commit_sent_time = time;        break;

       case RETIRED:            m_retired_time = time;            break;

       default: break;
   };

   if (state == RETIRED) {
      read_set().delete_bloomfilter(); 
      write_set().delete_bloomfilter(); 
   }
}

void commit_entry::validation_return(bool pass)
{
   m_fail = m_fail or (not pass); 
   m_n_validation_pending -= 1; 
   assert(m_n_validation_pending >= 0); 
}

new_addr_type commit_entry::get_next_delayfcd_read()
{
   const cu_access_set::linear_buffer_t &rs_buffer = read_set().get_linear_buffer();
   assert(m_delayfcd_reads_checked < rs_buffer.size());

   cu_access_set::linear_buffer_t::const_iterator iter = rs_buffer.begin();
   std::advance(iter, m_delayfcd_reads_checked++);

   return *iter;
}

new_addr_type commit_entry::get_next_delayfcd_write()
{
   const cu_access_set::linear_buffer_t &ws_buffer = write_set().get_linear_buffer();
   assert(m_delayfcd_writes_stored < ws_buffer.size());

   cu_access_set::linear_buffer_t::const_iterator iter = ws_buffer.begin();
   std::advance(iter, m_delayfcd_writes_stored++);

   return *iter;
}

void commit_entry::print(FILE *fout)
{
   fprintf(fout, "cid=%d; wst=(%d,%d,%d); state=%s; vp=%d; fail=%c; rv=%c; cp=%d; ", 
           m_commit_id, m_wid, m_sid, m_tpc, commit_state_str[m_state], m_n_validation_pending, 
           ((m_fail)? 't':'f'), ((m_revalidate)? 't':'f'), m_n_commit_write_pending); 
   fprintf(fout, "rs=%zu; ws=%zu; ", read_set().get_linear_buffer().size(), write_set().get_linear_buffer().size()); 
   fprintf(fout, "reply_sent=%c; final_pass=%c; RVset=%c;\n", 
           ((m_reply_sent)? 't':'f'), ((m_final_pass)? 't':'f'), ((m_revalidate_set)? 't':'f')); 
}

void commit_entry::set_tm_manager(class tm_manager_inf* tm_manager)
{
   assert(tm_manager != NULL); 

   if (m_tm_manager == NULL) {
      m_tm_manager = tm_manager; 
      m_tm_manager->inc_ref_count(); 
   } else {
      assert(m_tm_manager == tm_manager); 
   }
}

void commit_entry::delete_tm_manager()
{
   if (m_tm_manager == NULL) return; 

   if (m_tm_manager->dec_ref_count() == 0) {
      delete m_tm_manager; 
      m_tm_manager = NULL; 
   }
}

void commit_entry::dump_timestamps()
{
   if (m_commit_id == 0) return; 
   if (not g_cu_options.m_dump_timestamps) return; 
   assert(m_timestamp_file != NULL); 
   fprintf(m_timestamp_file, "%6d\t(%02d,%02d)\t%d\t", m_commit_id, m_sid, m_wid, m_mpid);
   fprintf(m_timestamp_file, "AC=%-8u FL=%-8u VW=%-8u ", m_alloc_time, m_fill_time, m_validation_wait_time); 
   fprintf(m_timestamp_file, "RW=%-8u PF=%-8u AW=%-8u ", m_revalidation_wait_time, m_pass_fail_time, m_ack_wait_time); 
   fprintf(m_timestamp_file, "CR=%-8u CS=%-8u RT=%-8u ", m_commit_ready_time, m_commit_sent_time, m_retired_time);
   fprintf(m_timestamp_file, "\n"); 
}

/////////////////////////////////////////////////////////////////////////////////////////

warp_commit_entry::warp_commit_entry(int sid, int wid) 
: m_max_commit_id(-1), m_max_commit_id_with_skip(-1), m_sid(sid), m_wid(wid) 
{ 
   m_commit_ids.resize(32);
   m_active_mask.reset(); 
   m_validation_done_mask.reset(); 
   m_commit_ack_pending_mask.reset(); 
   m_commit_done_mask.reset(); 
   m_hazard_detection_read_done_mask.reset(); 
   m_hazard_detection_write_done_mask.reset(); 
}

void warp_commit_entry::reset() 
{
   if (g_tm_options.m_use_logical_timestamp_based_tm == false && g_tm_options.m_eager_warptm_enabled == false) 
      assert(all_validation_done()); 
   
   assert(all_commit_done()); 

   m_commit_ids.clear(); 
   m_max_commit_id = -1; 
   m_max_commit_id_with_skip = -1; 
   m_sid = -1; 
   m_wid = -1; 
   m_active_mask.reset(); 
   m_validation_done_mask.reset(); 
   m_commit_ack_pending_mask.reset(); 
   m_commit_done_mask.reset(); 
   m_hazard_detection_read_done_mask.reset(); 
   m_hazard_detection_write_done_mask.reset(); 
}

void warp_commit_entry::dump(FILE *fout) const
{
   fprintf(fout, "(sid=%d,wid=%d) commit_ids = ", m_sid, m_wid); 
   for (unsigned c = 0; c < m_commit_ids.size(); c++) {
      fprintf(fout, "%d ", m_commit_ids[c]); 
   }
   fprintf(fout, "\n"); 

   fprintf(fout, "active_mask=%08zx validation_mask=%08zx commit_ack_pending_mask=%08zx commit_mask=%08zx\n", 
           m_active_mask.to_ulong(), m_validation_done_mask.to_ulong(), 
           m_commit_ack_pending_mask.to_ulong(), m_commit_done_mask.to_ulong()); 
}

// obtain the "lane" that the given commit entry correspond in the warp 
int warp_commit_entry::get_lane(const commit_entry& cmt_entry) const 
{
   // the commit id in a warp should all be contiguous 
   int lane = cmt_entry.get_commit_id() - m_commit_ids[0]; 
   return lane; 
}

// add commit entry to this warp  
void warp_commit_entry::link_to_commit_id(const commit_entry& cmt_entry)
{
   // verify that this commit entry does belong to the same warp 
   if (m_sid == -1) {
      m_sid = cmt_entry.get_sid(); 
   } else {
      assert(m_sid == cmt_entry.get_sid()); 
   }
   if (m_wid == -1) {
      m_wid = cmt_entry.get_wid(); 
   } else {
      if (cmt_entry.get_wid() != -1) 
         assert(m_wid == cmt_entry.get_wid()); 
   }

   if (cmt_entry.get_state() != RETIRED) { // not a skipped entry 
      m_commit_ids.push_back(cmt_entry.get_commit_id()); 
      int lane = get_lane(cmt_entry); 
      m_active_mask.set(lane); 
      m_max_commit_id = std::max(m_max_commit_id, cmt_entry.get_commit_id()); 
   }
   m_max_commit_id_with_skip = std::max(m_max_commit_id_with_skip, cmt_entry.get_commit_id()); 
}

// inform the warp that this particular entry has finished validation 
void warp_commit_entry::signal_validation_done(const commit_entry& cmt_entry)
{
   int lane = get_lane(cmt_entry); 
   assert(m_active_mask.test(lane) == true); 
   assert(m_validation_done_mask.test(lane) == false); 
   m_validation_done_mask.set(lane); 
}

// inform the warp of the final pass/fail of this particular entry 
void warp_commit_entry::signal_final_outcome(const commit_entry& cmt_entry) 
{
   int lane = get_lane(cmt_entry); 
   assert(m_active_mask.test(lane) == true); 
   assert(m_commit_ack_pending_mask.test(lane) == false); 
   bool commit_ack_pending = cmt_entry.get_final() and (cmt_entry.write_set().get_linear_buffer().size() > 0); 
   m_commit_ack_pending_mask.set(lane, commit_ack_pending); 
}

// inform the warp that this particular entry has finished commit 
void warp_commit_entry::signal_commit_done(const commit_entry& cmt_entry) 
{
   int lane = get_lane(cmt_entry); 
   assert(m_commit_ack_pending_mask.test(lane) == true); 
   assert(m_commit_done_mask.test(lane) == false); 
   m_commit_done_mask.set(lane);
}

bool warp_commit_entry::all_validation_done() const
{
   return (m_active_mask == m_validation_done_mask); 
}

bool warp_commit_entry::all_commit_done() const 
{
   return (m_commit_ack_pending_mask == m_commit_done_mask); 
}

bool warp_commit_entry::commit_ack_pending(const commit_entry& cmt_entry) const
{
   int lane = get_lane(cmt_entry); 
   return (m_commit_ack_pending_mask.test(lane)); 
}


// inform the warp that this entry has finished hazard detection 
void warp_commit_entry::signal_hazard_detection_read_done(const commit_entry& cmt_entry)
{
   int lane = get_lane(cmt_entry); 
   assert(m_active_mask.test(lane) == true); 
   // assert(m_hazard_detection_read_done_mask.test(lane) == false); 
   m_hazard_detection_read_done_mask.set(lane); 
}

// inform the warp that this entry has finished updating conflict table 
void warp_commit_entry::signal_hazard_detection_write_done(const commit_entry& cmt_entry)
{
   int lane = get_lane(cmt_entry); 
   assert(m_active_mask.test(lane) == true); 
   // assert(m_hazard_detection_write_done_mask.test(lane) == false); 
   m_hazard_detection_write_done_mask.set(lane); 
}

bool warp_commit_entry::hazard_detection_reads_done() const
{
   return m_active_mask == m_hazard_detection_read_done_mask; 
}

bool warp_commit_entry::hazard_detection_writes_done() const 
{
   return m_active_mask == m_hazard_detection_write_done_mask; 
}

/////////////////////////////////////////////////////////////////////////////////////////

class commit_unit_mf_allocator : public mem_fetch_allocator {
public:
   commit_unit_mf_allocator( const memory_config *config )
   {
      m_memory_config = config;
   }
   virtual mem_fetch * alloc(const class warp_inst_t &inst, const mem_access_t &access) const 
   {
      abort();
      return NULL;
   }
   virtual mem_fetch * alloc(new_addr_type addr, mem_access_type type, unsigned size, bool wr) const
   {
      return alloc(addr, type, size, wr, -1, -1, -1); 
   }
   virtual mem_fetch * alloc(new_addr_type addr, mem_access_type type, unsigned size, bool wr, 
                             int wid, int sid, int tpc) const
   {
      mem_access_t access( type, addr, size, wr );
      mem_fetch *mf = new mem_fetch( access, 
                                     NULL,
                                     ((wr)? WRITE_PACKET_SIZE : READ_PACKET_SIZE), 
                                     wid, 
                                     sid, 
                                     tpc,
                                     m_memory_config );
      return mf;
   }
private:
   const memory_config *m_memory_config;
};

/////////////////////////////////////////////////////////////////////////////////////////

commit_unit::commit_unit( const memory_config *memory_config, 
                          const shader_core_config *shader_config, 
                          unsigned partition_id, 
                          mem_fetch_interface *port, 
                          std::set<mem_fetch*> &request_tracker, 
                          std::queue<rop_delay_t> &rop2L2 )
   : m_memory_config(memory_config), m_shader_config(shader_config), m_partition_id(partition_id),
     m_conflict_detector(g_cu_options.m_conflict_table_hash_sets,g_cu_options.m_conflict_table_hash_ways,
                         g_cu_options.m_conflict_table_bf_size, g_cu_options.m_conflict_table_bf_n_funcs, 
                         g_cu_options.m_conflict_table_granularity),
     m_response_port(port), m_request_tracker(request_tracker), 
     m_mf_alloc(new commit_unit_mf_allocator(m_memory_config)), m_rop2L2(rop2L2), 
     m_cid_at_head(0), m_cid_fcd(0), m_cid_pass(0), m_cid_retire(0), m_cid_commit(0),
     m_n_tx_read_set(0), m_n_tx_write_set(0), m_n_tx_done_fill(0), m_n_tx_skip(0), m_n_tx_pass(0), m_n_tx_fail(0),
     m_n_input_pkt_processed(0), m_n_recency_bf_activity(0), m_n_reply_sent(0), 
     m_n_validations(0), m_n_validations_processed(0), m_n_commit_writes(0), m_n_commit_writes_processed(0), 
     m_n_revalidations(0), m_sent_icnt_traffic(0), 
     m_n_active_entries(0), m_n_active_entries_have_rs(0), m_n_active_entries_have_ws(0),
     m_n_active_entries_need_rs(0), m_n_active_entries_need_ws(0),
     m_cid_fcd_stall_cycles(0), m_cid_pass_stall_cycles(0), m_cid_commit_stall_cycles(0), m_cid_retire_stall_cycles(0)
{

   m_commit_entry_table.push_back(commit_entry(0)); // empty entry to jump start the structure (commit id 0 is reserved)
   m_commit_entry_table[0].set_state(RETIRED); 
   m_commit_entry_table[0].set_skip(); 
   cycle(0); // to move the pointers to the proper location for busy detection

   if (g_cu_options.m_dump_timestamps) {
      char tfilename[20];
      snprintf(tfilename, sizeof(tfilename), "cu-timeline%d.txt", m_partition_id); 
      m_timestamp_file = fopen(tfilename, "w"); 
   } else {
      m_timestamp_file = NULL; 
   }
}

commit_unit::~commit_unit()
{
   if (m_timestamp_file) 
      fclose(m_timestamp_file); 
}

// process a scalar validation operation returned from L2 cache 
void commit_unit::process_validation_op_reply(mem_fetch *mf, const cu_mem_acc &mem_op, unsigned time)
{
   if (mf != NULL) 
      assert(mf->get_access_type() == GLOBAL_ACC_R); 
   assert(mem_op.has_coalesced_ops() == false); // ensure this is an scalar operation 
   commit_entry &ce = get_commit_entry(mem_op.commit_id);
   // notify the validation result and advance state if this is the last validation 
   bool word_valid = true; 
   if (m_shader_config->timing_mode_vb_commit) {
      if (ce.get_tm_manager()->watched())
         printf("[CID=%d]", mem_op.commit_id); 
      word_valid = ce.get_tm_manager()->validate_addr(mem_op.addr); 
   }

   int existing_commit_id = m_update_done_mem_version[mem_op.addr]; 
   ce.read_set().update_version(mem_op.addr, existing_commit_id); 
   ce.validation_return(word_valid); 
   if (ce.get_state() == VALIDATION_WAIT and ce.validation_pending() == false and ce.get_revalidate() == false) {
      done_validation_wait(ce); 
   } else if(ce.get_state() == REVALIDATION_WAIT and ce.validation_pending() == false) {
      assert(ce.get_revalidate() == false);
      done_revalidation_wait(ce);
   }
   g_cu_stats.m_validation_latency.add2bin(time - mem_op.issue_cycle); 
   m_n_validations_processed++; 
}

// process a scalar commit operation returned from L2 cache 
void commit_unit::process_commit_op_reply(mem_fetch *mf, const cu_mem_acc &mem_op, unsigned time)
{
   if (mf != NULL) 
      assert(mf->get_access_type() == GLOBAL_ACC_W); 
   assert(mem_op.has_coalesced_ops() == false); // ensure this is an scalar operation 
   commit_entry &ce = get_commit_entry(mem_op.commit_id);
   // data race detection for debugging 
   check_read_set_version(ce); 
   // notify the commit update and retire transaction if this is the last update 
   addr_t commit_addr = mem_op.addr; 
   int existing_commit_id = m_update_done_mem_version[commit_addr]; 
   if (g_tm_options.m_eager_warptm_enabled == false) {
       // in eager WarpTM, all transaction will be validated and committed in the core side
       // this order does not matter any more
       assert(mem_op.commit_id >= existing_commit_id); // this can only go up, and rewrite within same tx is possible
   }
   if (m_shader_config->timing_mode_vb_commit) {
      if (ce.get_tm_manager()->watched())
         printf("[CID=%d]", mem_op.commit_id); 
      ce.get_tm_manager()->commit_addr(mem_op.addr); 
   }
  
   if (g_tm_options.m_eager_warptm_enabled) {
       temporal_conflict_detector::get_singleton().update_word(mem_op.addr >> 2, gpu_sim_cycle + gpu_tot_sim_cycle);
   }

   m_update_done_mem_version[commit_addr] = mem_op.commit_id; 
   ce.commit_write_done(); 
   if (ce.get_state() == COMMIT_SENT and ce.commit_write_pending() == false) {
      ce.set_state(RETIRED); 
      commit_done_ack(ce); 
   }
   g_cu_stats.m_commit_latency.add2bin(time - mem_op.issue_cycle); 
   m_n_commit_writes_processed++; 
}

// snoop reply from memory partition 
// return true if mf is processed 
bool commit_unit::snoop_mem_fetch_reply(mem_fetch *mf)
{
   // ignore any mf that is not generated by commit unit itself 
   if (mf->get_commit_unit_generated() == false) return false; 

   unsigned time = gpu_sim_cycle + gpu_tot_sim_cycle; 

   cu_mem_fetch_lookup::iterator iop = m_cu_mem_fetch_fields.find(mf); 
   assert(iop != m_cu_mem_fetch_fields.end()); 

   switch (iop->second.operation) {
   case VALIDATE: {
         if (iop->second.has_coalesced_ops()) {
            while (iop->second.has_coalesced_ops()) {
               cu_mem_acc &scalar_mem_op = iop->second.next_coalesced_op(); 
               process_validation_op_reply(mf, scalar_mem_op, time); 
               iop->second.pop_coalesced_op(); 
            } 
         } else {
            process_validation_op_reply(mf, iop->second, time); 
         }
      } break;
   case COMMIT_WRITE: {
         if (iop->second.has_coalesced_ops()) {
            while (iop->second.has_coalesced_ops()) {
               cu_mem_acc &scalar_mem_op = iop->second.next_coalesced_op(); 
               process_commit_op_reply(mf, scalar_mem_op, time); 
               iop->second.pop_coalesced_op(); 
            } 
         } else {
            process_commit_op_reply(mf, iop->second, time); 
         }
      } break;
   default: assert(0); 
   }

   m_cu_mem_fetch_fields.erase(iop); 

   return true; 
}

// send a mem_fetch to L2 via the ROP path for validation or commit write
void commit_unit::send_to_L2(unsigned long long time, commit_unit::cu_mem_acc mem_op)
{
   // create the memory request 
   const commit_entry &ce = get_commit_entry(mem_op.commit_id); 
   assert(mem_op.operation != NON_CU_OP); 
   assert(mem_op.size == 4 or mem_op.size % g_cu_options.m_coalesce_block_size == 0); 
   mem_fetch *req = m_mf_alloc->alloc( mem_op.addr, 
                                       ((mem_op.operation == VALIDATE)? GLOBAL_ACC_R : GLOBAL_ACC_W), 
                                       mem_op.size, 
                                       (mem_op.operation == COMMIT_WRITE),
                                       ce.get_wid(),
                                       ce.get_sid(), 
                                       ce.get_tpc() ); 
   m_cu_mem_fetch_fields[req] = mem_op; 
   req->set_commit_unit_generated(); 
   req->set_commit_id(mem_op.commit_id); 

   assert(req->get_sub_partition_id() == m_partition_id); 

   m_request_tracker.insert(req); 

   // push mem_fetch into ROP delay queue 
   rop_delay_t r;
   r.req = req;
   r.ready_cycle = time + g_cu_options.m_rop_latency; // Add 115*4=460 delay cycles
   m_rop2L2.push(r);
   req->set_status(IN_PARTITION_ROP_DELAY, time);

   // TODO: flow control
}

void commit_unit::check_read_set_version(const commit_entry &ce)
{
   if (g_cu_options.m_check_read_set_version == false) return;

   const cu_access_set::linear_buffer_t &rs_buffer = ce.read_set().get_linear_buffer(); 
   cu_access_set::linear_buffer_t::const_iterator iAddr; 

   for (iAddr = rs_buffer.begin(); iAddr != rs_buffer.end(); ++iAddr) {
      int existing_version = m_update_done_mem_version[*iAddr]; 
      int buffered_version = ce.read_set().get_version(*iAddr); 
      if (existing_version != buffered_version and existing_version != ce.get_commit_id()) {
         fprintf(stdout, "[CommUnit] Data race detected @ [%#08llx] for CID=%d: existing version=%d buffered_version=%d\n", 
                 *iAddr, ce.get_commit_id(), existing_version, buffered_version); 
         assert(existing_version == buffered_version); 
      }
   }
}

#define TX_PACKET_SIZE 8
// process queued work
void commit_unit::cycle(unsigned long long time)
{
   // e.g., send an abort message
   if( !m_response_queue.empty() ) {
      if( !m_response_port->full(TX_PACKET_SIZE,0) ) {
         mem_fetch *mf = m_response_queue.front();
         m_response_port->push(mf);
         m_sent_icnt_traffic += mf->get_num_flits(false); 
         m_response_queue.pop_front();
         m_n_reply_sent++; 
      }
   }

   // stub to drain the commit queue 
   if( !m_commit_queue.empty() ) {
      cu_mem_acc &mem_op = m_commit_queue.front(); 
      assert(mem_op.operation == COMMIT_WRITE); 

      if (g_cu_options.m_gen_L2_acc & 0x2) {
         send_to_L2(time, mem_op); 
      } else {
         if (mem_op.has_coalesced_ops()) {
            while (mem_op.has_coalesced_ops()) {
               cu_mem_acc &scalar_mem_op = mem_op.next_coalesced_op(); 
               process_commit_op_reply(NULL, scalar_mem_op, time); 
               mem_op.pop_coalesced_op(); 
            } 
         } else {
            process_commit_op_reply(NULL, mem_op, time); 
         }
      }

      m_n_commit_writes++; 
      m_commit_queue.pop_front(); 
   }
   g_cu_stats.m_commit_queue_size.add2bin(m_commit_queue.size()); 

   // stub to drain the validation queue and assume that they are all validated (and pass)
   if( !m_validation_queue.empty() ) {
      if (g_tm_options.m_eager_warptm_enabled) assert(false);

      cu_mem_acc &mem_op = m_validation_queue.front(); 
      assert(mem_op.operation == VALIDATE); 

      if (g_cu_options.m_gen_L2_acc & 0x1) {
         send_to_L2(time, mem_op); 
      } else {
         if (mem_op.has_coalesced_ops()) {
            while (mem_op.has_coalesced_ops()) {
               cu_mem_acc &scalar_mem_op = mem_op.next_coalesced_op(); 
               process_validation_op_reply(NULL, scalar_mem_op, time); 
               mem_op.pop_coalesced_op(); 
            } 
         } else {
            process_validation_op_reply(NULL, mem_op, time); 
         }
      }

      m_n_validations++; 
      m_validation_queue.pop_front(); 
   }
   g_cu_stats.m_validation_queue_size.add2bin(m_validation_queue.size()); 

   // entry pointers and state management 
   if (g_cu_options.m_vwait_nostall) {
      if (g_tm_options.m_eager_warptm_enabled) assert(false && "Not supported yet!");

      if(g_cu_options.m_fcd_mode == 1) {
         for (unsigned int a = 0; a < g_cu_options.m_overclock_hazard_detect; a++) {
            if (g_cu_options.m_warp_level_hazard_detect) {
               check_and_advance_fcd_ptr_warp_level(time); 
            } else {
               check_and_advance_fcd_ptr(time);
            }
         }
      }
      check_and_advance_pass_ptr_vwait_nostall(time); 
      check_and_advance_commit_ptr_vwait_nostall(time); 
      check_and_advance_retire_ptr_vwait_nostall(time); 
   } else {
      if (g_tm_options.m_eager_warptm_enabled == false) { 
          if(g_cu_options.m_fcd_mode == 1) {
             for (unsigned int a = 0; a < g_cu_options.m_overclock_hazard_detect; a++) {
                if (g_cu_options.m_warp_level_hazard_detect) {
                   check_and_advance_fcd_ptr_warp_level(time); 
                } else {
                   check_and_advance_fcd_ptr(time);
                }
             }
          }
          check_and_advance_pass_ptr(time); 
      }
      check_and_advance_commit_ptr(time); 
      check_and_advance_retire_ptr(time); 
   }

   if (m_cid_retire <= m_cid_at_head) 
      g_cu_stats.m_distance_retire_head.add2bin(m_cid_at_head - m_cid_retire); 
   else 
      g_cu_stats.m_distance_retire_head.add2bin(0);

   // Distance between pass and fcd - only count it if fcd is blocked by fill/unused (and not by head)
   if(g_cu_options.m_fcd_mode == 1) {
      if(m_cid_pass <= m_cid_at_head)
         g_cu_stats.m_distance_fcd_pass.add2bin(m_cid_fcd - m_cid_pass);
   }

   assert(m_n_active_entries_have_rs <= m_n_active_entries);
   assert(m_n_active_entries_have_ws <= m_n_active_entries);
   assert(m_n_active_entries_need_rs <= m_n_active_entries_have_rs);
   assert(m_n_active_entries_need_ws <= m_n_active_entries_have_ws);

   g_cu_stats.m_active_entries.add2bin(m_n_active_entries);
   g_cu_stats.m_active_entries_have_rs.add2bin(m_n_active_entries_have_rs);
   g_cu_stats.m_active_entries_have_ws.add2bin(m_n_active_entries_have_ws);
   g_cu_stats.m_active_entries_need_rs.add2bin(m_n_active_entries_need_rs);
   g_cu_stats.m_active_entries_need_ws.add2bin(m_n_active_entries_need_ws);

   if(g_cu_options.m_fcd_mode == 1)
      g_cu_stats.m_conflict_table_size.add2bin(m_conflict_detector.size());


   // process input messages 
   if (not m_input_queue.empty()) {
      mem_fetch *input_msg = m_input_queue.front(); 
      if (input_msg->has_coalesced_packet()) {
         if (g_cu_options.m_parallel_process_coalesced_input == true) {
            process_coalesced_input_parallel(input_msg, time);
         } else {
            process_coalesced_input_serial(input_msg, time); 
	    if (g_tm_options.m_eager_warptm_enabled == false)
                transfer_ops_to_queue(VALIDATE); 
         }
      } else {
         process_input( input_msg, time );
	 if (g_tm_options.m_eager_warptm_enabled) {
             transfer_ops_to_queue(VALIDATE); 
	 }
         m_input_queue.pop_front();
	 if (g_tm_options.m_lsu_hpca_enabled) {
	    broadcast_newly_inserted_addr();
	 } 
      }
      m_n_input_pkt_processed++;
   }
   g_cu_stats.m_input_queue_size.add2bin(m_input_queue.size()); 
   g_cu_stats.m_response_queue_size.add2bin(m_response_queue.size()); 

   if ( time % 1000 == 0 ) 
      scrub_retired_commit_entries(); 
}

// process coalesced input messages in serial 
void commit_unit::process_coalesced_input_serial( mem_fetch *input_msg, unsigned time )
{
   mem_fetch *input_cmsg = input_msg->next_coalesced_packet(); 
   process_input( input_cmsg, time ); 
   input_msg->pop_coalesced_packet(); 
   if (input_msg->has_coalesced_packet() == false) {
      delete input_msg; 
      m_input_queue.pop_front();
      if (g_tm_options.m_lsu_hpca_enabled) {
         broadcast_newly_inserted_addr();
      } 
   }
}

// return the warp commit entry for warp <warp_id> at core <core_id>
warp_commit_entry & commit_unit::get_warp_commit_entry( int core_id, int warp_id )
{
   return m_warp_commit_entry_table[std::make_pair(core_id, warp_id)]; 
}

// return the warp_commit_entry corresponding to the core/warp of the message 
warp_commit_entry & commit_unit::get_warp_commit_entry_for_msg( mem_fetch *input_msg, unsigned time )
{
   std::list<mem_fetch*>& packets = input_msg->get_coalesced_packet_list(); 
   assert(packets.size() > 0); 

   std::list<mem_fetch*>::const_iterator msg = packets.begin(); 
   int sid = (*msg)->get_sid(); 
   int wid = (*msg)->get_wid(); 

   warp_commit_entry & wcmt_entry = get_warp_commit_entry(sid,wid); 

   return wcmt_entry; 
}

// transfer the memory operations from m_<mem_op>_coalescing_queue to validation or commit queue 
void commit_unit::transfer_ops_to_queue(enum cu_mem_op operation)
{
   assert(operation != NON_CU_OP); 
   mem_op_queue_t & coalescing_queue = (operation == VALIDATE)? m_validation_coalescing_queue : m_commit_coalescing_queue; 
   mem_op_queue_t & mem_op_queue = (operation == VALIDATE)? m_validation_queue : m_commit_queue; 
   for (auto iMemOp = coalescing_queue.begin(); iMemOp != coalescing_queue.end(); ++iMemOp) {
      assert( iMemOp->operation == operation );
      mem_op_queue.push_back(*iMemOp); 
   }
   coalescing_queue.clear(); 
}

void commit_unit::cu_mem_acc::append_coalesced_op(new_addr_type block_addr, cu_mem_acc &mem_op) 
{
   const new_addr_type block_size = g_cu_options.m_coalesce_block_size; 
   if (operation == NON_CU_OP) {
      commit_id = mem_op.commit_id; 
      addr = block_addr; 
      operation = mem_op.operation; 
      issue_cycle = mem_op.issue_cycle; 
      assert(m_coalesced_ops.empty()); 
      set_size(block_size); 
   } else {
      assert(addr == block_addr); 
      assert(operation == mem_op.operation); 
   }
   assert(mem_op.m_coalesced_ops.empty()); 
   m_coalesced_ops.push_back(mem_op); 
}

// coalesce the memory operations from m_mem_op_coalescing_queue and transfer the coalesced operations to validation or commit queue 
void commit_unit::coalesce_ops_to_queue(enum cu_mem_op operation)
{
   const new_addr_type block_size = g_cu_options.m_coalesce_block_size; 
   assert(operation != NON_CU_OP); 
   mem_op_queue_t & coalescing_queue = (operation == VALIDATE)? m_validation_coalescing_queue : m_commit_coalescing_queue; 
   mem_op_queue_t & mem_op_queue = (operation == VALIDATE)? m_validation_queue : m_commit_queue; 
   std::map<new_addr_type, cu_mem_acc> mem_op_map; 
   // just coalesce memory operations into blocks for now 
   for (auto iMemOp = coalescing_queue.begin(); iMemOp != coalescing_queue.end(); ++iMemOp) {
      new_addr_type block_addr = iMemOp->addr & ~(block_size-1); 
      assert(operation == iMemOp->operation); 
      mem_op_map[block_addr].append_coalesced_op(block_addr, *iMemOp); 

      addrdec_t decoded_addr; 
      m_memory_config->m_address_mapping.addrdec_tlx(iMemOp->addr,&decoded_addr);
      assert(decoded_addr.sub_partition == m_partition_id); 
   }
   coalescing_queue.clear(); 

   // push the coalesced operations into the actual queues 
   for (auto iCMemOp = mem_op_map.begin(); iCMemOp != mem_op_map.end(); ++iCMemOp) {
      mem_op_queue.push_back(iCMemOp->second); 
   }
}

// process coalesced input messages in parallel 
void commit_unit::process_coalesced_input_parallel( mem_fetch *input_msg, unsigned time )
{
   const bool serialize_tx_read_write_set = false; 
   const bool serialize_tx_done_fill = false; 
   const bool serialize_tx_pass_fail = false; 

   enum mf_type  access_type = input_msg->get_type();
   switch(access_type) {
   case TX_CU_ALLOC: assert(0); break; 
   case TX_READ_SET:
   case TX_WRITE_SET: {
         // estimate coalescing ratio 
         if (input_msg->partial_processed_packet() == false) {
            std::list<mem_fetch*>& packets = input_msg->get_coalesced_packet_list(); 
            unsigned raw_access_count = packets.size(); 
            unsigned coalesced_access_count = num_coalesced_accesses(packets); 
            if (access_type == TX_READ_SET) {
               g_cu_stats.m_read_set_access_raw += raw_access_count; 
               g_cu_stats.m_read_set_access_coalesced += coalesced_access_count; 
            } else {
               assert(access_type == TX_WRITE_SET); 
               g_cu_stats.m_write_set_access_raw += raw_access_count; 
               g_cu_stats.m_write_set_access_coalesced += coalesced_access_count; 
            }
         }
         // process packet - memory operations are sent to coalescing queue
         if (serialize_tx_read_write_set) {
            process_coalesced_input_serial(input_msg, time); 
         } else {
            while (input_msg->has_coalesced_packet()) {
               mem_fetch *input_cmsg = input_msg->next_coalesced_packet(); 
               process_input( input_cmsg, time ); 

               input_msg->pop_coalesced_packet(); 
            }
            delete input_msg; 
            m_input_queue.pop_front(); 
         }
         // transfer memory operations from coalescing queue to actual queues
	 if (g_tm_options.m_use_logical_timestamp_based_tm || g_tm_options.m_eager_warptm_enabled) {
	     // No validation in logical timestamp based tm manager
	 } else {
             if (g_cu_options.m_coalesce_mem_op) {
                coalesce_ops_to_queue(VALIDATE); 
             } else {
                transfer_ops_to_queue(VALIDATE); 
             }
	 }
      } break; 
   case TX_DONE_FILL:
   case TX_SKIP: {
         if (serialize_tx_done_fill) {
            process_coalesced_input_serial(input_msg, time); 
         } else {
            warp_commit_entry & wcmt_entry = get_warp_commit_entry_for_msg(input_msg, time); 
            wcmt_entry.reset(); // clean out previous warp
            while (input_msg->has_coalesced_packet()) {
               mem_fetch *input_cmsg = input_msg->next_coalesced_packet(); 
               int cmsg_commit_id = input_cmsg->get_transaction_id(); // obtain commit id before cmsg is deleted
               process_input( input_cmsg, time ); 

               commit_entry& cmt_entry = get_commit_entry( cmsg_commit_id ); 
               wcmt_entry.link_to_commit_id(cmt_entry);

	       if (g_tm_options.m_lsu_hpca_enabled) { 
	          newly_inserted_addr_union(cmt_entry);
	       }
	       
               if ((m_shader_config->tlw_use_logical_temporal_cd || g_tm_options.m_eager_warptm_enabled) && cmt_entry.get_final()) {
                  wcmt_entry.signal_final_outcome(cmt_entry); 
	       }

               input_msg->pop_coalesced_packet(); 
            }
            delete input_msg; 
            m_input_queue.pop_front();
	    if (g_tm_options.m_lsu_hpca_enabled) {
	       broadcast_newly_inserted_addr(); 
	    }
         }
      } break; 
   case TX_PASS:
   case TX_FAIL: {
	 if (g_tm_options.m_eager_warptm_enabled) assert(false);
         if (serialize_tx_pass_fail) {
            process_coalesced_input_serial(input_msg, time); 
         } else {
            warp_commit_entry & wcmt_entry = get_warp_commit_entry_for_msg(input_msg, time); 
            while (input_msg->has_coalesced_packet()) {
               mem_fetch *input_cmsg = input_msg->next_coalesced_packet(); 
               int cmsg_commit_id = input_cmsg->get_transaction_id(); // obtain commit id before cmsg is deleted
               process_input( input_cmsg, time ); 

               commit_entry& cmt_entry = get_commit_entry( cmsg_commit_id ); 
               wcmt_entry.signal_final_outcome(cmt_entry); 

               input_msg->pop_coalesced_packet(); 
            }
            delete input_msg; 
            m_input_queue.pop_front(); 
         }
      } break; 
   default:
      assert(0); 
      break;
   }
}

// return the number of coalesced access generated from a given group of packets 
unsigned commit_unit::num_coalesced_accesses( const std::list<mem_fetch*>& packets ) 
{
   const new_addr_type block_size = 128; 
   std::map<new_addr_type, unsigned> accessed_blocks; 

   std::list<mem_fetch*>::const_iterator iPacket; 
   for (iPacket = packets.cbegin(); iPacket != packets.cend(); ++iPacket) {
      new_addr_type block_addr = (*iPacket)->get_addr() & ~(block_size-1); 
      accessed_blocks[block_addr] += 1; 
   }
   return accessed_blocks.size(); 
}

void commit_unit::check_and_advance_fcd_ptr(unsigned long long time)
{
   // check if the oldest commit id has finished conflict detection
   assert(m_cid_pass <= m_cid_fcd);
   assert(g_cu_options.m_fcd_mode == 1);
   bool cid_fcd_inc = false;
   if(m_cid_fcd <= m_cid_at_head) {
      commit_entry &oldest_ce = get_commit_entry(m_cid_fcd);
      if(oldest_ce.get_state() == VALIDATION_WAIT || oldest_ce.get_state() == PASS) {

         if(!oldest_ce.delayfcd_reads_done()) {
            // First, check the reads against conflict table
            int youngest_conflicting_cid;
            new_addr_type read_to_check = oldest_ce.get_next_delayfcd_read();
            if( m_conflict_detector.check_read_conflict(read_to_check, oldest_ce.get_commit_id(), oldest_ce.get_retire_cid_at_fill(), youngest_conflicting_cid) ) {
               update_youngest_conflicting_commit_id(youngest_conflicting_cid, oldest_ce);
               oldest_ce.set_revalidate(true);
               if(oldest_ce.get_state() == PASS)
                  oldest_ce.set_state(VALIDATION_WAIT);
            }
            m_n_recency_bf_activity++; 
         } else if (!oldest_ce.delayfcd_writes_done()) {
            // Second, store writes into conflict table
            new_addr_type write_to_store = oldest_ce.get_next_delayfcd_write();
            m_conflict_detector.store_write(write_to_store, oldest_ce.get_commit_id());
            m_n_recency_bf_activity++; 
         } else {
            // Both readset and writeset are dealt with
            m_cid_fcd++;
            cid_fcd_inc = true;
         }
      } else if (oldest_ce.get_state() == RETIRED || oldest_ce.get_state() == FAIL) {
         m_cid_fcd++;
         cid_fcd_inc = true;
      }

      if(cid_fcd_inc) {
         g_cu_stats.m_cid_fcd_stats.m_stall_duration.add2bin(m_cid_fcd_stall_cycles);
         m_cid_fcd_stall_cycles = 0;
      } else {
         enum commit_state fcd_ptr_state = oldest_ce.get_state(); 
         if ((fcd_ptr_state == VALIDATION_WAIT or fcd_ptr_state == PASS) and 
             (!oldest_ce.delayfcd_reads_done() or !oldest_ce.delayfcd_writes_done())) 
         {
            fcd_ptr_state = HAZARD_DETECT; 
         } 
         g_cu_stats.m_cid_fcd_stats.m_stall_reason[fcd_ptr_state] += 1;
         m_cid_fcd_stall_cycles += 1;
      }
   } else {
      g_cu_stats.m_cid_fcd_stats.m_stall_reason[UNUSED] += 1;
      m_cid_fcd_stall_cycles += 1;
   }
}

// warp-level hazard detection that assume no hazard within a warp 
void commit_unit::check_and_advance_fcd_ptr_warp_level(unsigned long long time)
{
   // check if the oldest commit id has finished conflict detection
   assert(m_cid_pass <= m_cid_fcd);
   assert(g_cu_options.m_fcd_mode == 1);
   bool cid_fcd_inc = false;

   // check for conditions that stalls hazard detection 
   if (m_cid_fcd > m_cid_at_head) {
      g_cu_stats.m_cid_fcd_stats.m_stall_reason[UNUSED] += 1;
      m_cid_fcd_stall_cycles += 1;
      return; 
   }

   // advance m_cid_fcd beyond the initial position once the head pointer start moving 
   if (m_cid_fcd == 0) {
      m_cid_fcd++; 
      m_cid_fcd_stall_cycles = 0;
      return; 
   }

   // check for conditions that stalls hazard detection -- entry in FILL or UNUSED state 
   commit_entry &fcd_ce = get_commit_entry(m_cid_fcd);
   if (fcd_ce.get_state() == UNUSED or fcd_ce.get_state() == FILL) {
      g_cu_stats.m_cid_fcd_stats.m_stall_reason[fcd_ce.get_state()] += 1;
      m_cid_fcd_stall_cycles += 1;
      return; 
   } 

   // the whole warp should have received FILL_DONE message together 
   int sid = fcd_ce.get_sid(); 
   int wid = fcd_ce.get_wid(); 
   warp_commit_entry &wcmt_entry = get_warp_commit_entry(sid, wid); 
   #if 0
   int max_cid_in_warp = wcmt_entry.get_max_commit_id(); 
   commit_entry &max_ce_in_warp = get_commit_entry(max_cid_in_warp); 
   enum commit_state max_ce_state = max_ce_in_warp.get_state(); 
   assert(max_ce_state != UNUSED and max_ce_state != FILL); 
   #endif

   const std::vector<int> &m_fcd_cids = wcmt_entry.get_commit_ids(); 

   if (not wcmt_entry.hazard_detection_reads_done()) {
      // allow each thread in warp to perform one hazard detection 
      for (auto i_cid = m_fcd_cids.begin(); i_cid != m_fcd_cids.end(); ++i_cid) {
         commit_entry &hzd_ce = get_commit_entry(*i_cid); 
         if (hzd_ce.get_state() == VALIDATION_WAIT or hzd_ce.get_state() == PASS) {
            if(not hzd_ce.delayfcd_reads_done()) {
               // check the reads against conflict table
               int youngest_conflicting_cid;
               new_addr_type read_to_check = hzd_ce.get_next_delayfcd_read();
               if( m_conflict_detector.check_read_conflict(read_to_check, hzd_ce.get_commit_id(), 
                                                           hzd_ce.get_retire_cid_at_fill(), youngest_conflicting_cid) ) 
               {
                  update_youngest_conflicting_commit_id(youngest_conflicting_cid, hzd_ce);
                  hzd_ce.set_revalidate(true);
                  if(hzd_ce.get_state() == PASS)
                     hzd_ce.set_state(VALIDATION_WAIT);
               }
               m_n_recency_bf_activity++; 
            }
            if(hzd_ce.delayfcd_reads_done()) {
               wcmt_entry.signal_hazard_detection_read_done(hzd_ce); 
            }
         } else {
            wcmt_entry.signal_hazard_detection_read_done(hzd_ce); 
         }
      }
   } else if (not wcmt_entry.hazard_detection_writes_done()) {
      // allow each thread in warp to update one entry in conflict table 
      for (auto i_cid = m_fcd_cids.begin(); i_cid != m_fcd_cids.end(); ++i_cid) {
         commit_entry &hzd_ce = get_commit_entry(*i_cid); 
         if (hzd_ce.get_state() == VALIDATION_WAIT or hzd_ce.get_state() == PASS) {
            if(not hzd_ce.delayfcd_writes_done()) {
               // store writes into conflict table
               new_addr_type write_to_store = hzd_ce.get_next_delayfcd_write();
               m_conflict_detector.store_write(write_to_store, hzd_ce.get_commit_id());
               m_n_recency_bf_activity++; 
            } 
            if(hzd_ce.delayfcd_writes_done()) {
               wcmt_entry.signal_hazard_detection_write_done(hzd_ce); 
            }
         } else {
            wcmt_entry.signal_hazard_detection_write_done(hzd_ce); 
         }
      }
   } 

   // advance hazard detection pointer if the whole warp is done 
   if (wcmt_entry.hazard_detection_reads_done() and wcmt_entry.hazard_detection_writes_done()) {
      int max_cid_with_skip = wcmt_entry.get_max_commit_id_with_skip(); 
      assert(m_cid_fcd <= max_cid_with_skip); 
      m_cid_fcd = max_cid_with_skip + 1; 
      cid_fcd_inc = true;
   }

   if(cid_fcd_inc) {
      g_cu_stats.m_cid_fcd_stats.m_stall_duration.add2bin(m_cid_fcd_stall_cycles);
      m_cid_fcd_stall_cycles = 0;
   } else {
      g_cu_stats.m_cid_fcd_stats.m_stall_reason[HAZARD_DETECT] += 1;
      m_cid_fcd_stall_cycles += 1;
   }
}

void commit_unit::check_and_advance_pass_ptr(unsigned long long time) 
{
   // check if the oldest commit id is ready to send reply
   if (
         (g_cu_options.m_fcd_mode == 0 && m_cid_pass <= m_cid_at_head) ||
         (g_cu_options.m_fcd_mode == 1 && m_cid_pass < m_cid_fcd)
   ) {
      bool cid_pass_inc = false; 
      commit_entry &oldest_ce = get_commit_entry(m_cid_pass); 
      if (oldest_ce.get_state() == PASS) {
         assert(oldest_ce.get_revalidate() == false);
         send_reply(oldest_ce.get_sid(), oldest_ce.get_tpc(), oldest_ce.get_wid(), oldest_ce.get_commit_id(), CU_PASS); 
         oldest_ce.send_reply(); 
         oldest_ce.set_state(PASS_ACK_WAIT);
         // PASSed state no longer needs RS
         if(oldest_ce.read_set().linear_buffer_usage() > 0)
            m_n_active_entries_need_rs -= 1;
         assert(m_n_active_entries_need_rs >= 0);

         // TODO: check for overflow and need for revalidate 
         m_cid_pass++;
         cid_pass_inc = true; 
      } else if (oldest_ce.get_state() == FAIL) {
         send_reply(oldest_ce.get_sid(), oldest_ce.get_tpc(), oldest_ce.get_wid(), oldest_ce.get_commit_id(), CU_FAIL); 
         oldest_ce.send_reply(); 
         oldest_ce.set_state(RETIRED); 
         m_cid_pass++;
         cid_pass_inc = true; 
      } else if (oldest_ce.get_state() == RETIRED) {
         m_cid_pass++;
         cid_pass_inc = true; 
      } else if (oldest_ce.get_state() == VALIDATION_WAIT) {
         // If youngest conflicting entry has committed and need to revalidate, advance to revalidate
         // TODO: optimization - if in the middle of first validation (and no conflicting entry, by design), then change to revalidate
         // serialized validation
         if(m_cid_retire > oldest_ce.get_youngest_conflicting_commit_id() && oldest_ce.get_revalidate() == true) {
            revalidation(oldest_ce);
            m_cid_pass++;
            cid_pass_inc = true; 
         }
      }

      if (cid_pass_inc) {
         g_cu_stats.m_cid_pass_stats.m_stall_duration.add2bin(m_cid_pass_stall_cycles); 
         m_cid_pass_stall_cycles = 0;
      } else {
         enum commit_state pass_ptr_state = oldest_ce.get_state(); 
         if (!oldest_ce.delayfcd_reads_done() or !oldest_ce.delayfcd_writes_done()) {
            pass_ptr_state = HAZARD_DETECT; 
         } 
         g_cu_stats.m_cid_pass_stats.m_stall_reason[pass_ptr_state] += 1; 
         m_cid_pass_stall_cycles += 1; 
      }
   } else {
      g_cu_stats.m_cid_pass_stats.m_stall_reason[UNUSED] += 1; 
      m_cid_pass_stall_cycles += 1; 
   }

}

void commit_unit::check_and_advance_commit_ptr(unsigned long long time)
{
   // check if oldest commit id is ready to commit
   bool test = false;
   if (g_tm_options.m_eager_warptm_enabled) {
       test = m_cid_commit <= m_cid_at_head;
   } else {
       test = m_cid_commit < m_cid_pass;
   }

   if(test) {
      assert(m_cid_commit <= m_cid_at_head); 
      bool cid_commit_inc = false; 
      commit_entry &ce_committing = get_commit_entry(m_cid_commit);
      if(ce_committing.get_state() == COMMIT_READY) {
         start_commit_state(ce_committing);
         m_cid_commit++;
         cid_commit_inc = true; 
      } else if (ce_committing.get_state() == RETIRED) {
         m_cid_commit++;
         cid_commit_inc = true; 
      }
      if (g_cu_options.m_coalesce_mem_op) {
         // wait until all the memory ops are in the coalescing queue 
         int sid = ce_committing.get_sid(); 
         int wid = ce_committing.get_wid(); 
         warp_commit_entry &wcmt_entry = get_warp_commit_entry(sid, wid); 
         if (m_cid_commit > wcmt_entry.get_max_commit_id())
            coalesce_ops_to_queue(COMMIT_WRITE); 
      } else {
         transfer_ops_to_queue(COMMIT_WRITE); 
      }

      if (cid_commit_inc) {
         g_cu_stats.m_cid_commit_stats.m_stall_duration.add2bin(m_cid_commit_stall_cycles); 
         m_cid_commit_stall_cycles = 0;
      } else {
         g_cu_stats.m_cid_commit_stats.m_stall_reason[ce_committing.get_state()] += 1; 
         m_cid_commit_stall_cycles += 1;
      }
   } else {
      g_cu_stats.m_cid_commit_stats.m_stall_reason[UNUSED] += 1; 
      m_cid_commit_stall_cycles += 1;
   }

}

void commit_unit::check_and_advance_retire_ptr(unsigned long long time)
{
   // advance retire pointer to the oldest committing transaction
   if (g_tm_options.m_eager_warptm_enabled) {
       if (m_cid_retire <= m_cid_commit and m_cid_retire <= m_cid_at_head) {
          bool cid_retire_inc = false; 
          commit_entry &ce_retiring = get_commit_entry(m_cid_retire); 
          if (ce_retiring.get_state() == RETIRED) {
             ce_retiring.delete_tm_manager(); 

             m_cid_retire++; // advance through retired commits 
             cid_retire_inc = true;

             if (not ce_retiring.was_skip()) {
                // this entry was active 
                m_n_active_entries -= 1; 

                // this entry had non-empty read-set
                if(ce_retiring.read_set().linear_buffer_usage() > 0)
                   m_n_active_entries_have_rs -= 1;

                // this entry had non-empty write-set
                if(ce_retiring.write_set().linear_buffer_usage() > 0)
                   m_n_active_entries_have_ws -= 1;

                // if entry needed a ws and PASSed
                assert(ce_retiring.fail() == false);
                if(ce_retiring.write_set().linear_buffer_usage() > 0)
                   m_n_active_entries_need_ws -= 1;

                assert(m_n_active_entries_need_rs <= m_n_active_entries_have_rs);
                assert(m_n_active_entries_need_ws <= m_n_active_entries_have_ws);

                assert(m_n_active_entries >= 0);
                assert(m_n_active_entries_have_rs >= 0);
                assert(m_n_active_entries_have_ws >= 0);
                assert(m_n_active_entries_need_rs >= 0);
                assert(m_n_active_entries_need_ws >= 0);
             }
          }

          if (cid_retire_inc) {
             g_cu_stats.m_cid_retire_stats.m_stall_duration.add2bin(m_cid_retire_stall_cycles); 
             m_cid_retire_stall_cycles = 0; 
          } else {
             g_cu_stats.m_cid_retire_stats.m_stall_reason[ce_retiring.get_state()] += 1; 
             m_cid_retire_stall_cycles += 1; 
          }
       } else {
          g_cu_stats.m_cid_retire_stats.m_stall_reason[UNUSED] += 1; 
          m_cid_retire_stall_cycles += 1; 
       }
       return;
   }

   if (m_cid_retire <= m_cid_commit and m_cid_retire <= m_cid_pass and m_cid_retire <= m_cid_at_head) {
      bool cid_retire_inc = false; 
      commit_entry &ce_retiring = get_commit_entry(m_cid_retire); 
      if (ce_retiring.get_state() == RETIRED) {
         ce_retiring.at_retire_ptr(g_cu_stats, time);
         ce_retiring.delete_tm_manager(); 

         m_cid_retire++; // advance through retired commits 
         cid_retire_inc = true;

         if(g_cu_options.m_fcd_mode == 1)
            m_conflict_detector.clear_writes(ce_retiring);

         if (not ce_retiring.was_skip()) {
            // this entry was active 
            m_n_active_entries -= 1; 

            // this entry had non-empty read-set
            if(ce_retiring.read_set().linear_buffer_usage() > 0)
               m_n_active_entries_have_rs -= 1;

            // this entry had non-empty write-set
            if(ce_retiring.write_set().linear_buffer_usage() > 0)
               m_n_active_entries_have_ws -= 1;

            // if entry needed a ws and PASSed
            if(!ce_retiring.fail() and ce_retiring.write_set().linear_buffer_usage() > 0 and ce_retiring.get_final())
               m_n_active_entries_need_ws -= 1;

            assert(m_n_active_entries_need_rs <= m_n_active_entries_have_rs);
            assert(m_n_active_entries_need_ws <= m_n_active_entries_have_ws);

            assert(m_n_active_entries >= 0);
            assert(m_n_active_entries_have_rs >= 0);
            assert(m_n_active_entries_have_ws >= 0);
            assert(m_n_active_entries_need_rs >= 0);
            assert(m_n_active_entries_need_ws >= 0);
         }
         ce_retiring.dump_timestamps(); 
      }

      if (cid_retire_inc) {
         g_cu_stats.m_cid_retire_stats.m_stall_duration.add2bin(m_cid_retire_stall_cycles); 
         m_cid_retire_stall_cycles = 0; 
      } else {
         g_cu_stats.m_cid_retire_stats.m_stall_reason[ce_retiring.get_state()] += 1; 
         m_cid_retire_stall_cycles += 1; 
      }
   } else {
      g_cu_stats.m_cid_retire_stats.m_stall_reason[UNUSED] += 1; 
      m_cid_retire_stall_cycles += 1; 
   }
}

void commit_unit::check_and_advance_pass_ptr_vwait_nostall(unsigned long long time)
{
   // check if the oldest commit id is ready to send reply 
   if (
         (g_cu_options.m_fcd_mode == 0 && m_cid_pass <= m_cid_at_head) ||
         (g_cu_options.m_fcd_mode == 1 && m_cid_pass < m_cid_fcd)
   ) {
      bool cid_pass_inc = false; 
      commit_entry &oldest_ce = get_commit_entry(m_cid_pass); 
      if (oldest_ce.get_state() == PASS) {
         assert(oldest_ce.get_revalidate() == false);
         send_reply(oldest_ce.get_sid(), oldest_ce.get_tpc(), oldest_ce.get_wid(), oldest_ce.get_commit_id(), CU_PASS); 
         oldest_ce.send_reply(); 
         oldest_ce.set_state(PASS_ACK_WAIT); 
         // PASSed state no longer needs RS
         if(oldest_ce.read_set().linear_buffer_usage() > 0)
            m_n_active_entries_need_rs -= 1;
         assert(m_n_active_entries_need_rs >= 0);

         // TODO: check for overflow and need for revalidate 
         m_cid_pass++;
         cid_pass_inc = true; 
      } else if (oldest_ce.get_state() == FAIL) {
         send_reply(oldest_ce.get_sid(), oldest_ce.get_tpc(), oldest_ce.get_wid(), oldest_ce.get_commit_id(), CU_FAIL); 
         oldest_ce.send_reply(); 
         oldest_ce.set_state(RETIRED); 
         m_cid_pass++;
         cid_pass_inc = true; 
      } else if (oldest_ce.get_state() == RETIRED) {
         m_cid_pass++;
         cid_pass_inc = true; 
      } else if (oldest_ce.get_state() == VALIDATION_WAIT) {
         // If youngest conflicting entry has committed and need to revalidate, advance to revalidate
         // TODO: optimization - if in the middle of first validation (and no conflicting entry, by design), then change to revalidate
         // serialized validation
         if (oldest_ce.get_revalidate() == true) {
            if(m_cid_retire > oldest_ce.get_youngest_conflicting_commit_id()) {
               revalidation(oldest_ce);
            }
         }
         // it is ok to let this pass through pass ptr? 
         m_cid_pass++;
         cid_pass_inc = true; 
      } else if (oldest_ce.get_state() == PASS_ACK_WAIT) {
         m_cid_pass++;
         cid_pass_inc = true; 
      }

      if (cid_pass_inc) {
         g_cu_stats.m_cid_pass_stats.m_stall_duration.add2bin(m_cid_pass_stall_cycles); 
         m_cid_pass_stall_cycles = 0;
      } else {
         g_cu_stats.m_cid_pass_stats.m_stall_reason[oldest_ce.get_state()] += 1; 
         m_cid_pass_stall_cycles += 1; 
      }
   } else {
      g_cu_stats.m_cid_pass_stats.m_stall_reason[UNUSED] += 1; 
      m_cid_pass_stall_cycles += 1; 
   }

}

void commit_unit::check_and_advance_commit_ptr_vwait_nostall(unsigned long long time)
{
   // check if oldest commit id is ready to commit
   if(m_cid_commit < m_cid_pass) {
      assert(m_cid_commit <= m_cid_at_head); 
      bool cid_commit_inc = false; 
      commit_entry &ce_committing = get_commit_entry(m_cid_commit);
      if(ce_committing.get_state() == COMMIT_READY) {
         start_commit_state(ce_committing);
         m_cid_commit++;
         cid_commit_inc = true; 
      } else if (ce_committing.get_state() == RETIRED) {
         m_cid_commit++;
         cid_commit_inc = true; 
      } else if (ce_committing.get_state() == VALIDATION_WAIT) {
         // serialized validation 
         if (m_cid_retire > ce_committing.get_youngest_conflicting_commit_id() && ce_committing.get_revalidate() == true) {
            revalidation(ce_committing);
         }
      }

      if (cid_commit_inc) {
         g_cu_stats.m_cid_commit_stats.m_stall_duration.add2bin(m_cid_commit_stall_cycles); 
         m_cid_commit_stall_cycles = 0;
      } else {
         g_cu_stats.m_cid_commit_stats.m_stall_reason[ce_committing.get_state()] += 1; 
         m_cid_commit_stall_cycles += 1;
      }
   } else {
      g_cu_stats.m_cid_commit_stats.m_stall_reason[UNUSED] += 1; 
      m_cid_commit_stall_cycles += 1;
   }
}

void commit_unit::check_and_advance_retire_ptr_vwait_nostall(unsigned long long time)
{
   // advance retire pointer to the oldest committing transaction 
   if (m_cid_retire <= m_cid_commit and m_cid_retire <= m_cid_pass and m_cid_retire <= m_cid_at_head) {
      bool cid_retire_inc = false; 
      commit_entry &ce_retiring = get_commit_entry(m_cid_retire); 
      if (ce_retiring.get_state() == RETIRED) {
         ce_retiring.at_retire_ptr(g_cu_stats, time);
         ce_retiring.delete_tm_manager();

         m_cid_retire++; // advance through retired commits 
         cid_retire_inc = true; 

         if(g_cu_options.m_fcd_mode == 1)
            m_conflict_detector.clear_writes(ce_retiring);

         if (not ce_retiring.was_skip()) {
            // this entry was active 
            m_n_active_entries -= 1; 

            // this entry had non-empty read-set
            if(ce_retiring.read_set().linear_buffer_usage() > 0)
               m_n_active_entries_have_rs -= 1;

            // this entry had non-empty write-set
            if(ce_retiring.write_set().linear_buffer_usage() > 0)
               m_n_active_entries_have_ws -= 1;

            // if entry needed a ws and PASSed
            if(!ce_retiring.fail() and ce_retiring.write_set().linear_buffer_usage() > 0)
               m_n_active_entries_need_ws -= 1;

            assert(m_n_active_entries >= 0);
            assert(m_n_active_entries_have_rs >= 0);
            assert(m_n_active_entries_have_ws >= 0);
            assert(m_n_active_entries_need_rs >= 0);
            assert(m_n_active_entries_need_ws >= 0);

            // check revalidation table and initiate revalidation here 
            std::pair<cu_revalidation_lookup::iterator, cu_revalidation_lookup::iterator> ireval_set;
            ireval_set = m_revalidation_table.equal_range(ce_retiring.get_commit_id()); 
            for (cu_revalidation_lookup::iterator ireval = ireval_set.first; ireval != ireval_set.second; ++ireval) {
               commit_entry &reval_ce = get_commit_entry(ireval->second); 
               // only initiate revalidation if the pass pointer had pass this sub-tx, so no more conflict can be detected
               if (reval_ce.get_commit_id() < m_cid_pass and reval_ce.get_revalidate() == true) {
                  revalidation(reval_ce); 
               }
            }
            m_revalidation_table.erase(ireval_set.first, ireval_set.second); 
         }
         ce_retiring.dump_timestamps(); 
      }

      if (cid_retire_inc) {
         g_cu_stats.m_cid_retire_stats.m_stall_duration.add2bin(m_cid_retire_stall_cycles); 
         m_cid_retire_stall_cycles = 0; 
      } else {
         g_cu_stats.m_cid_retire_stats.m_stall_reason[ce_retiring.get_state()] += 1; 
         m_cid_retire_stall_cycles += 1; 
      }
   } else {
      g_cu_stats.m_cid_retire_stats.m_stall_reason[UNUSED] += 1; 
      m_cid_retire_stall_cycles += 1; 
   }
}

enum cu_mem_op commit_unit::check_cu_access( mem_fetch *mf ) 
{
   cu_mem_fetch_lookup::const_iterator iMemOp = m_cu_mem_fetch_fields.find(mf); 

   if (iMemOp == m_cu_mem_fetch_fields.end()) {
      return NON_CU_OP; 
   } else {
      return iMemOp->second.operation; 
   }
}

bool commit_unit::ideal_L2_cache(enum cu_mem_op mem_op)
{
   switch (mem_op) {
   case VALIDATE:
      return (g_cu_options.m_ideal_L2_validation);
   case COMMIT_WRITE:
      return (g_cu_options.m_ideal_L2_commit);
   default: 
      return false;
   }
}

void commit_unit::record_cache_hit(enum cu_mem_op mem_op, enum cache_request_status status)
{
   switch (mem_op) {
   case NON_CU_OP: break;
   case VALIDATE:
      g_cu_stats.m_validation_L2_access += 1; 
      if (status == HIT) {
         g_cu_stats.m_validation_L2_hit += 1; 
      }
      break; 
   case COMMIT_WRITE:
      g_cu_stats.m_commit_L2_access += 1; 
      if (status == HIT) {
         g_cu_stats.m_commit_L2_hit += 1; 
      }
      break; 
   }
}

void commit_unit::dump()
{
   print_sanity_counters(stdout);
   for (std::list<mem_fetch*>::iterator imf = m_response_queue.begin(); imf != m_response_queue.end(); ++imf) {
      (*imf)->print(stdout, false);
   }
   printf("cid_at_head=%d cid_fcd=%d cid_pass=%d cid_commit=%d cid_retire=%d\n", m_cid_at_head, m_cid_fcd, m_cid_pass, m_cid_commit, m_cid_retire);
   get_commit_entry(m_cid_at_head).print(stdout); 
   get_commit_entry(m_cid_pass).print(stdout); 
   get_commit_entry(m_cid_retire).print(stdout); 
}

#ifdef DEBUG_TM
   #define tm_debug_printf(...) printf(...)
#else
   #define tm_debug_printf(...) 
#endif

// send a reply packet 
void commit_unit::send_reply( unsigned sid, unsigned tpc, unsigned wid, unsigned commit_id, enum mf_type reply_type )
{
    if (g_cu_options.m_parallel_process_coalesced_input == true) {
        assert((int)wid != -1); 
        warp_commit_entry & wcmt_entry = get_warp_commit_entry(sid, wid); 
        if (reply_type == CU_PASS or reply_type == CU_FAIL) {
            wcmt_entry.signal_validation_done( get_commit_entry(commit_id) ); 
        } else if (reply_type == CU_DONE_COMMIT) {
            wcmt_entry.signal_commit_done( get_commit_entry(commit_id) ); 
        }
    }

    if (g_cu_options.m_coalesce_reply == true and 
        (reply_type == CU_PASS or reply_type == CU_FAIL or reply_type == CU_DONE_COMMIT)) 
    {
        warp_commit_entry & wcmt_entry = get_warp_commit_entry(sid, wid); 
        switch (reply_type) {
            case CU_PASS:
            case CU_FAIL:
                if (wcmt_entry.all_validation_done()) {
                    send_reply_coalesced(sid, tpc, wid, commit_id, reply_type);
		    if (g_tm_options.m_lsu_hpca_enabled) {
		        const std::vector<int> &c_ids = wcmt_entry.get_commit_ids();
			std::vector<int>::const_iterator iter;
			for (iter = c_ids.begin(); iter != c_ids.end(); iter++) {
			    commit_entry &ce = get_commit_entry(*iter);
			    removed_addr_union(ce); 
			}
			broadcast_removed_addr();
		    } 
                }
                break; 
            case CU_DONE_COMMIT:
                if (wcmt_entry.all_commit_done()) {
                    send_reply_coalesced(sid, tpc, wid, commit_id, reply_type);
                }
                break; 
            default: assert(0); break; 
        } 
    } else {
        send_reply_scalar(sid, tpc, wid, commit_id, reply_type);
        if (reply_type == CU_PASS or reply_type == CU_FAIL) {	
	    if (g_tm_options.m_lsu_hpca_enabled) {
	        commit_entry &ce = get_commit_entry(commit_id);
	        removed_addr_union(ce);
	        broadcast_removed_addr();
	    }
	}
    }
}

// send a reply packet - non-coalesced 
void commit_unit::send_reply_scalar( unsigned sid, unsigned tpc, unsigned wid, unsigned commit_id, enum mf_type reply_type )
{
    mem_fetch *r = new mem_fetch( mem_access_t(TX_MSG,0xDEADBEEF,0,false),NULL,TX_PACKET_SIZE,wid,sid,tpc,m_memory_config );
    r->set_type( reply_type );
    r->set_transaction_id( commit_id );
    r->set_is_transactional();
    r->set_sub_partition_id( m_partition_id );
    m_response_queue.push_back(r);
}

// send a reply packet - coalesced 
void commit_unit::send_reply_coalesced( unsigned sid, unsigned tpc, unsigned wid, unsigned commit_id, enum mf_type reply_type )
{
    enum mf_type coalesced_type = reply_type; 
    if (coalesced_type == CU_FAIL) coalesced_type = CU_PASS; 

    // allocate the coalesced packet holder
    mem_fetch *r = new mem_fetch( mem_access_t(TX_MSG,0xDEADBEEF,0,false),NULL,TX_PACKET_SIZE,wid,sid,tpc,m_memory_config );
    r->set_type( coalesced_type );
    r->set_transaction_id( commit_id );
    r->set_is_transactional();
    r->set_sub_partition_id( m_partition_id );

    // generate the individual scalar messages and insert them into the coalesced packet 
    warp_commit_entry & wcmt_entry = get_warp_commit_entry(sid, wid); 
    const std::vector<int> & commit_ids = wcmt_entry.get_commit_ids(); 
    for (unsigned t = 0; t < commit_ids.size(); t++) {
        commit_entry & ce = get_commit_entry(commit_ids[t]); 
        if (coalesced_type == CU_DONE_COMMIT) {
            if ( wcmt_entry.commit_ack_pending(ce) == false )
                continue; // skip to next lane 
        }
        mem_fetch *r_scalar = new mem_fetch( mem_access_t(TX_MSG,0xDEADBEEF,0,false),NULL,TX_PACKET_SIZE,wid,sid,tpc,m_memory_config );
        if (coalesced_type == CU_PASS) {
            if (ce.fail()) {
                r_scalar->set_type( CU_FAIL );
            } else {
                r_scalar->set_type( CU_PASS );
            }
        } else {
           r_scalar->set_type( reply_type );
        }
        r_scalar->set_transaction_id( commit_ids[t] );
        r_scalar->set_is_transactional();
        r_scalar->set_sub_partition_id( m_partition_id );

        r->append_coalesced_packet(r_scalar); 
    }

    m_response_queue.push_back(r);
}

// if not done yet, allocate entries all the way up to the given commit_id
void commit_unit::allocate_to_cid(int commit_id, enum mf_type type)
{
   if (commit_id <= m_cid_at_head) {
      assert(get_commit_entry(commit_id).get_commit_id() == commit_id); 
      if (get_commit_entry(commit_id).get_state() == UNUSED and type != TX_SKIP) {
         m_n_active_entries += 1; // first member arrival
      }
      return; 
   }

   do {
      m_cid_at_head += 1; 
      m_commit_entry_table.push_back(commit_entry(m_cid_at_head));
   } while (m_cid_at_head < commit_id); 
   assert(get_commit_entry(commit_id).get_commit_id() == commit_id); 
   if (get_commit_entry(commit_id).get_state() == UNUSED and type != TX_SKIP) {
      m_n_active_entries += 1; // first member arrival
   }

   if(m_shader_config->cu_finite)
      assert(get_allocated_size() <= m_shader_config->cu_size);
}

void commit_unit::done_validation_wait(commit_entry &ce)
{
   assert(ce.get_state() == VALIDATION_WAIT); 
   
   if (g_tm_options.m_lsu_hpca_enabled) {
       int dec_cycles = 0;
       dec_refCountTable_rd(ce, dec_cycles);
       dec_refCountTable_wr(ce, dec_cycles);
       g_tm_global_statistics.m_reference_count_table_cycles.add2bin(dec_cycles);
   }
   
   if (not g_cu_options.m_vwait_nostall or ce.get_commit_id() > m_cid_pass) {
      // not reaching pass ptr yet -- not sure if it will be conflicting with earlier sub-tx
      // so hold onto cu replies
      if (ce.fail()) {
         ce.set_state(FAIL);

         // FAILed state no longer needs RS or WS
         if(ce.read_set().linear_buffer_usage() > 0)
            m_n_active_entries_need_rs -= 1;
         if(ce.write_set().linear_buffer_usage() > 0)
            m_n_active_entries_need_ws -= 1;

         assert(m_n_active_entries_need_rs >= 0);
         assert(m_n_active_entries_need_ws >= 0);

      } else {
         ce.set_state(PASS);
      }
   } else {
      // passed the pass ptr already, treat as revalidation and send reply right away 
      if (ce.fail()) {
         ce.set_state(FAIL);

         // FAILed state no longer needs RS or WS
         if(ce.read_set().linear_buffer_usage() > 0)
            m_n_active_entries_need_rs -= 1;
         if(ce.write_set().linear_buffer_usage() > 0)
            m_n_active_entries_need_ws -= 1;
         assert(m_n_active_entries_need_rs >= 0);
         assert(m_n_active_entries_need_ws >= 0);

         send_reply(ce.get_sid(), ce.get_tpc(), ce.get_wid(), ce.get_commit_id(), CU_FAIL);
         ce.send_reply();
         ce.set_state(RETIRED);
      } else {
         ce.set_state(PASS);
         send_reply(ce.get_sid(), ce.get_tpc(), ce.get_wid(), ce.get_commit_id(), CU_PASS);
         ce.send_reply();
         ce.set_state(PASS_ACK_WAIT);
         // PASSed state no longer needs RS
         if(ce.read_set().linear_buffer_usage() > 0)
            m_n_active_entries_need_rs -= 1;
         assert(m_n_active_entries_need_rs >= 0);
      }
   }
}

void commit_unit::done_revalidation_wait(commit_entry &ce)
{
   assert(ce.get_state() == REVALIDATION_WAIT);
   
   if (g_tm_options.m_lsu_hpca_enabled) {
       int dec_cycles = 0;
       dec_refCountTable_rd(ce, dec_cycles);
       dec_refCountTable_wr(ce, dec_cycles);
       g_tm_global_statistics.m_reference_count_table_cycles.add2bin(dec_cycles);
   }
   
   if (ce.fail()) {
      send_reply(ce.get_sid(), ce.get_tpc(), ce.get_wid(), ce.get_commit_id(), CU_FAIL);
      ce.send_reply();
      ce.set_state(RETIRED);

      // FAILed state no longer needs RS or WS
      if(ce.read_set().linear_buffer_usage() > 0)
         m_n_active_entries_need_rs -= 1;
      if(ce.write_set().linear_buffer_usage() > 0)
         m_n_active_entries_need_ws -= 1;
      assert(m_n_active_entries_need_rs >= 0);
      assert(m_n_active_entries_need_ws >= 0);

   } else {
      send_reply(ce.get_sid(), ce.get_tpc(), ce.get_wid(), ce.get_commit_id(), CU_PASS);
      ce.send_reply();
      ce.set_state(PASS_ACK_WAIT);
      // PASSed state no longer needs RS
      if(ce.read_set().linear_buffer_usage() > 0)
         m_n_active_entries_need_rs -= 1;
      assert(m_n_active_entries_need_rs >= 0);
   }
}

void commit_unit::start_commit_state(commit_entry &ce)
{
   assert(ce.get_state() == COMMIT_READY);
   const cu_access_set::linear_buffer_t &write_buffer = ce.write_set().get_linear_buffer(); 

   unsigned time = gpu_sim_cycle + gpu_tot_sim_cycle; 

   cu_access_set::linear_buffer_t::const_iterator iAddr;
   for (iAddr = write_buffer.begin(); iAddr != write_buffer.end(); ++iAddr) {
      m_commit_coalescing_queue.push_back(cu_mem_acc(ce.get_commit_id(), *iAddr, COMMIT_WRITE, time)); 
      ce.sent_commit_write(); 
   }
   ce.set_state(COMMIT_SENT);

   // read-only transaction (at least in this commit unit) -- just go directly to retire
   if (ce.commit_write_pending() == false) {
      if (g_tm_options.m_use_logical_timestamp_based_tm == false && g_tm_options.m_eager_warptm_enabled == false)
	  check_read_set_version(ce); 

      ce.set_state(RETIRED); 
      // commit_done_ack(ce); 
   }
}

void commit_unit::revalidation(commit_entry &ce)
{
   assert(ce.get_state() == VALIDATION_WAIT); 
   assert(ce.get_revalidate() == true); 
   const cu_access_set::linear_buffer_t &read_buffer = ce.read_set().get_linear_buffer(); 

   unsigned time = gpu_sim_cycle + gpu_tot_sim_cycle; 

   cu_access_set::linear_buffer_t::const_iterator iAddr;
   for (iAddr = read_buffer.begin(); iAddr != read_buffer.end(); ++iAddr) {
      m_validation_queue.push_back(cu_mem_acc(ce.get_commit_id(), *iAddr, VALIDATE, time)); 
      ce.sent_validation(); 
   }
   m_n_revalidations++; 

   ce.set_revalidate(false);
   ce.set_state(REVALIDATION_WAIT);
}

void commit_unit::update_youngest_conflicting_commit_id(int conflictor_id, commit_entry &reval_ce)
{
   if (g_cu_options.m_vwait_nostall) {
      int old_ycid = reval_ce.get_youngest_conflicting_commit_id(); 
      if (old_ycid != -1 and old_ycid != conflictor_id) {
         std::pair<cu_revalidation_lookup::iterator, cu_revalidation_lookup::iterator> ireval_set;
         ireval_set = m_revalidation_table.equal_range(old_ycid); 
         for (cu_revalidation_lookup::iterator ireval = ireval_set.first; ireval != ireval_set.second; ++ireval) {
            if (ireval->second == reval_ce.get_commit_id()) {
               m_revalidation_table.erase(ireval); 
               break; 
            }
         }
      }
      m_revalidation_table.insert(std::make_pair(conflictor_id, reval_ce.get_commit_id())); 
   }
   reval_ce.set_youngest_conflicting_commit_id(conflictor_id); 
}

// check for conflict between a incoming read and write set of the older transactions 
bool commit_unit::check_conflict_for_read(int commit_id, new_addr_type read_addr)
{
   if (g_cu_options.m_detect_conflicting_cid == false) return false; 

   assert(commit_id > 0);

   commit_entry &ce_original = get_commit_entry(commit_id);

   g_cu_stats.m_bloomfilter_detections += 1; 
   bool conflict_detect = false; 
   for (int c = commit_id - 1; c >= m_cid_retire; c--) {
      commit_entry &ce = get_commit_entry(c); 
      if (not (ce.get_state() == FAIL or ce.get_state() == RETIRED) and ce.write_set().match(read_addr)) {
         conflict_detect = true;
         update_youngest_conflicting_commit_id(ce.get_commit_id(), ce_original); 
         // ce_original.set_youngest_conflicting_commit_id(ce.get_commit_id());
         break; 
      }
   }
   return conflict_detect; 
}

// check for conflict between a incoming write and read set of the younger transactions 
void commit_unit::check_conflict_for_write(int commit_id, new_addr_type write_addr)
{
   if (g_cu_options.m_detect_conflicting_cid == false) return; 

   commit_entry &writing_ce = get_commit_entry(commit_id); 
   bool writer_failed = writing_ce.fail(); 

   g_cu_stats.m_bloomfilter_detections += 1; 
   assert(commit_id > 0); 
   assert(commit_id <= m_cid_at_head); 
   for (int c = commit_id + 1; c <= m_cid_at_head; c++) {
      commit_entry &ce = get_commit_entry(c); 
      if (ce.get_state() != FAIL and ce.read_set().match(write_addr)) {
         if (g_cu_options.m_fail_at_revalidation) {
            if (not writer_failed) {
               ce.set_fail(); // just fail the transaction, do not bother doing revalidation 
               if (ce.get_state() == PASS) 
                  ce.set_state(FAIL); 
            }
         } else {
            // revoke the pass fail status of younger transaction -- it needs to be validate again
            if (ce.get_state() == PASS) 
               ce.set_state(VALIDATION_WAIT); 
            assert(ce.get_state() == FILL || ce.get_state() == VALIDATION_WAIT); 
            ce.set_revalidate(true);
            update_youngest_conflicting_commit_id(commit_id, ce); 
            // ce.set_youngest_conflicting_commit_id(commit_id);
         }
      }
   }
}

void commit_unit::commit_done_ack(commit_entry &ce) 
{
   if (m_shader_config->cu_commit_ack_traffic) {
      send_reply(ce.get_sid(), ce.get_tpc(), ce.get_wid(), ce.get_commit_id(), CU_DONE_COMMIT);
      ce.send_commit_ack();
   } else {
      ce.clear_commit_pending(m_partition_id); 
   }
}

// return false if request should be sent to DRAM after accessing directory
bool commit_unit::access( mem_fetch *mf, unsigned time )
{
   bool done = false; // true simply means this request does not need to access DRAM
   enum mf_type  access_type = mf->get_type();
   unsigned      sid  = mf->get_sid();
   unsigned      commit_id  = mf->get_transaction_id(); // not set for all types of requests
   switch(access_type) {
   case TX_CU_ALLOC:
   case TX_READ_SET:
   case TX_WRITE_SET:
   case TX_DONE_FILL:
   case TX_SKIP:
   case TX_PASS:
   case TX_FAIL:
      tm_debug_printf(" [commit_unit] [part=%u] %d -> m_input_queue : cid=%u sid=%u\n", m_partition_id, access_type, commit_id, sid );
      m_input_queue.push_back(mf); 
      done = true; 
      if (g_cu_options.m_input_queue_length != 0)
         assert(m_input_queue.size() <= g_cu_options.m_input_queue_length); 
      break;
   default:
      break;
   }
   return done;
}

bool commit_unit::get_busy()
{
   return (m_cid_retire <= m_cid_at_head or not m_input_queue.empty()); 
}

// Unit is full if input message queue is full 
bool commit_unit::full()
{
   if (g_cu_options.m_input_queue_length == 0) return false; 
   return (m_input_queue.size() >= g_cu_options.m_input_queue_length); 
}

// process input messages from m_input_queue, called at cycle() 
void commit_unit::process_input( mem_fetch *mf, unsigned time )
{
   bool dummy_mode = g_cu_options.m_dummy_mode; // just a simply request reflector 
   bool done = false; // true simply means this request does not need to access DRAM

   enum mf_type  access_type = mf->get_type();
   new_addr_type addr = mf->get_addr();
   unsigned      wid  = mf->get_wid();
   unsigned      sid  = mf->get_sid();
   unsigned      tpc  = mf->get_tpc();
   unsigned      commit_id  = mf->get_transaction_id(); // not set for all types of requests

   switch(access_type) {
   case TX_CU_ALLOC:
      assert(m_shader_config->cu_finite); // Shouldn't be sending this message for infinite cache
      tm_debug_printf(" [commit_unit] [part=%u] TX_CU_ALLOC : cid=%u\n", m_partition_id, commit_id );
      // Check size
      send_reply(sid,tpc,wid,commit_id, commit_id - m_cid_retire + 1 <= m_shader_config->cu_size ? CU_ALLOC_PASS : CU_ALLOC_FAIL );
      done = true;
      delete mf;
      break;
   case TX_READ_SET:
      if (g_tm_options.m_eager_warptm_enabled) assert(false);
      tm_debug_printf(" [commit_unit] [part=%u] TX_READ_SET : cid=%u\n", m_partition_id, commit_id );
      m_n_tx_read_set++; 
      if (not dummy_mode) {
         allocate_to_cid(commit_id, TX_READ_SET); 
         commit_entry &ce = get_commit_entry(commit_id);
         if (ce.get_state() == UNUSED) {
            ce.set_state(FILL);
            ce.set_retire_cid_at_fill(m_cid_retire);
         }
         assert(ce.get_state() == FILL); 
         ce.set_tx_origin(sid, tpc, wid); 
         ce.set_timestamp_file(m_timestamp_file, m_partition_id); 
         if (m_shader_config->timing_mode_vb_commit) {
            assert(mf->get_tm_manager_ptr() != NULL); 
            ce.set_tm_manager(mf->get_tm_manager_ptr());
         }
         if(ce.read_set().linear_buffer_usage() == 0) {
            m_n_active_entries_have_rs += 1; // first read-set value arrival
            m_n_active_entries_need_rs += 1;
         }
         ce.read_set().append(addr);
         // BF FCD does on the fly CD
         if(g_cu_options.m_fcd_mode == 0) {
            // conflict detection for serialized validation
            if (check_conflict_for_read(commit_id, addr) == true) {
               if (g_cu_options.m_fail_at_revalidation) {
                  ce.set_fail(); // just fail the transaction, do not bother doing revalidation
               } else {
                  ce.set_revalidate(true);
               }
            }
         } else if (g_cu_options.m_fcd_mode == 1) {
            m_conflict_detector.register_read(addr);
         }

         // no need to send validation now if it will be revalidated later
         if (ce.get_revalidate() == false) {
            m_validation_coalescing_queue.push_back(cu_mem_acc(commit_id, addr, VALIDATE, time));
            ce.sent_validation();
         } else {
            assert(g_cu_options.m_fcd_mode != 1); // this should not happen for delayed FCD
         }
         
	 if (g_tm_options.m_lsu_hpca_enabled) {
	    if (m_reference_count_table.count(addr) > 0 || !is_refCountTable_full()) {
	       if (num_refCountTable(addr, true) == 0) {
	           ce.add_newly_inserted_rd_addr(addr);
	       }
	       inc_refCountTable(addr, true);
	    }
         }
      }

      done = true;
      delete mf;
      break;
   case TX_WRITE_SET:
      tm_debug_printf(" [commit_unit] [part=%u] TX_WRITE_SET : cid=%u\n", m_partition_id, commit_id );
      m_n_tx_write_set++; 
      if (not dummy_mode) {
         allocate_to_cid(commit_id, TX_WRITE_SET); 
         commit_entry &ce = get_commit_entry(commit_id); 
         if (ce.get_state() == UNUSED) {
            ce.set_state(FILL);
            ce.set_retire_cid_at_fill(m_cid_retire);
         }
         assert(ce.get_state() == FILL); 
         ce.set_tx_origin(sid, tpc, wid); 
         ce.set_timestamp_file(m_timestamp_file, m_partition_id); 
         if (m_shader_config->timing_mode_vb_commit) {
            assert(mf->get_tm_manager_ptr() != NULL); 
            ce.set_tm_manager(mf->get_tm_manager_ptr());
         }
         if(ce.write_set().linear_buffer_usage() == 0) {
            m_n_active_entries_have_ws += 1; // first write-set value arrival
            m_n_active_entries_need_ws += 1;
         }
         ce.write_set().append(addr); 
         // BF FCD does on the fly CD
         if(g_cu_options.m_fcd_mode == 0 && g_tm_options.m_eager_warptm_enabled == false) {
            // conflict detection for serialized validation
            check_conflict_for_write(commit_id, addr);
         }
         
	 if (g_tm_options.m_lsu_hpca_enabled) {
	    if (m_reference_count_table.count(addr) > 0 || !is_refCountTable_full()) {
	       if (num_refCountTable(addr, false) == 0) {
	           ce.add_newly_inserted_wr_addr(addr);
	       }
	       inc_refCountTable(addr, false);
	    }
         }
      }

      done = true;
      delete mf;
      break;
   case TX_DONE_FILL:
      tm_debug_printf(" [commit_unit] [part=%u] TX_DONE_FILL : cid=%u\n", m_partition_id, commit_id );
      m_n_tx_done_fill++; 
      if (not dummy_mode) {
         allocate_to_cid(commit_id, TX_DONE_FILL); 
         commit_entry &ce = get_commit_entry(commit_id); 
         if (ce.get_state() == UNUSED) {
            ce.set_state(FILL);
            ce.set_retire_cid_at_fill(m_cid_retire);
         }
         assert(ce.get_state() == FILL); 
         ce.set_tx_origin(sid, tpc, wid); 
         ce.set_timestamp_file(m_timestamp_file, m_partition_id); 
         ce.set_commit_pending_ptr(mf->get_commit_pending_ptr()); 
	 if (g_tm_options.m_eager_warptm_enabled) {
	     ce.set_state(COMMIT_READY);
	     ce.set_final(true); 
	 } else {
             ce.set_state(VALIDATION_WAIT);

             if(g_cu_options.m_fcd_mode == 1)
                assert(ce.get_revalidate() == false);

             if (ce.validation_pending() == false and ce.get_revalidate() == false) {
                // nothing pending and no need to revalidate, can get out of validation_wait state
                done_validation_wait(ce);
             }
	 }

	 if (g_tm_options.m_lsu_hpca_enabled) {
	     newly_inserted_addr_union(ce);
	 }
      } else {
         send_reply(sid, tpc, -1, commit_id, CU_PASS); // only for close loop testing 
      }
      done = true;
      delete mf;
      break;
   case TX_SKIP:
       tm_debug_printf(" [commit_unit] [part=%u] TX_SKIP : cid=%u\n", m_partition_id, commit_id );
       m_n_tx_skip++;
       if (not dummy_mode) {
           allocate_to_cid(commit_id, TX_SKIP);
           commit_entry &ce = get_commit_entry(commit_id);
           assert(ce.get_state() == UNUSED);
           ce.set_tx_origin(sid, tpc, wid);
           ce.set_timestamp_file(m_timestamp_file, m_partition_id); 
           ce.set_skip(); 
           ce.set_state(RETIRED);
       }
       done = true;
       delete mf;
       break;
   case TX_PASS:
      if (g_tm_options.m_eager_warptm_enabled) assert(false);
      tm_debug_printf(" [commit_unit] [part=%u] TX_PASS : cid=%u\n", m_partition_id, commit_id );
      m_n_tx_pass++; 
      if (not dummy_mode) {
         commit_entry &ce = get_commit_entry(commit_id); 
         assert(ce.get_state() == PASS_ACK_WAIT); 
         ce.set_final(true); 
         ce.set_state(COMMIT_READY);
      }
      done = true;
      delete mf;
      break;
   case TX_FAIL:
      if (g_tm_options.m_eager_warptm_enabled) assert(false);
      tm_debug_printf(" [commit_unit] [part=%u] TX_FAIL : cid=%u\n", m_partition_id, commit_id );
      m_n_tx_fail++; 
      if (not dummy_mode) {
         commit_entry &ce = get_commit_entry(commit_id); 
         assert(ce.get_state() == PASS_ACK_WAIT or ce.get_state() == RETIRED); 
         ce.set_final(false);
         // Remove writeset if it had passed
         if(!ce.fail() and ce.write_set().linear_buffer_usage() > 0)
            m_n_active_entries_need_ws -= 1;
         assert(m_n_active_entries_need_ws >= 0);
         if (ce.get_state() == PASS_ACK_WAIT) {
            ce.set_state(RETIRED);  
         }
      }
      done = true;
      delete mf;
      break;
   default:
      break;
   }
   // return done;
}

void commit_unit::print_sanity_counters(FILE *fp)
{
   fprintf(fp, "  n_tx_read_set=%u; n_tx_write_set=%u; n_tx_done_fill=%u; n_tx_skip=%u, n_tx_pass=%u; n_tx_fail=%u;\n",
           m_n_tx_read_set, m_n_tx_write_set, m_n_tx_done_fill, m_n_tx_skip, m_n_tx_pass, m_n_tx_fail);
   fprintf(fp, "  input_pkt_processed=%u; recency_bf_activity=%u; reply_sent=%u;\n", 
           m_n_input_pkt_processed, m_n_recency_bf_activity, m_n_reply_sent); 
   fprintf(fp, "  n_validations=%u; n_validations_L2acc=%u; n_commit_writes=%u; n_commit_writes_L2acc=%u;\n", 
           m_n_validations, m_n_validations_processed, m_n_commit_writes, m_n_commit_writes_processed ); 
   fprintf(fp, "  n_revalidations=%u; n_active=%d\n", m_n_revalidations, m_n_active_entries);
}

void commit_unit::visualizer_print(gzFile visualizer_file)
{
   gzprintf(visualizer_file, "cu_tx_read_set: %u %u\n", m_partition_id, m_n_tx_read_set); 
   gzprintf(visualizer_file, "cu_tx_write_set: %u %u\n", m_partition_id, m_n_tx_write_set); 
   gzprintf(visualizer_file, "cu_tx_done_fill: %u %u\n", m_partition_id, m_n_tx_done_fill); 
   gzprintf(visualizer_file, "cu_tx_skip: %u %u\n", m_partition_id, m_n_tx_skip); 
   gzprintf(visualizer_file, "cu_tx_pass: %u %u\n", m_partition_id, m_n_tx_pass); 
   gzprintf(visualizer_file, "cu_tx_fail: %u %u\n", m_partition_id, m_n_tx_fail); 

   gzprintf(visualizer_file, "cu_input_pkt_processed: %u %u\n", m_partition_id, m_n_input_pkt_processed); 
   gzprintf(visualizer_file, "cu_recency_bf_activity: %u %u\n", m_partition_id, m_n_recency_bf_activity); 
   gzprintf(visualizer_file, "cu_reply_sent: %u %u\n", m_partition_id, m_n_reply_sent); 
   gzprintf(visualizer_file, "cu_validations: %u %u\n", m_partition_id, m_n_validations); 
   gzprintf(visualizer_file, "cu_validations_processed: %u %u\n", m_partition_id, m_n_validations_processed); 
   gzprintf(visualizer_file, "cu_commit_writes: %u %u\n", m_partition_id, m_n_commit_writes); 
   gzprintf(visualizer_file, "cu_commit_writes_processed: %u %u\n", m_partition_id, m_n_commit_writes_processed); 
   gzprintf(visualizer_file, "cu_revalidations: %u %u\n", m_partition_id, m_n_revalidations); 
}

// access function to decouple commit id and location in commit entry table 
// - return the commit entry that corresponds to the commit id
// - every access to commit table (except scrub) should go through this 
commit_entry& commit_unit::get_commit_entry(int commit_id)
{
   int cid_at_table_front = m_commit_entry_table.begin()->get_commit_id(); 
   assert(commit_id >= cid_at_table_front); 
   int cid_with_offset = commit_id - cid_at_table_front;
   return m_commit_entry_table[cid_with_offset]; 
}

// deallocate some commit entry to free up memory 
void commit_unit::scrub_retired_commit_entries() 
{
   int cid_at_table_front = m_commit_entry_table.begin()->get_commit_id(); 
   while (cid_at_table_front < (m_cid_retire - 23040)) { // max #concurrent transactions 
      tm_debug_printf(" [commit_unit] [part=%u] scrubbing entry cid=%d\n", m_partition_id, cid_at_table_front); 
      m_commit_entry_table.pop_front(); 
      cid_at_table_front = m_commit_entry_table.begin()->get_commit_id(); 
   }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////

conflict_table_hash::conflict_table_hash(unsigned n_sets, unsigned n_ways)
   : m_sets(n_sets), m_ways(n_ways)
{
   m_size = m_sets * m_ways;
   m_lines = new conflict_table_hash_entry[m_size];
   init_h3_hash(m_sets);
}

conflict_table_hash::~conflict_table_hash()
{
   delete [] m_lines;
}

bool conflict_table_hash::probe(new_addr_type addr, int& probed_entry_idx) const
{
   unsigned set = h3_hash1(m_sets, addr);
   assert(set < m_sets);
   for(unsigned i=0; i<m_ways; i++) {
      unsigned idx = set*m_ways + i;
      const conflict_table_hash_entry& entry = m_lines[idx];
      if(!entry.valid)
         continue;
      if(entry.addr == addr) {
         probed_entry_idx = idx;
         return true;
      }
   }
   return false;
}

bool conflict_table_hash::check_read_conflict(new_addr_type addr, int cid, int retire_cid_at_fill, int& conflicting_cid) const
{
   int probed_entry_idx;
   if(probe(addr,probed_entry_idx)) {
      if(m_lines[probed_entry_idx].commit_id >= retire_cid_at_fill) {
         conflicting_cid = m_lines[probed_entry_idx].commit_id;
         return true;
      }
   }
   return false;
}

bool conflict_table_hash::store_write(new_addr_type addr, int cid, new_addr_type& evicted_addr, int& evicted_cid)
{
   // Check for a hit
   int probed_entry_idx;
   if(probe(addr,probed_entry_idx)) {
      conflict_table_hash_entry& entry = m_lines[probed_entry_idx];
      assert(entry.addr == addr && entry.valid);
      entry.commit_id = cid;
      return false;
   }

   // Not a hit, need to insert
   bool evicted = false;
   bool inserted = false;
   int evict_entry_id = -1;

   unsigned set = h3_hash1(m_sets, addr);
   assert(set < m_sets);
   for(unsigned i=0; i<m_ways; i++) {
      unsigned idx = set*m_ways + i;
      conflict_table_hash_entry *entry = &m_lines[idx];
      // Insert into unused entry if possible
      if(!entry->valid) {
         entry->addr = addr;
         entry->commit_id = cid;
         entry->valid = true;
         inserted = true;
         break;
      } else {
         // Record entry with smallest cid for eviction (oldest first)
         if( evict_entry_id==-1 || m_lines[evict_entry_id].commit_id > entry->commit_id)
            evict_entry_id = idx;
      }
   }

   // Evict if necessary
   if(!inserted){
      conflict_table_hash_entry *evict_entry = &m_lines[evict_entry_id];
      assert(evict_entry->valid);

      evicted = true;
      evicted_addr = evict_entry->addr;
      evicted_cid = evict_entry->commit_id;

      evict_entry->addr = addr;
      evict_entry->commit_id = cid;
   }

   return evicted;
}

void conflict_table_hash::print(FILE* fp, new_addr_type addr) {
   unsigned set = h3_hash1(m_sets, addr);
   assert(set < m_sets);
   for(unsigned i=0; i<m_ways; i++) {
      unsigned idx = set*m_ways + i;
      const conflict_table_hash_entry& entry = m_lines[idx];
      fprintf(fp,"[v=%d addr=%llu cid=%d] ", entry.valid, entry.addr, entry.commit_id);
   }
   fprintf(fp,"\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////

conflict_table_perfect::conflict_table_perfect(unsigned addr_granularity)
{
   m_active_entries = 0;
   m_addr_quantize_mask = ~((new_addr_type)addr_granularity - 1ULL); 
}

// quantize address to specified granularity 
new_addr_type conflict_table_perfect::quantize_address(new_addr_type addr)
{
   return addr & m_addr_quantize_mask; 
}

bool conflict_table_perfect::check_read_conflict(new_addr_type addr, int cid, int retire_cid_at_fill, int& conflicting_cid) {
   conflict_table_t::const_iterator it = m_conflict_table.find(addr);
   assert(it != m_conflict_table.end());

   const conflict_table_entry &entry = it->second;
   if(entry.commit_id >= retire_cid_at_fill) {
      conflicting_cid = entry.commit_id;
      return true;
   }

   return false;
}

void conflict_table_perfect::store_write(new_addr_type addr, int cid) {
   conflict_table_t::iterator it = m_conflict_table.find(addr);
   if(it == m_conflict_table.end()) {
      m_conflict_table[addr] = conflict_table_entry(cid);
      m_active_entries += 1;
   } else {
      conflict_table_entry &entry = it->second;
      entry.commit_id = cid;
      if(!entry.active) {
         assert(entry.read_counter == 0);
         m_active_entries += 1;
      }
      entry.active = true;
   }
}

void conflict_table_perfect::register_read(new_addr_type addr) {
   conflict_table_t::iterator it = m_conflict_table.find(addr);
   if(it == m_conflict_table.end()) {
      m_conflict_table[addr] = conflict_table_entry(-1);
      m_conflict_table[addr].read_counter += 1;
      m_active_entries += 1;
   } else {
      conflict_table_entry &entry = it->second;
      if(!entry.active) {
         assert(entry.read_counter == 0);
         m_active_entries += 1;
      }
      entry.active = true;
      entry.read_counter += 1;
   }
}

void conflict_table_perfect::clear_writes(commit_entry &ce) {
   if(ce.were_delayfcd_writes_stored()) {
      // Remove unused writeset entries
      const cu_access_set::linear_buffer_t &ws_buffer = ce.write_set().get_linear_buffer();
      cu_access_set::linear_buffer_t::const_iterator iAddrW;
      for(iAddrW=ws_buffer.begin(); iAddrW!=ws_buffer.end(); iAddrW++) {
         new_addr_type addrW = quantize_address(*iAddrW); 
         if(m_conflict_table.find(addrW) != m_conflict_table.end()) {
            // Remove if ce's own entry and no one else reading it
            if( m_conflict_table[addrW].active &&
                m_conflict_table[addrW].commit_id == ce.get_commit_id() &&
                m_conflict_table[addrW].read_counter == 0
               ) {
               m_conflict_table[addrW].active = false;
               m_active_entries -= 1;
               assert(m_active_entries >= 0);
            }
         }
      }
   }

   // Decrement all read counters, remove unused entries
   const cu_access_set::linear_buffer_t &rs_buffer = ce.read_set().get_linear_buffer();
   cu_access_set::linear_buffer_t::const_iterator iAddrR;
   for(iAddrR=rs_buffer.begin(); iAddrR!=rs_buffer.end(); iAddrR++) {
      new_addr_type addrR = quantize_address(*iAddrR); 
      assert(m_conflict_table.find(addrR) != m_conflict_table.end()); // it was inserted
      assert(m_conflict_table[addrR].read_counter > 0); // it was incremented
      assert(m_conflict_table[addrR].active);
      m_conflict_table[addrR].read_counter -= 1;

      if(m_conflict_table[addrR].read_counter == 0 && m_conflict_table[addrR].active) {
         m_conflict_table[addrR].active = false;
         m_active_entries -= 1;
         assert(m_active_entries >= 0);
      }
   }
}


int conflict_table_perfect::count_active_entries() const {
   int count = 0;
   conflict_table_t::const_iterator it;
   for(it=m_conflict_table.begin(); it!=m_conflict_table.end(); it++) {
      const conflict_table_entry &entry = it->second;
      if(entry.active)
         count++;
   }
   return count;
}

bool conflict_table_perfect::check_entry(new_addr_type addr, int cid) const
{
   conflict_table_t::const_iterator it = m_conflict_table.find(addr);
   if(it == m_conflict_table.end())
      return false;
   else {
      return (cid == it->second.commit_id);
   }
}

void conflict_table_perfect::print(FILE *fp, new_addr_type addr) const
{
   conflict_table_t::const_iterator it = m_conflict_table.find(addr);
   if(it == m_conflict_table.end()) {
      fprintf(fp, "Entry at addr=%llu does not exist.\n", addr);
   } else {
      fprintf(fp, "[addr=%llu cid=%d active=%d rc=%u]\n", addr, it->second.commit_id, it->second.active, it->second.read_counter);
   }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

conflict_detector::conflict_detector(unsigned n_hash_sets, unsigned n_hash_ways, unsigned bf_size, unsigned bf_n_funcs, unsigned addr_granularity)
   : m_conflict_table_hash(n_hash_sets,n_hash_ways), m_conflict_table_perfect(addr_granularity) 
{
   assert(bf_n_funcs > 0 && bf_n_funcs <= 4);
   std::vector<int> fid(bf_n_funcs);
   for(unsigned i=0; i<bf_n_funcs; i++)
      fid[i] = i;
   m_conflict_table_bf = new versioning_bloomfilter(bf_size, fid, bf_n_funcs);

   m_addr_quantize_mask = ~((new_addr_type)addr_granularity - 1ULL); 
}

conflict_detector::~conflict_detector()
{
   delete m_conflict_table_bf;
}

// quantize address to specified granularity 
new_addr_type conflict_detector::quantize_address(new_addr_type addr)
{
   return addr & m_addr_quantize_mask; 
}

bool conflict_detector::check_read_conflict(new_addr_type addr, int cid, int retire_cid_at_fill, int& conflicting_cid) 
{
   addr = quantize_address(addr);  

   int conflict_cid_perfect = -1; // conflict cid as reported by perfect table
   int conflict_cid_hash_bf = -1; // conflict cid as reported by hash+bf system

   int conflict_cid_hash;
   int conflict_cid_bf;

   // Perfect table
   bool conflict_perfect = m_conflict_table_perfect.check_read_conflict(addr,cid,retire_cid_at_fill,conflict_cid_perfect);

   // Hash table
   bool conflict_hash = m_conflict_table_hash.check_read_conflict(addr,cid,retire_cid_at_fill,conflict_cid_hash);

   // Versioning bf
   conflict_cid_bf = m_conflict_table_bf->get_version(addr);
   bool conflict_bf = (conflict_cid_bf >= retire_cid_at_fill);

   // Hash+BF
   if(conflict_hash) {
      // Hit in hash
      conflict_cid_hash_bf = conflict_cid_hash;

      assert(conflict_perfect);
      assert(conflict_cid_hash == conflict_cid_perfect);
      g_cu_stats.m_conflict_table_hash_hits += 1;     // hit in hash and hit in perfect

   } else {
      // Not in hash, check BF
      if(conflict_perfect)
         g_cu_stats.m_conflict_table_hash_misses += 1;     // miss in hash but hit in perfect

      if(conflict_bf) {
         // "Hit" in BF
         conflict_cid_hash_bf = conflict_cid_bf;

         if(conflict_perfect) {
            assert(conflict_cid_bf >= conflict_cid_perfect);
            g_cu_stats.m_conflict_table_bf_true_positives += 1; // hit in bf and hit in perfect
         } else
            g_cu_stats.m_conflict_table_bf_false_positives += 1; // hit in bf but no hit in perfect
      } else {
         assert(!conflict_perfect); // Can't have false negatives (no hit in bf but hit in perfect)
         g_cu_stats.m_conflict_table_bf_true_negatives += 1;
      }
   }

   if(g_cu_options.m_delayed_fcd_cd_mode == 0) {
      // Perfect conflict detector
      conflicting_cid = conflict_cid_perfect;
      return conflict_perfect;
   } else if(g_cu_options.m_delayed_fcd_cd_mode == 1) {
      // Hash + BF conflict detector
      conflicting_cid = conflict_cid_hash_bf;
      return (conflict_hash || conflict_bf);
   } else {
      abort();
   }
}

void conflict_detector::store_write(new_addr_type addr, int cid) 
{
   addr = quantize_address(addr);  

   // Perfect
   m_conflict_table_perfect.store_write(addr,cid);

   // Hash + BF
   new_addr_type evicted_addr;
   int evicted_cid;
   bool evicted = m_conflict_table_hash.store_write(addr,cid,evicted_addr,evicted_cid);
   if(evicted) {
      assert(m_conflict_table_perfect.check_entry(evicted_addr,evicted_cid));
      m_conflict_table_bf->update_version(evicted_addr,evicted_cid);
      g_cu_stats.m_conflict_table_hash_evictions += 1;
   }
}

void conflict_detector::register_read(new_addr_type addr) {
   addr = quantize_address(addr);  
   m_conflict_table_perfect.register_read(addr);
}

void conflict_detector::clear_writes(commit_entry &ce) {
   m_conflict_table_perfect.clear_writes(ce);
}

// process queued work
void commit_unit_logical::cycle(unsigned long long time)
{
   // e.g., send an abort message
   if( !m_response_queue.empty() ) {
      if( !m_response_port->full(TX_PACKET_SIZE,0) ) {
         mem_fetch *mf = m_response_queue.front();
         m_response_port->push(mf);
         m_sent_icnt_traffic += mf->get_num_flits(false); 
         m_response_queue.pop_front();
         m_n_reply_sent++; 
      }
   }

   // stub to drain the commit queue 
   if( !m_commit_queue.empty() ) {
      cu_mem_acc &mem_op = m_commit_queue.front(); 
      assert(mem_op.operation == COMMIT_WRITE); 

      if (g_cu_options.m_gen_L2_acc & 0x2) {
         send_to_L2(time, mem_op); 
      } else {
         if (mem_op.has_coalesced_ops()) {
            while (mem_op.has_coalesced_ops()) {
               cu_mem_acc &scalar_mem_op = mem_op.next_coalesced_op(); 
               process_commit_op_reply(NULL, scalar_mem_op, time); 
               mem_op.pop_coalesced_op(); 
            } 
         } else {
            process_commit_op_reply(NULL, mem_op, time); 
         }
      }

      m_n_commit_writes++; 
      m_commit_queue.pop_front(); 
   }
   g_cu_stats.m_commit_queue_size.add2bin(m_commit_queue.size()); 

   // stub to drain the validation queue and assume that they are all validated (and pass)
   if( !m_validation_queue.empty() ) {
       assert(0 && "No validation in commit unit for logical timestamp based tm manager.\n");
   }
   g_cu_stats.m_validation_queue_size.add2bin(m_validation_queue.size()); 

   // entry pointers and state management 
   if (g_cu_options.m_vwait_nostall) {
       assert(0);
   } else {
      check_and_advance_commit_ptr(time); 
      check_and_advance_retire_ptr(time); 
   }

   if (m_cid_retire <= m_cid_at_head) 
      g_cu_stats.m_distance_retire_head.add2bin(m_cid_at_head - m_cid_retire); 
   else 
      g_cu_stats.m_distance_retire_head.add2bin(0);

   assert(m_n_active_entries_have_rs <= m_n_active_entries);
   assert(m_n_active_entries_have_ws <= m_n_active_entries);
   assert(m_n_active_entries_need_rs <= m_n_active_entries_have_rs);
   assert(m_n_active_entries_need_ws <= m_n_active_entries_have_ws);

   g_cu_stats.m_active_entries.add2bin(m_n_active_entries);
   g_cu_stats.m_active_entries_have_rs.add2bin(m_n_active_entries_have_rs);
   g_cu_stats.m_active_entries_have_ws.add2bin(m_n_active_entries_have_ws);
   g_cu_stats.m_active_entries_need_rs.add2bin(m_n_active_entries_need_rs);
   g_cu_stats.m_active_entries_need_ws.add2bin(m_n_active_entries_need_ws);

   // process input messages 
   if (not m_input_queue.empty()) {
      mem_fetch *input_msg = m_input_queue.front(); 
      if (input_msg->has_coalesced_packet()) {
         if (g_cu_options.m_parallel_process_coalesced_input == true) {
            process_coalesced_input_parallel(input_msg, time); 
         } else {
            process_coalesced_input_serial(input_msg, time); 
         }
      } else {
         process_input( input_msg, time );
         m_input_queue.pop_front(); 
      }
      m_n_input_pkt_processed++; 
   }
   g_cu_stats.m_input_queue_size.add2bin(m_input_queue.size()); 
   g_cu_stats.m_response_queue_size.add2bin(m_response_queue.size()); 

   if ( time % 1000 == 0 ) 
      scrub_retired_commit_entries(); 
}

// process a scalar commit operation returned from L2 cache 
void commit_unit_logical::process_commit_op_reply(mem_fetch *mf, const cu_mem_acc &mem_op, unsigned time)
{
   if (mf != NULL) 
      assert(mf->get_access_type() == GLOBAL_ACC_W); 
   assert(mem_op.has_coalesced_ops() == false); // ensure this is an scalar operation 
   commit_entry &ce = get_commit_entry(mem_op.commit_id);
   // notify the commit update and retire transaction if this is the last update 
   addr_t commit_addr = mem_op.addr; 
   if (m_shader_config->timing_mode_vb_commit) {
      if (ce.get_tm_manager()->watched())
         printf("[CID=%d]", mem_op.commit_id); 
      ce.get_tm_manager()->commit_addr(mem_op.addr);
   }
   ce.commit_write_done(); 
   if (ce.get_state() == COMMIT_SENT and ce.commit_write_pending() == false) {
      ce.set_state(RETIRED); 
      commit_done_ack(ce);
   }
   if (mf  && g_tm_options.m_logical_temporal_use_cuckoo_table) {
      mem_access_byte_mask_t cuckoo_check_byte_mask = mf->get_cuckoo_check_byte_mask();
      new_addr_type block_addr = commit_addr & (127ull);
      for (int i = 0; i < 4; i++) {
         cuckoo_check_byte_mask.set(block_addr + i);
      }
      mf->set_cuckoo_check_byte_mask(cuckoo_check_byte_mask);
      mf->set_cuckoo_table_checked();
   }
   g_cu_stats.m_commit_latency.add2bin(time - mem_op.issue_cycle); 
   m_n_commit_writes_processed++; 
}

// process input messages from m_input_queue, called at cycle() 
void commit_unit_logical::process_input( mem_fetch *mf, unsigned time )
{
   bool dummy_mode = g_cu_options.m_dummy_mode; // just a simply request reflector 
   bool done = false; // true simply means this request does not need to access DRAM

   enum mf_type  access_type = mf->get_type();
   new_addr_type addr = mf->get_addr();
   unsigned      wid  = mf->get_wid();
   unsigned      sid  = mf->get_sid();
   unsigned      tpc  = mf->get_tpc();
   unsigned      commit_id  = mf->get_transaction_id(); // not set for all types of requests

   switch(access_type) {
   case TX_CU_ALLOC:
   case TX_READ_SET:
      assert(0);
      break;
   case TX_WRITE_SET:
      tm_debug_printf(" [commit_unit] [part=%u] TX_WRITE_SET : cid=%u\n", m_partition_id, commit_id );
      m_n_tx_write_set++; 
      if (not dummy_mode) {
         allocate_to_cid(commit_id, TX_WRITE_SET); 
         commit_entry &ce = get_commit_entry(commit_id); 
         if (ce.get_state() == UNUSED) {
            ce.set_state(FILL);
            ce.set_retire_cid_at_fill(m_cid_retire);
         }
         assert(ce.get_state() == FILL); 
         ce.set_tx_origin(sid, tpc, wid); 
         ce.set_timestamp_file(m_timestamp_file, m_partition_id); 
         if (m_shader_config->timing_mode_vb_commit) {
            assert(mf->get_tm_manager_ptr() != NULL); 
            ce.set_tm_manager(mf->get_tm_manager_ptr());
         }
         if(ce.write_set().linear_buffer_usage() == 0) {
            m_n_active_entries_have_ws += 1; // first write-set value arrival
            m_n_active_entries_need_ws += 1;
         }
         ce.write_set().append(addr); 
      }
      done = true;
      delete mf;
      break;
   case TX_DONE_FILL:
      tm_debug_printf(" [commit_unit] [part=%u] TX_DONE_FILL : cid=%u\n", m_partition_id, commit_id );
      m_n_tx_done_fill++; 
      if (not dummy_mode) {
         allocate_to_cid(commit_id, TX_DONE_FILL); 
         commit_entry &ce = get_commit_entry(commit_id); 
         if (ce.get_state() == UNUSED) {
            ce.set_state(FILL);
            ce.set_retire_cid_at_fill(m_cid_retire);
         }
         assert(ce.get_state() == FILL); 
         ce.set_tx_origin(sid, tpc, wid); 
         ce.set_timestamp_file(m_timestamp_file, m_partition_id); 
         ce.set_commit_pending_ptr(mf->get_commit_pending_ptr()); 
         ce.set_state(COMMIT_READY);
	 ce.set_final(true);
      } else {
         send_reply(sid, tpc, -1, commit_id, CU_PASS); // only for close loop testing 
      }
      done = true;
      delete mf;
      break;
   case TX_SKIP:
       tm_debug_printf(" [commit_unit] [part=%u] TX_SKIP : cid=%u\n", m_partition_id, commit_id );
       m_n_tx_skip++;
       if (not dummy_mode) {
           allocate_to_cid(commit_id, TX_SKIP);
           commit_entry &ce = get_commit_entry(commit_id);
           assert(ce.get_state() == UNUSED);
           ce.set_tx_origin(sid, tpc, wid);
           ce.set_timestamp_file(m_timestamp_file, m_partition_id); 
           ce.set_skip(); 
           ce.set_state(RETIRED);
	   ce.set_final(false);
       }
       done = true;
       delete mf;
       break;
   case TX_PASS:
   case TX_FAIL:
      assert(0);
      break;
   default:
      assert(0);
      break;
   }
   // return done;
}

void commit_unit_logical::check_and_advance_commit_ptr(unsigned long long time)
{
   // check if oldest commit id is ready to commit
   if(m_cid_commit <= m_cid_at_head) {
      bool cid_commit_inc = false; 
      commit_entry &ce_committing = get_commit_entry(m_cid_commit);
      if(ce_committing.get_state() == COMMIT_READY) {
         start_commit_state(ce_committing);
         m_cid_commit++;
         cid_commit_inc = true; 
      } else if (ce_committing.get_state() == RETIRED) {
         m_cid_commit++;
         cid_commit_inc = true; 
      }
      if (g_cu_options.m_coalesce_mem_op) {
         // wait until all the memory ops are in the coalescing queue 
         int sid = ce_committing.get_sid(); 
         int wid = ce_committing.get_wid(); 
         warp_commit_entry &wcmt_entry = get_warp_commit_entry(sid, wid); 
         if (m_cid_commit > wcmt_entry.get_max_commit_id()) {
            coalesce_ops_to_queue(COMMIT_WRITE); 
	 }
      } else {
         transfer_ops_to_queue(COMMIT_WRITE); 
      }

      if (cid_commit_inc) {
         g_cu_stats.m_cid_commit_stats.m_stall_duration.add2bin(m_cid_commit_stall_cycles); 
         m_cid_commit_stall_cycles = 0;
      } else {
         g_cu_stats.m_cid_commit_stats.m_stall_reason[ce_committing.get_state()] += 1; 
         m_cid_commit_stall_cycles += 1;
      }
   } else {
      g_cu_stats.m_cid_commit_stats.m_stall_reason[UNUSED] += 1; 
      m_cid_commit_stall_cycles += 1;
   }

}

void commit_unit_logical::check_and_advance_retire_ptr(unsigned long long time)
{
   // advance retire pointer to the oldest committing transaction 
   if (m_cid_retire <= m_cid_commit and m_cid_retire <= m_cid_at_head) {
      bool cid_retire_inc = false; 
      commit_entry &ce_retiring = get_commit_entry(m_cid_retire); 
      if (ce_retiring.get_state() == RETIRED) {
         ce_retiring.at_retire_ptr_logical(g_cu_stats, time);
         ce_retiring.delete_tm_manager(); 

         m_cid_retire++; // advance through retired commits 
         cid_retire_inc = true;

         if (not ce_retiring.was_skip()) {
            // this entry was active 
            m_n_active_entries -= 1; 

            // this entry had non-empty read-set
            if(ce_retiring.read_set().linear_buffer_usage() > 0)
               m_n_active_entries_have_rs -= 1;

            // this entry had non-empty write-set
            if(ce_retiring.write_set().linear_buffer_usage() > 0)
               m_n_active_entries_have_ws -= 1;

            // if entry needed a ws and PASSed
	    assert(ce_retiring.fail() == false);
            if(ce_retiring.write_set().linear_buffer_usage() > 0)
               m_n_active_entries_need_ws -= 1;

            assert(m_n_active_entries_need_rs <= m_n_active_entries_have_rs);
            assert(m_n_active_entries_need_ws <= m_n_active_entries_have_ws);

            assert(m_n_active_entries >= 0);
            assert(m_n_active_entries_have_rs >= 0);
            assert(m_n_active_entries_have_ws >= 0);
            assert(m_n_active_entries_need_rs >= 0);
            assert(m_n_active_entries_need_ws >= 0);
         }
         ce_retiring.dump_timestamps(); 
      }

      if (cid_retire_inc) {
         g_cu_stats.m_cid_retire_stats.m_stall_duration.add2bin(m_cid_retire_stall_cycles); 
         m_cid_retire_stall_cycles = 0; 
      } else {
         g_cu_stats.m_cid_retire_stats.m_stall_reason[ce_retiring.get_state()] += 1; 
         m_cid_retire_stall_cycles += 1; 
      }
   } else {
      g_cu_stats.m_cid_retire_stats.m_stall_reason[UNUSED] += 1; 
      m_cid_retire_stall_cycles += 1; 
   }
}

// Functions for LSU HPCA2016 Early Abort Paper

bool commit_unit::is_refCountTable_full() {
    return m_reference_count_table.size() >= g_tm_options.m_reference_count_table_size;
} 

void commit_unit::inc_refCountTable(addr_t waddr, bool rd) {
    assert(m_reference_count_table.count(waddr) > 0 || !is_refCountTable_full());
    if (rd) {    
        m_reference_count_table[waddr].first++;
    } else {
        m_reference_count_table[waddr].second++;
    }
    g_tm_global_statistics.m_reference_count_table_size.add2bin(m_reference_count_table.size());
}

void commit_unit::dec_refCountTable_rd(commit_entry &ce, int &dec_cycles) {
    // Assume TX_LOG traverse happens in a single cycle, but actually this is impossible, especially for long transaction
    const cu_access_set::linear_buffer_t &rd_buffer = ce.read_set().get_linear_buffer();
    cu_access_set::linear_buffer_t::const_iterator iAddr;
    for (iAddr = rd_buffer.begin(); iAddr != rd_buffer.end(); iAddr++) {
	addr_t addr = *iAddr;
	if (m_reference_count_table.count(addr) > 0) {
	    assert(m_reference_count_table[addr].first > 0 || m_reference_count_table[addr].second > 0);
	    if (m_reference_count_table[addr].first > 0) {
	        m_reference_count_table[addr].first--;
	        if (m_reference_count_table[addr].first == 0) {
                    ce.add_removed_rd_addr(addr);
	        }
	    }
	    if (m_reference_count_table[addr].first + m_reference_count_table[addr].second == 0) {
	        m_reference_count_table.erase(addr);
	    }
	}
    }
    dec_cycles += rd_buffer.size();
    g_tm_global_statistics.m_reference_count_table_size.add2bin(m_reference_count_table.size());
}

void commit_unit::dec_refCountTable_wr(commit_entry &ce, int &dec_cycles) {
    // Assume TX_LOG traverse happens in a single cycle, but actually this is impossible, especially for long transaction
    const cu_access_set::linear_buffer_t &wr_buffer = ce.write_set().get_linear_buffer();
    cu_access_set::linear_buffer_t::const_iterator iAddr;
    for (iAddr = wr_buffer.begin(); iAddr != wr_buffer.end(); iAddr++) {
	addr_t addr = *iAddr;
	if (m_reference_count_table.count(addr) > 0) {
	    assert(m_reference_count_table[addr].first > 0 || m_reference_count_table[addr].second > 0);
	    if (m_reference_count_table[addr].second > 0) {
	        m_reference_count_table[addr].second--;
	        if (m_reference_count_table[addr].second == 0) {
                    ce.add_removed_wr_addr(addr);
	        }
	    }
	    if (m_reference_count_table[addr].first + m_reference_count_table[addr].second == 0) {
	        m_reference_count_table.erase(addr);
	    }
	}
    }
    dec_cycles += wr_buffer.size();
    g_tm_global_statistics.m_reference_count_table_size.add2bin(m_reference_count_table.size());
}

unsigned commit_unit::num_refCountTable(addr_t waddr, bool rd) {
    if (m_reference_count_table.count(waddr) > 0) {
	assert(m_reference_count_table[waddr].first > 0 || m_reference_count_table[waddr].second > 0);
	if (rd) {
            return m_reference_count_table[waddr].first;
	} else {
	    return m_reference_count_table[waddr].second;
	}
    } else {
        return 0;
    }
}

void commit_unit::broadcast_newly_inserted_addr() {
    extern gpgpu_sim *g_the_gpu;
    unsigned num_shader = g_the_gpu->get_config().shader_config().num_shader();
    if (m_newly_inserted_addr_rd.size() >0){
        for (unsigned sid = 0; sid < num_shader; sid++) {
            unsigned tpc = g_the_gpu->get_config().shader_config().sid_to_cluster(sid);
	    // Idealize the packet size as TX_PACKET_SIZE, otherwise simulation cannnot be done
	    // so the impact of broadcast mechanism is pretty big
            mem_fetch *r = new mem_fetch( mem_access_t(TX_MSG,0xDEADBEEF,0,false),NULL,TX_PACKET_SIZE,0,sid,tpc,m_memory_config );
            r->set_early_abort_read();
            r->set_early_abort_inserted();
            r->set_type(NEWLY_INSERTED_ADDR);
            r->set_early_abort_addr_set(m_newly_inserted_addr_rd);
            m_response_queue.push_back(r);
	    g_tm_global_statistics.m_tot_early_abort_messages++;
        }
	g_tm_global_statistics.m_num_newly_inserted_addr.add2bin(m_newly_inserted_addr_rd.size());
	g_tm_global_statistics.m_num_early_abort_addr.add2bin(m_newly_inserted_addr_rd.size());
	m_newly_inserted_addr_rd.clear();
    }
    if (m_newly_inserted_addr_wr.size() >0){
        for (unsigned sid = 0; sid < num_shader; sid++) {
            unsigned tpc = g_the_gpu->get_config().shader_config().sid_to_cluster(sid);
	    // Idealize the packet size as TX_PACKET_SIZE, otherwise simulation cannnot be done
	    // so the impact of broadcast mechanism is pretty big
            mem_fetch *r = new mem_fetch( mem_access_t(TX_MSG,0xDEADBEEF,0,false),NULL,TX_PACKET_SIZE,0,sid,tpc,m_memory_config );
            r->set_early_abort_inserted();
            r->set_type(NEWLY_INSERTED_ADDR);
            r->set_early_abort_addr_set(m_newly_inserted_addr_wr);
            m_response_queue.push_back(r);
	    g_tm_global_statistics.m_tot_early_abort_messages++;
        }
	g_tm_global_statistics.m_num_newly_inserted_addr.add2bin(m_newly_inserted_addr_wr.size());
	g_tm_global_statistics.m_num_early_abort_addr.add2bin(m_newly_inserted_addr_wr.size());
	m_newly_inserted_addr_wr.clear();
    }
}

void commit_unit::broadcast_removed_addr() {
    extern gpgpu_sim *g_the_gpu;
    unsigned num_shader = g_the_gpu->get_config().shader_config().num_shader();
    if (m_removed_addr_rd.size() >0){
        for (unsigned sid = 0; sid < num_shader; sid++) {
            unsigned tpc = g_the_gpu->get_config().shader_config().sid_to_cluster(sid);
	    // Idealize the packet size as TX_PACKET_SIZE, otherwise simulation cannnot be done
	    // so the impact of broadcast mechanism is pretty big
            mem_fetch *r = new mem_fetch( mem_access_t(TX_MSG,0xDEADBEEF,0,false),NULL,TX_PACKET_SIZE,0,sid,tpc,m_memory_config );
            r->set_early_abort_read();
            r->set_type(REMOVED_ADDR);
            r->set_early_abort_addr_set(m_removed_addr_rd);
            m_response_queue.push_back(r);
	    g_tm_global_statistics.m_tot_early_abort_messages++;
        }
	g_tm_global_statistics.m_num_removed_addr.add2bin(m_removed_addr_rd.size());
	g_tm_global_statistics.m_num_early_abort_addr.add2bin(m_removed_addr_rd.size());
	m_removed_addr_rd.clear();
    }
    if (m_removed_addr_wr.size() >0){
        for (unsigned sid = 0; sid < num_shader; sid++) {
            unsigned tpc = g_the_gpu->get_config().shader_config().sid_to_cluster(sid);
	    // Idealize the packet size as TX_PACKET_SIZE, otherwise simulation cannnot be done
	    // so the impact of broadcast mechanism is pretty big
            mem_fetch *r = new mem_fetch( mem_access_t(TX_MSG,0xDEADBEEF,0,false),NULL,TX_PACKET_SIZE,0,sid,tpc,m_memory_config );
            r->set_type(REMOVED_ADDR);
            r->set_early_abort_addr_set(m_removed_addr_wr);
            m_response_queue.push_back(r);
	    g_tm_global_statistics.m_tot_early_abort_messages++;
        }
	g_tm_global_statistics.m_num_removed_addr.add2bin(m_removed_addr_wr.size());
	g_tm_global_statistics.m_num_early_abort_addr.add2bin(m_removed_addr_wr.size());
	m_removed_addr_wr.clear();
    }
}

void commit_unit::newly_inserted_addr_union(commit_entry &ce) {
    const std::set<addr_t> &addr_set_rd = ce.get_newly_inserted_addr_rd();
    const std::set<addr_t> &addr_set_wr = ce.get_newly_inserted_addr_wr();
    std::set<addr_t>::const_iterator iter;
    for (iter = addr_set_rd.begin(); iter != addr_set_rd.end(); iter++) {
        m_newly_inserted_addr_rd.insert(*iter);
    }
    for (iter = addr_set_wr.begin(); iter != addr_set_wr.end(); iter++) {
        m_newly_inserted_addr_wr.insert(*iter);
    }
}

void commit_unit::removed_addr_union(commit_entry &ce) {
    const std::set<addr_t> &addr_set_rd = ce.get_removed_addr_rd();
    const std::set<addr_t> &addr_set_wr = ce.get_removed_addr_wr();
    std::set<addr_t>::const_iterator iter;
    for (iter = addr_set_rd.begin(); iter != addr_set_rd.end(); iter++) {
        m_removed_addr_rd.insert(*iter);
    }
    for (iter = addr_set_wr.begin(); iter != addr_set_wr.end(); iter++) {
        m_removed_addr_wr.insert(*iter);
    }
}
