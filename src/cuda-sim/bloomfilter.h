#pragma once 

#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

#include "../abstract_hardware_model.h" 
#include "../option_parser.h" 

#include <stdio.h>
#include <bitset>
#include <vector> 

// pointer to hash function 
typedef unsigned int (*hash_funct_ptr)(unsigned int size, addr_t addr); 

void bloomfilter_reg_options(option_parser_t opp);

// generic hashtable interface 
class hashtable
{
public:
   hashtable(unsigned int size, int funct_id); 
   virtual ~hashtable() {} 

   virtual int add(addr_t addr) = 0; // for read/write set creation, return position modified if signature is modified 
   virtual void set_bitpos(unsigned int bitpos) = 0; // for partial update of global conflict table 

   // only possible for counter-based hashtable 
   // guard against overflow? 
   virtual void remove(addr_t addr) = 0; 
   virtual void remove(const hashtable& signature) = 0; 

   // match against a single address
   virtual bool match(addr_t addr) const = 0; 
   virtual bool match(const hashtable& signature) const = 0; 

   // clear all entries in the hashtable 
   virtual void clear() = 0; 

   // print hashtable content 
   virtual void print(FILE *fout) const = 0; 

   // exposed hash function
   unsigned int hash(addr_t addr) const { return m_hash_funct_ptr(m_size, addr); } 

   // accessor helper functions 
   // size of the table 
   unsigned int size() const { return m_size; } 
   unsigned int funct_id() const { return m_funct_id; } 

   static const int s_nullpos; 

protected:
   unsigned int m_size; 
   int m_funct_id; 
   hash_funct_ptr m_hash_funct_ptr; 
};


// hashtable with bit-based signature 
template<int sig_limit>
class hashtable_bits : public hashtable
{
public: 
   hashtable_bits(unsigned int size, int funct_id); 

   virtual int add(addr_t addr); // for read/write set creation
   virtual void set_bitpos(unsigned int bitpos); // for partial update of global conflict table 

   // only possible for counter-based hashtable 
   // guard against overflow? 
   virtual void remove(addr_t addr); 
   virtual void remove(const hashtable& signature); 

   // match against a single address
   virtual bool match(addr_t addr) const; 
   virtual bool match(const hashtable& signature) const; 

   // clear all entries in the hashtable 
   virtual void clear(); 

   // print hashtable content 
   virtual void print(FILE *fout) const; 

   // accessor helper functions 
   // return true if signature bit at 'bitpos' is set 
   bool test_signature(unsigned int bitpos) const { return m_signature.test(bitpos); }

protected: 
   int m_obj_type; // safety check to ensure that we do not cast pointer to hashtable_bits into hashtable_counter 

   typedef std::bitset<sig_limit> sig_t; 
   sig_t m_signature; // the bit array containing the approximate representation of the hashed set 
}; 


// hashtable with counter-based signature (support set member removal) 
#define htctr_sig_limit 1024
class hashtable_counter : public hashtable_bits<htctr_sig_limit>
{
public: 
   hashtable_counter(unsigned int size, int funct_id); 

   virtual int add(addr_t addr); // for read/write set creation
   virtual void set_bitpos(unsigned int bitpos); // for partial update of global conflict table 

   // only possible for counter-based hashtable 
   // guard against overflow? 
   virtual void remove(addr_t addr); 
   virtual void remove(const hashtable& signature); 

   // match against a single address
   virtual bool match(addr_t addr) const; 
   virtual bool match(const hashtable& signature) const; 

   // clear all entries in the hashtable 
   virtual void clear(); 

   // print hashtable content 
   virtual void print(FILE *fout) const; 

   // print counters in all the buckets
   void print_counters(FILE *fout) const; 

protected: 
   typedef std::vector<int> countertable_t;
   countertable_t m_countertable; // array of counters tracking the set 

   void dec_counter(unsigned pos); 
}; 


// hashtable with bit-based signature for a set of threads 
class hashtable_bits_mt : public hashtable
{
public: 
   static const int s_nthread_limit = 2048; 
   typedef std::bitset<s_nthread_limit> tvec_t; 
   
   static const int s_tid_hash_limit = 512; 
   typedef std::bitset<s_tid_hash_limit> hashed_tvec_t; 

public: 
   hashtable_bits_mt(unsigned int size, int funct_id, unsigned int nthreads, unsigned int tid_hashsize); 
   static void reg_options(option_parser_t opp); 

   // select a particular thread to operate
   void select_thread(int thread_id) { 
      assert((unsigned)thread_id < m_nthreads); 
      m_selected_thread = thread_id; 
   } 
   // unselect the current selected thread
   void unselect_thread() { m_selected_thread = s_null_tid; } 

   virtual int add(addr_t addr); // for read/write set creation
   virtual void set_bitpos(unsigned int bitpos); // for partial update of global conflict table 

   // only possible for counter-based hashtable 
   // guard against overflow? 
   virtual void remove(addr_t addr); 
   virtual void remove(const hashtable& signature); 

   // match against a single address
   virtual bool match(addr_t addr) const; 
   virtual bool match(const hashtable& signature) const; 

   // clear all entries in the hashtable (for the selected thread only)
   virtual void clear(); 

   // print hashtable content 
   virtual void print(FILE *fout) const; 

   // mt specific accessors
   const tvec_t& get_sigbit(addr_t addr) const {
      unsigned hashpos = hash(addr); 
      return m_signature[hashpos]; 
   }
   const hashed_tvec_t& get_hashed_sigbit(addr_t addr) const {
      unsigned hashpos = hash(addr); 
      return m_hashed_sig[hashpos]; 
   }
   // calculating the number of threads active in a given thread-hash signature 
   bool hashed_threadsig_populated(const hashed_tvec_t& combined_sig) const; 

   // expand a thread-hash signature to a full thread vector  
   tvec_t expand_hashed_sigbit(const hashed_tvec_t& hashed_sig) const; 
   tvec_t get_expanded_hashed_sigbit(addr_t addr) const {
      unsigned hashpos = hash(addr); 
      return expand_hashed_sigbit(m_hashed_sig[hashpos]); 
   }

   // mt specific modifier 
   void clear_all(); // clear signatures for all thread 

protected: 
   int m_obj_type; // safety check to ensure that we do not cast pointer to hashtable_bits into hashtable_counter 

   unsigned int m_nthreads; // # threads tracked in this filter 
   unsigned int m_tid_hashsize; // # bits in each hashed thread vector 

   static const int s_null_tid = -1; 
   int m_selected_thread; // thread id of the selected thread to be operated 

   std::vector<tvec_t> m_signature; // array of the bit arrays [sig-bit, tid] containing the approx. representation of the hashed set 

   std::vector<hashed_tvec_t> m_hashed_sig; 

   // internal helper functions and structures
   static int s_n_tid_subhash;
   std::vector<unsigned> m_tid_subhashsize; // size of each sub hash in the thread-bloomfilter 
   std::vector<tvec_t> m_hash_gen_mask; // mask that helps accelerate hash generation 
   void update_tvec_hash(unsigned int bitpos); 
}; 


// an implementation of bloomfilter based on an array of hashtables 
class bloomfilter
{
public:
   bloomfilter(unsigned int size, const std::vector<int>& funct_ids, unsigned int n_functs, bool counter_based); 
   bloomfilter(const bloomfilter& other); // copy constructor 
   bloomfilter& operator=(const bloomfilter& other); // assignment operator 
   ~bloomfilter(); 

   // add an address into the bloomfilter, return true if any of the hash is setting a new bit 
   // position of the new bits set returned via mod_pos
   bool add(addr_t addr, std::vector<int>& mod_pos); 
   bool add(addr_t addr); // simple version that does not return modified positions in the hashes

   void set_bitpos(const std::vector<int>& bitpos); // for partial update of global conflict table 

   // only possible for counter-based bloomfilter 
   void remove(addr_t addr); 
   void remove(const bloomfilter& signature); 

   // match against a single address
   bool match(addr_t addr) const; 
   // match against the signature from another bloomfilter 
   bool match(const bloomfilter& signature) const; 

   // clear all entries in the filter 
   void clear(); 

   // print hashtable content 
   void print(FILE *fout) const; 

   // accessor helper functions
   unsigned int size() const { return m_size; }
   size_t n_hashes() const { return m_n_hashes; }

private:
   unsigned int m_size; 
   size_t m_n_hashes; 
   bool m_counter_based; 

   typedef std::vector<hashtable*> hashtables_array; 
   hashtables_array m_hashtables; 
};


// bloom filter for a set of threads 
class bloomfilter_mt
{
public:
   bloomfilter_mt(unsigned int size, const std::vector<int>& funct_ids, unsigned int n_functs, unsigned int nthreads, unsigned int tid_hashsize); 
   bloomfilter_mt(const bloomfilter_mt& other); // copy constructor 
   bloomfilter_mt& operator=(const bloomfilter_mt& other); // assignment operator 
   ~bloomfilter_mt(); 

   static void reg_options(option_parser_t opp); 

   void select_thread(int thread_id); 
   void unselect_thread(); 

   // add an address into the bloomfilter, return true if any of the hash is setting a new bit 
   bool add(addr_t addr); // simple version that does not return modified positions in the hashes

   // match against a single address
   bool match(addr_t addr) const; 
   bool match_hashed(addr_t addr, hashtable_bits_mt::tvec_t& matched_threads) const; 
   bool match_hashexpanded(addr_t addr); 
   // match against the signature from another bloomfilter 
   bool match(const bloomfilter& signature); 

   // clear all entries in the filter 
   void clear(); // for selected thread 
   void clear_all(); // for all threads

   // accessor helper functions
   unsigned int size() const { return m_size; }
   size_t n_hashes() const { return m_n_hashes; }

private:
   unsigned int m_size; 
   size_t m_n_hashes; 
   
   unsigned int m_nthreads;
   unsigned int m_tid_hashsize; 

   typedef std::vector<hashtable_bits_mt*> hashtables_array; 
   hashtables_array m_hashtables; 

   static int s_tid_hashvar; 
};


// hash table that track the version of its member conservatively 
// (return version that is younger or equal to actual version)
class versioning_hashtable
{
public:
   versioning_hashtable(unsigned int size, int funct_id); 
   virtual ~versioning_hashtable() {} 

   // set version of a given address 
   void update_version(addr_t addr, unsigned int version); 

   // match against a single address
   unsigned int get_version(addr_t addr) const;

   // clear all entries in the hashtable 
   void clear();

   // print hashtable content 
   void print(FILE *fout) const; 

   // exposed hash function
   unsigned int hash(addr_t addr) const { return m_hash_funct_ptr(m_size, addr); } 

   // accessor helper functions 
   // size of the table 
   unsigned int size() const { return m_size; } 
   unsigned int funct_id() const { return m_funct_id; } 

   static void unit_test(); 

protected:
   unsigned int m_size; 
   int m_funct_id; 
   hash_funct_ptr m_hash_funct_ptr; 

   std::vector<unsigned int> m_version;
};

// versioning bloom filter -- track version of its member conservatively 
class versioning_bloomfilter
{
public:
   versioning_bloomfilter(unsigned int size, const std::vector<int>& funct_ids, unsigned int n_functs); 
   versioning_bloomfilter(const versioning_bloomfilter& other); // copy constructor 
   versioning_bloomfilter& operator=(const versioning_bloomfilter& other); // assignment operator 
   ~versioning_bloomfilter(); 

   // set version of a given address 
   void update_version(addr_t addr, unsigned int version); 

   // match against a single address
   unsigned int get_version(addr_t addr) const;

   // clear all entries in the hashtable 
   void clear();

   // print hashtable content 
   void print(FILE *fout) const; 

   // accessor helper functions
   unsigned int size() const { return m_size; }
   size_t n_hashes() const { return m_n_hashes; }

   static void unit_test(); 

private:
   unsigned int m_size; 
   size_t m_n_hashes; 

   typedef std::vector<versioning_hashtable*> hashtables_array; 
   hashtables_array m_hashtables; 
};
#endif
