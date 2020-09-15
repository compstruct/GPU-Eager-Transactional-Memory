#ifndef COMMIT_UNIT_H
#define COMMIT_UNIT_H

#include "gpu-cache.h"
#include "shader.h"
#include "../option_parser.h"
#include "../tr1_hash_map.h"
#include "../cuda-sim/bloomfilter.h"
#include "../cuda-sim/hashfunc.h"

#include "../gpgpu-sim/visualizer.h"

#include <bitset>
#include <unordered_set>
#include <deque>
#include <queue>
#include <set>

#define MAX_CORES 64

extern tm_options g_tm_options;
extern tm_global_statistics g_tm_global_statistics;

// read-set/write-set buffer in an entry in commit unit 
class cu_access_set
{
public:
    cu_access_set(); 
    ~cu_access_set(); 

    // append the linear buffer and update the bloom filter 
    void append(new_addr_type addr); 

    void reset(); 

    // is linear buffer overflowing? 
    bool overflow() const; 

    size_t linear_buffer_usage() const { return m_linear_buffer.size(); }

    // check if this access set buffer contains a given address 
    bool match(new_addr_type addr) const; 

    // for revalidation and commit 
    typedef std::list<new_addr_type> linear_buffer_t; 
    const linear_buffer_t& get_linear_buffer() const { return m_linear_buffer; }

    // update version number for given address 
    void update_version(new_addr_type addr, int version);
    int get_version(new_addr_type addr) const; 

    // prematurally deallocate the bloom filter to save memory 
    void delete_bloomfilter(); 

    void print(FILE *fout) const; 

private:
    // linear buffer for storing <address, value> pair (value is in tm_manager)
    linear_buffer_t m_linear_buffer;
    int m_linear_buffer_limit; 

    // for fast perfect matching and for version tracking (data race detection)
    typedef tr1_hash_map<new_addr_type, int> addr_hashset_t; 
    addr_hashset_t m_addr_hashtable; 

    // bloom filter -- to be added 
    bloomfilter *m_bloomfilter; 
};

enum commit_state 
{
   UNUSED = 0,
   FILL, 
   HAZARD_DETECT, 
   VALIDATION_WAIT,
   REVALIDATION_WAIT,
   PASS,
   FAIL,
   PASS_ACK_WAIT,
   COMMIT_READY,
   COMMIT_SENT,
   RETIRED,
   N_COMMIT_STATE
};

enum cu_mem_op 
{
   NON_CU_OP = 0, 
   VALIDATE,
   COMMIT_WRITE
}; 

class commit_unit_stats;
class tm_manager_inf;

class commit_entry 
{
public:
    commit_entry(int commit_id);

    int get_commit_id() const { return m_commit_id; }
    int get_wid() const { return m_wid; }
    int get_sid() const { return m_sid; }
    int get_tpc() const { return m_tpc; }
    void set_tx_origin(int sid, int tpc, int wid = -1);

    int get_youngest_conflicting_commit_id() const { return m_youngest_conflicting_commit_id; }
    void set_youngest_conflicting_commit_id (int youngest_commit_id) {
        if(youngest_commit_id > m_youngest_conflicting_commit_id)
            m_youngest_conflicting_commit_id = youngest_commit_id;
    }

    void set_state(enum commit_state state); 
    enum commit_state get_state() const { return m_state; }

    // update VP counter and pass-fail status 
    void sent_validation() { m_n_validation_pending += 1; }
    void validation_return(bool pass); 
    bool validation_pending() const { return (m_n_validation_pending != 0); }

    // ideally we can just use the write_buffer to keep track of this, but this is simpler 
    void sent_commit_write() { m_n_commit_write_pending += 1; }
    void commit_write_done() { m_n_commit_write_pending -= 1; assert(m_n_commit_write_pending >= 0); }
    bool commit_write_pending() const { return (m_n_commit_write_pending != 0); }

    bool fail() const { return m_fail; }
    void set_fail() { m_fail = true; } // used only by fail_at_revalidation

    // set/get revalidate
    void set_revalidate(bool revalidate) { 
       m_revalidate = revalidate; 
       m_revalidate_set = m_revalidate_set or revalidate; 
    }
    bool get_revalidate() const { return m_revalidate; }

    bool was_revalidate_set() const { return m_revalidate_set; }

    // allow direct access to read_set and write_set buffers 
    cu_access_set& read_set() { return m_read_set; }
    cu_access_set& write_set() { return m_write_set; }
    const cu_access_set& read_set() const { return m_read_set; }
    const cu_access_set& write_set() const { return m_write_set; }

    // for debugging correctness of the commit process
    void send_reply() { 
       assert(m_reply_sent == false); 
       m_reply_sent = true; 
    }
    void set_final(bool pass) { m_final_pass = pass; }
    bool get_final() const { return m_final_pass; }
    void set_skip() { m_skip_entry = true; }
    bool was_skip() const { return m_skip_entry; }
    void send_commit_ack() { m_commit_ack_sent = true; }

    void print(FILE *fout); 
    void at_retire_ptr(class commit_unit_stats &stats, unsigned time); 
    void at_retire_ptr_logical(class commit_unit_stats &stats, unsigned time); 

    // assign tm manager to a commit entry for functional validation and commit 
    void set_tm_manager(class tm_manager_inf* tm_manager);
    class tm_manager_inf* get_tm_manager() { return m_tm_manager; }
    void delete_tm_manager(); 
    void set_commit_pending_ptr(std::bitset<16>* commit_pending) { m_commit_pending_flag = commit_pending; }
    void clear_commit_pending(unsigned mpid) { 
       if (m_commit_pending_flag) 
          m_commit_pending_flag->reset(mpid); 
    } 

    void set_timestamp_file(FILE *timestamp_file, int mpid) { m_timestamp_file = timestamp_file; m_mpid = mpid; } 
    void dump_timestamps(); 

    void set_retire_cid_at_fill( int retire_ptr ) { m_retire_ptr_at_fill = retire_ptr; }
    int get_retire_cid_at_fill() const { return m_retire_ptr_at_fill; }

    new_addr_type get_next_delayfcd_read();
    new_addr_type get_next_delayfcd_write();

    bool delayfcd_reads_done() const { return m_delayfcd_reads_checked == read_set().get_linear_buffer().size(); }
    bool delayfcd_writes_done() const { return m_delayfcd_writes_stored == write_set().get_linear_buffer().size(); }

    bool were_delayfcd_writes_stored() const { return m_delayfcd_writes_stored > 0; }

    // Functions for the LSU HPCA2016 Early Abort paper
    const std::set<addr_t> &get_newly_inserted_addr_rd() const { return m_newly_inserted_addr_rd; }
    const std::set<addr_t> &get_removed_addr_rd() const { return m_removed_addr_rd; }
    const std::set<addr_t> &get_newly_inserted_addr_wr() const { return m_newly_inserted_addr_wr; }
    const std::set<addr_t> &get_removed_addr_wr() const { return m_removed_addr_wr; }
    void add_newly_inserted_rd_addr(addr_t addr) { m_newly_inserted_addr_rd.insert(addr); }
    void add_removed_rd_addr(addr_t addr) { m_removed_addr_rd.insert(addr); } 
    void add_newly_inserted_wr_addr(addr_t addr) { m_newly_inserted_addr_wr.insert(addr); }
    void add_removed_wr_addr(addr_t addr) { m_removed_addr_wr.insert(addr); } 

private:
    int m_commit_id; 
    int m_wid; 
    int m_sid;
    int m_tpc; 
    int m_mpid; 
    enum commit_state m_state;
    int m_n_validation_pending; 
    bool m_fail; 
    bool m_revalidate; 

    int m_youngest_conflicting_commit_id;

    int m_retire_ptr_at_fill; // where retire ptr was at first validation

    int m_n_commit_write_pending; 

    cu_access_set m_read_set; 
    cu_access_set m_write_set; 

    bool m_reply_sent; // to detect redundant replies 
    bool m_final_pass;
    bool m_revalidate_set; // the revalidation flag was set 
    bool m_skip_entry; // this is a skip entry 
    bool m_commit_ack_sent; // to detect missing/redundant commit ack


    unsigned m_delayfcd_reads_checked; // number of reads checked against CD table
    unsigned m_delayfcd_writes_stored; // number of writes written to CD table

    // for functional commit 
    class tm_manager_inf* m_tm_manager; 
    std::bitset<16>* m_commit_pending_flag;

    // stats 
    unsigned m_alloc_time; 
    unsigned m_fill_time; 
    unsigned m_validation_wait_time;
    unsigned m_revalidation_wait_time;
    unsigned m_pass_fail_time;
    unsigned m_ack_wait_time; 
    unsigned m_commit_ready_time;
    unsigned m_commit_sent_time;
    unsigned m_retired_time;

    FILE *m_timestamp_file;

    // Data needed for the LSU HPCA2016 Early Abort paper
    std::set<addr_t> m_newly_inserted_addr_rd;
    std::set<addr_t> m_newly_inserted_addr_wr;
    std::set<addr_t> m_removed_addr_rd;
    std::set<addr_t> m_removed_addr_wr;
}; 

// a collection of commit entries that should act as a warp 
class warp_commit_entry
{
public: 
   warp_commit_entry(int sid = -1, int wid = -1);

   void reset();

   // add commit entry to this group 
   void link_to_commit_id(const commit_entry& cmt_entry); 

   // inform the warp that this particular entry has finished validation 
   void signal_validation_done(const commit_entry& cmt_entry); 
   // inform the warp of the final pass/fail of this particular entry 
   void signal_final_outcome(const commit_entry& cmt_entry); 
   // inform the warp that this particular entry has finished commit 
   void signal_commit_done(const commit_entry& cmt_entry); 

   // inform the warp that this entry has finished hazard detection 
   void signal_hazard_detection_read_done(const commit_entry& cmt_entry); 
   // inform the warp that this entry has finished updating conflict table 
   void signal_hazard_detection_write_done(const commit_entry& cmt_entry); 

   // accessors 
   int get_max_commit_id() const { return m_max_commit_id; }
   int get_max_commit_id_with_skip() const { return m_max_commit_id_with_skip; }
   int get_sid() const { return m_sid; }
   int get_wid() const { return m_wid; }

   bool all_validation_done() const; 
   bool all_commit_done() const; 
   bool commit_ack_pending(const commit_entry& cmt_entry) const; 

   bool hazard_detection_reads_done() const; 
   bool hazard_detection_writes_done() const; 

   const std::vector<int>& get_commit_ids() const { return m_commit_ids; }

   void dump(FILE *fout) const; 

private: 
   std::vector<int> m_commit_ids; 
   int m_max_commit_id; 
   int m_max_commit_id_with_skip; 
   int m_sid; 
   int m_wid; 

   typedef std::bitset<32> active_mask_t; 
   active_mask_t m_active_mask; 
   active_mask_t m_validation_done_mask; 
   active_mask_t m_commit_ack_pending_mask; 
   active_mask_t m_commit_done_mask; 

   active_mask_t m_hazard_detection_read_done_mask; 
   active_mask_t m_hazard_detection_write_done_mask; 

   int get_lane(const commit_entry& cmt_entry) const; 
};

class conflict_table_hash {
public:
   conflict_table_hash(unsigned n_sets, unsigned n_ways);
   ~conflict_table_hash();

   bool check_read_conflict(new_addr_type addr, int cid, int retire_cid_at_fill, int& conflicting_cid) const;
   bool store_write(new_addr_type addr, int cid, new_addr_type& evicted_addr, int& evicted_cid);

   bool probe(new_addr_type addr, int& probed_entry_idx) const;
   void print(FILE* fp, new_addr_type addr);

private:

   struct conflict_table_hash_entry {
      new_addr_type addr;
      int commit_id;
      bool valid;
      conflict_table_hash_entry(new_addr_type addr=0, int cid=0) : addr(addr), commit_id(cid), valid(false) {}
   };
   conflict_table_hash_entry* m_lines;

   const shader_core_config* m_shader_config;
   unsigned m_sets;
   unsigned m_ways;
   unsigned m_size;


};

class conflict_table_perfect {
public:
   conflict_table_perfect(unsigned addr_granularity);
   ~conflict_table_perfect();

   bool check_read_conflict(new_addr_type addr, int cid, int retire_cid_at_fill, int& conflicting_cid);
   void store_write(new_addr_type addr, int cid);

   void register_read(new_addr_type addr);
   void clear_writes(commit_entry &ce);

   int size() const { return m_active_entries; }
   int count_active_entries() const;

   bool check_entry(new_addr_type addr, int cid) const;
   void print(FILE *fp, new_addr_type addr) const;

private:
   struct conflict_table_entry {
      int commit_id;
      unsigned read_counter;
      bool active;
      conflict_table_entry(int cid = 0) : commit_id(cid), read_counter(0), active(true) {}
   };

   typedef std::map<new_addr_type,conflict_table_entry> conflict_table_t;

   conflict_table_t m_conflict_table;
   int m_active_entries;

   new_addr_type quantize_address(new_addr_type addr); 
   new_addr_type m_addr_quantize_mask; 
};

class conflict_detector {
public:
   conflict_detector(unsigned n_hash_sets, unsigned n_hash_ways, unsigned bf_size, unsigned bf_n_funcs, unsigned addr_granularity);
   ~conflict_detector();

   bool check_read_conflict(new_addr_type addr, int cid, int retire_cid_at_fill, int& conflicting_cid);
   void store_write(new_addr_type addr, int cid);

   void register_read(new_addr_type addr);
   void clear_writes(commit_entry &ce);

   int size() const { return m_conflict_table_perfect.size(); }

private:
   new_addr_type quantize_address(new_addr_type addr); 
   new_addr_type m_addr_quantize_mask; 

   conflict_table_hash m_conflict_table_hash;
   conflict_table_perfect m_conflict_table_perfect;
   versioning_bloomfilter* m_conflict_table_bf;
};


class commit_unit_mf_allocator;

class commit_unit 
{
public:
    commit_unit( const memory_config *memory_config, 
                 const shader_core_config *shader_config, 
                 unsigned partition_id, 
                 mem_fetch_interface *port, 
                 std::set<mem_fetch*> &request_tracker, 
                 std::queue<rop_delay_t> &rop2L2 );

    ~commit_unit(); 

    // Unit is busy if retire ptr is behind head or there are remaining input messages 
    bool get_busy(); 

    // Unit is full if input message queue is full 
    bool full(); 

    // snoop reply from memory partition for validation or commit, return true if mf is processed 
    bool snoop_mem_fetch_reply(mem_fetch *mf);

    // process queued work
    virtual void cycle(unsigned long long time);

    // return false if request should be sent to DRAM after accessing directory
    bool access( mem_fetch *mf, unsigned time );

    // return the operation type of a mem_fetch from commit unit, or return INVALID
    enum cu_mem_op check_cu_access( mem_fetch *mf ); 
    bool ideal_L2_cache(enum cu_mem_op mem_op); 
    void record_cache_hit(enum cu_mem_op mem_op, enum cache_request_status status); 

    void print_sanity_counters(FILE *fp);

    void visualizer_print(gzFile visualizer_file); 

    void dump(); 

    unsigned get_sent_icnt_traffic() const { return m_sent_icnt_traffic; } 

protected:
    const memory_config *m_memory_config;
    const shader_core_config *m_shader_config;
    unsigned m_partition_id;

    // Conflict detector table
    conflict_detector m_conflict_detector;

    // interfaces
    std::list<mem_fetch*> m_input_queue; // input messages 
    std::list<mem_fetch*> m_response_queue; // reply messages generated
    mem_fetch_interface *m_response_port;
    std::set<mem_fetch*> &m_request_tracker; // tracking sent L2 access 
    commit_unit_mf_allocator *m_mf_alloc; // for generating L2 access 
    std::queue<rop_delay_t> &m_rop2L2; // modeling delay for L2 access 

    // internal states 
    int m_cid_at_head; // the youngest commit id in the unit 
    int m_cid_fcd; // the oldest commit id that has yet to pass fast conflict detection
    int m_cid_pass; // the oldest commit id that has yet to validate or pass
    int m_cid_retire; // the oldest commit id that has retired
    int m_cid_commit; // the oldest commit id that has yet to send writeset for committing
    typedef std::deque<commit_entry> commit_entry_table_t;
    commit_entry_table_t m_commit_entry_table; 

    // access function to decouple commit id and location in commit entry table 
    // - every access to commit table (except scrub) should go through this 
    commit_entry& get_commit_entry(int commit_id); 

    // deallocate entries from commit unit that are no longer in use
    void scrub_retired_commit_entries(); 

    // warp-level grouping of commit entries 
    typedef std::map< std::pair<int, int>, warp_commit_entry> warp_commit_entry_table; 
    warp_commit_entry_table m_warp_commit_entry_table; 

    // return the warp_commit_entry corresponding to the core/warp of the message 
    warp_commit_entry & get_warp_commit_entry_for_msg( mem_fetch *input_msg, unsigned time ); 
    // return the warp commit entry for warp <warp_id> at core <core_id>
    warp_commit_entry & get_warp_commit_entry( int core_id, int warp_id ); 

    // connection between commit unit and L2 cache 
    class cu_mem_acc {
    public:
       static const new_addr_type word_size = 4; 
       int commit_id;
       new_addr_type addr;
       new_addr_type size;
       enum cu_mem_op operation;
       unsigned issue_cycle; 
       std::list<cu_mem_acc> m_coalesced_ops; 
       cu_mem_acc(int cid = -1, new_addr_type adr = 0xDEADBEEF, enum cu_mem_op op = NON_CU_OP, unsigned time = -1) 
          : commit_id(cid), addr(adr), size(word_size), operation(op), issue_cycle(time) { }
       void set_size(new_addr_type new_size) { 
          assert(addr % new_size == 0); 
          size = new_size; 
       }
       void append_coalesced_op(new_addr_type block_addr, cu_mem_acc &mem_op); 
       bool has_coalesced_ops() const { return (not m_coalesced_ops.empty()); }
       cu_mem_acc& next_coalesced_op() { return m_coalesced_ops.front(); }
       void pop_coalesced_op() { m_coalesced_ops.pop_front(); }
    }; 
    typedef std::list<cu_mem_acc> mem_op_queue_t; 
    mem_op_queue_t m_validation_queue; 
    mem_op_queue_t m_commit_queue; 
    mem_op_queue_t m_validation_coalescing_queue; 
    mem_op_queue_t m_commit_coalescing_queue; 
    // transfer the memory operations from m_mem_op_coalescing_queue to validation or commit queue 
    void transfer_ops_to_queue(enum cu_mem_op operation); 
    // coalesce the memory operations from m_mem_op_coalescing_queue and transfer the coalesced operations to validation or commit queue 
    void coalesce_ops_to_queue(enum cu_mem_op operation); 

    // extra fields in mem_fetch private to commit unit 
    typedef tr1_hash_map<mem_fetch*, cu_mem_acc> cu_mem_fetch_lookup; 
    cu_mem_fetch_lookup m_cu_mem_fetch_fields; 

    // look up table to initiate revalidation when a transaction retire 
    typedef std::multimap<int, int> cu_revalidation_lookup; // <retire CID, revalidate CID> 
    cu_revalidation_lookup m_revalidation_table; 

    // structure needed for LSU HPCA2016 Early Abort paper
    typedef std::map<addr_t, std::pair<unsigned, unsigned> > reference_count_table;  // entry format: address##rd_count##wr_count
    reference_count_table m_reference_count_table;
    std::set<addr_t> m_newly_inserted_addr_rd;
    std::set<addr_t> m_removed_addr_rd; 
    std::set<addr_t> m_newly_inserted_addr_wr;
    std::set<addr_t> m_removed_addr_wr; 

    // helper functions

    // sanity counters -- input packets 
    unsigned m_n_tx_read_set; 
    unsigned m_n_tx_write_set; 
    unsigned m_n_tx_done_fill;
    unsigned m_n_tx_skip;
    unsigned m_n_tx_pass; 
    unsigned m_n_tx_fail; 
    // sanity counters -- activities
    unsigned m_n_input_pkt_processed; 
    unsigned m_n_recency_bf_activity; 
    unsigned m_n_reply_sent; 
    unsigned m_n_validations; 
    unsigned m_n_validations_processed; 
    unsigned m_n_commit_writes;
    unsigned m_n_commit_writes_processed;
    unsigned m_n_revalidations; 
    // power model counter
    unsigned m_sent_icnt_traffic; 

    int m_n_active_entries; // entries that are between head and retire, not skipped or unused
    int m_n_active_entries_have_rs; // active entries that have a read-set
    int m_n_active_entries_have_ws; // active entries that have a write-set
    int m_n_active_entries_need_rs; // active entries that need a read-set (no RS needed after PASS/FAIL)
    int m_n_active_entries_need_ws; // active entries that need a write-set (no WS needed after FAIL)

    // number of cycle the pointers has been stall since last increment
    int m_cid_fcd_stall_cycles;
    int m_cid_pass_stall_cycles; 
    int m_cid_commit_stall_cycles; 
    int m_cid_retire_stall_cycles; 

    FILE *m_timestamp_file;

    // helper functions 
    // process input messages
    virtual void process_input( mem_fetch *mf, unsigned time );
    // send a reply packet to a specific core 
    void send_reply( unsigned sid, unsigned tpc, unsigned wid, unsigned commit_id, enum mf_type reply_type );
    // send a scalar reply packet to a specific core 
    void send_reply_scalar( unsigned sid, unsigned tpc, unsigned wid, unsigned commit_id, enum mf_type reply_type );
    // send a coalesced reply packet to a specific core 
    void send_reply_coalesced( unsigned sid, unsigned tpc, unsigned wid, unsigned commit_id, enum mf_type reply_type );
    // if not done yet, allocate entries all the way up to the given commit_id
    void allocate_to_cid(int commit_id, enum mf_type type); 
    // advance given commit entry to pass or fail state
    void done_validation_wait(commit_entry& ce);
    // advance given commit entry to pass_ack_wait or retired state and send the reply accordingly
    void done_revalidation_wait(commit_entry& ce);
    // advance given commit entry to commit state and send all the commit writes 
    void start_commit_state(commit_entry& ce); 
    // resend validation requests 
    void revalidation(commit_entry &ce);
    // check for conflict between a incoming read and write set of the older transactions
    // sets youngest_conflicting_commit_id to id of youngest (largest cid) conflicting commit entry
    bool check_conflict_for_read(int commit_id, new_addr_type read_addr);
    // check for conflict between a incoming write and read set of the younger transactions 
    // if detected, set revalidate flag of the younger transactions
    void check_conflict_for_write(int commit_id, new_addr_type write_addr); 
    // send a mem_fetch to L2 via the ROP path for validation or commit write
    void send_to_L2(unsigned long long time, commit_unit::cu_mem_acc mem_op);
    // get current size of the commit unit
    unsigned get_allocated_size() const {
       assert(m_cid_at_head - m_cid_retire + 1 >= 0);
       return m_cid_at_head - m_cid_retire + 1;
    }
    // generate commit ack traffic to allow the committing thread to advance 
    void commit_done_ack(commit_entry &ce);

    // process coalesced input messages in serial 
    void process_coalesced_input_serial( mem_fetch *input_msg, unsigned time );
    // process coalesced input messages in parallel 
    void process_coalesced_input_parallel( mem_fetch *input_msg, unsigned time );
    // return the number of coalesced access generated from a given group of packets 
    unsigned num_coalesced_accesses( const std::list<mem_fetch*>& packets ); 

    // process a scalar validation operation returned from L2 cache 
    void process_validation_op_reply(mem_fetch *mf, const cu_mem_acc &mem_op, unsigned time);
    // process a scalar commit operation returned from L2 cache 
    virtual void process_commit_op_reply(mem_fetch *mf, const cu_mem_acc &mem_op, unsigned time);

    // internal functions called by cycle()
    void check_and_advance_fcd_ptr(unsigned long long time);
    void check_and_advance_fcd_ptr_warp_level(unsigned long long time);
    void check_and_advance_pass_ptr(unsigned long long time); 
    void check_and_advance_pass_ptr_vwait_nostall(unsigned long long time); 
    virtual void check_and_advance_commit_ptr(unsigned long long time); 
    void check_and_advance_commit_ptr_vwait_nostall(unsigned long long time); 
    virtual void check_and_advance_retire_ptr(unsigned long long time); 
    void check_and_advance_retire_ptr_vwait_nostall(unsigned long long time); 

    void update_youngest_conflicting_commit_id(int conflictor_id, commit_entry &reval_ce); 

    // for debugging/detecting data race 
    typedef tr1_hash_map<addr_t, int> mem_version_t;
    mem_version_t m_update_done_mem_version; // the latest commit_id that updates the global memory location
    mem_version_t m_update_sent_mem_version; // the latest commit_id that has sent a update request 

    void check_read_set_version(const commit_entry &ce);

    // Functions for LSU HPCA2016 Early Abort Paper
    bool is_refCountTable_full();
    void inc_refCountTable(addr_t waddr, bool rd);
    void dec_refCountTable_rd(commit_entry &ce, int &dec_cycles);
    void dec_refCountTable_wr(commit_entry &ce, int &dec_cycles);
    unsigned num_refCountTable(addr_t waddr, bool rd);
    void broadcast_newly_inserted_addr();
    void newly_inserted_addr_union(commit_entry &ce);
    void broadcast_removed_addr();
    void removed_addr_union(commit_entry &ce);
};

void commit_unit_reg_options(option_parser_t opp);
void commit_unit_statistics(FILE *fout);

class commit_unit_logical : public commit_unit {
public:
    commit_unit_logical( const memory_config *memory_config, 
                         const shader_core_config *shader_config, 
                         unsigned partition_id, 
                         mem_fetch_interface *port, 
                         std::set<mem_fetch*> &request_tracker, 
                         std::queue<rop_delay_t> &rop2L2 )
    : commit_unit(memory_config, shader_config, partition_id, port, request_tracker, rop2L2) {};
    ~commit_unit_logical() {}; 
    
    // process queued work
    virtual void cycle(unsigned long long time);
protected:
    // process input messages
    virtual void process_input( mem_fetch *mf, unsigned time );
    // process a scalar commit operation returned from L2 cache 
    virtual void process_commit_op_reply(mem_fetch *mf, const cu_mem_acc &mem_op, unsigned time);
    // internal functions called by cycle()
    virtual void check_and_advance_commit_ptr(unsigned long long time); 
    virtual void check_and_advance_retire_ptr(unsigned long long time); 
};

#endif

