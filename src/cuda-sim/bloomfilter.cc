#include "bloomfilter.h"
#include "hashfunc.h"
#include <map>
#include <set>

enum {
   HASHTABLE_BITS = 1,
   HASHTABLE_COUNTER = 2
}; 


enum {
   TOY_SET = 0,
   GSKEW_SET = 1, 
   H3_SET = 2
};
int g_hash_function_set = TOY_SET; 

#define UNITTEST 1 
#ifndef BLOOMFILTER_UNITTEST
#ifndef HASHTABLE_MT_UNITTEST
#ifndef VERSIONING_BF_UNITTEST
#undef UNITTEST
#define UNITTEST 0
#endif
#endif
#endif
void bloomfilter_reg_options(option_parser_t opp)
{
    #if not UNITTEST
    option_parser_register(opp, "-tm_bloomfilter_hashset", OPT_INT32, &g_hash_function_set, 
                "the hash function set used in the bloomfilter for tm conflict detection (default = 2)",
                "2");
    hashtable_bits_mt::reg_options(opp); 
    bloomfilter_mt::reg_options(opp);
    #endif
}


///////////////////////////////////////////////////////////////////////////////
// generic hashtable interface

const int hashtable::s_nullpos = 0xDEADBEEF; 
hashtable::hashtable(unsigned int size, int funct_id)
   : m_size(size), m_funct_id(funct_id)
{
   if (g_hash_function_set == TOY_SET) {
      // set the hash function 
      switch(funct_id) {
      case  0:  m_hash_funct_ptr = &hasht_funct::simplemod;        break; 
      case  1:  m_hash_funct_ptr = &hasht_funct::upperloweradd<8>; break; 
      case  2:  m_hash_funct_ptr = &hasht_funct::upperlowerxor<8>; break; 
      case  3:  m_hash_funct_ptr = &hasht_funct::upperloweradd<4>; break; 
      default: abort(); 
      }; 
   } else if (g_hash_function_set == GSKEW_SET) {
      if (g_gskew == NULL) g_gskew = new hasht_funct::gskew_hash(size); 
      // set the hash function 
      switch(funct_id) {
      case  0:  m_hash_funct_ptr = &gskew_hash1; break; 
      case  1:  m_hash_funct_ptr = &gskew_hash2; break; 
      case  2:  m_hash_funct_ptr = &gskew_hash3; break; 
      case  3:  m_hash_funct_ptr = &gskew_hash4; break; 
      default: abort(); 
      }; 
   } else if (g_hash_function_set == H3_SET) {
      init_h3_hash(size); 
      // set the hash function 
      switch(funct_id) {
      case  0:  m_hash_funct_ptr = &h3_hash1; break; 
      case  1:  m_hash_funct_ptr = &h3_hash2; break; 
      case  2:  m_hash_funct_ptr = &h3_hash3; break; 
      case  3:  m_hash_funct_ptr = &h3_hash4; break; 
      default: abort(); 
      }; 
   }
}

///////////////////////////////////////////////////////////////////////////////
// hashtable with bit-based signature 

template<int sig_limit>
hashtable_bits<sig_limit>::hashtable_bits(unsigned int size, int funct_id)
   : hashtable(size, funct_id), m_obj_type(HASHTABLE_BITS)
{
   assert(m_size <= sig_limit); 
}

// for read/write set creation
template<int sig_limit>
int hashtable_bits<sig_limit>::add(addr_t addr)
{
   unsigned int hashpos = hash(addr); 
   assert(hashpos < m_size); 
   bool sigbit_modifed = not m_signature.test(hashpos); 
   m_signature.set(hashpos, true); 

   int modified_pos = hashtable::s_nullpos; 
   if (sigbit_modifed == true) {
      modified_pos = hashpos; 
   }

   return modified_pos; 
}

// for partial update of global conflict table (in this case the hashed position is already known)
template<int sig_limit>
void hashtable_bits<sig_limit>::set_bitpos(unsigned int bitpos) 
{
   assert(bitpos < m_size); 
   m_signature.set(bitpos, true); 
}

// member removal 
// only possible for counter-based hashtable 
template<int sig_limit> void hashtable_bits<sig_limit>::remove(addr_t addr) { assert(0); }  
template<int sig_limit> void hashtable_bits<sig_limit>::remove(const hashtable& signature) { assert(0); }  

// match against a single address
template<int sig_limit>
bool hashtable_bits<sig_limit>::match(addr_t addr) const
{
   unsigned int hashpos = hash(addr); 
   assert(hashpos < m_size); 
   return m_signature.test(hashpos); 
}

// match against another hashtable's signature 
template<int sig_limit>
bool hashtable_bits<sig_limit>::match(const hashtable& signature) const
{
   const hashtable_bits<sig_limit>& other_table = (const hashtable_bits<sig_limit>&) signature; 
   sig_t intersact_sig = m_signature & other_table.m_signature; 

   return intersact_sig.any(); 
}

// clear all entries in the hashtable 
template<int sig_limit>
void hashtable_bits<sig_limit>::clear()
{
   m_signature.reset(); 
}

// print hashtable content
template<int sig_limit>
void hashtable_bits<sig_limit>::print(FILE *fout) const
{
   std::string str_sig = m_signature.to_string(); 
   str_sig.erase(0, sig_limit - m_size); 
   fprintf(fout, "%s\n", str_sig.c_str()); 
}

template class hashtable_bits<128>;
template class hashtable_bits<256>;
template class hashtable_bits<512>;
template class hashtable_bits<1024>;


///////////////////////////////////////////////////////////////////////////////
// hashtable with counter-based signature (support set member removal) 

hashtable_counter::hashtable_counter(unsigned int size, int funct_id)
   : hashtable_bits<htctr_sig_limit>(size, funct_id), m_countertable(m_size, 0)  
{
   m_obj_type = HASHTABLE_COUNTER; 
}

// for read/write set creation
int hashtable_counter::add(addr_t addr)
{
   unsigned hashpos = hash(addr); 
   assert(hashpos < m_size); 

   int origvalue = m_countertable[hashpos]; 
   assert(m_signature.test(hashpos) == (origvalue != 0)); // ensure consistency between counter and signature 
   m_countertable[hashpos] += 1; 
   m_signature.set(hashpos, true); 

   int modified_pos = hashtable::s_nullpos; 
   if (origvalue == 0) {
      modified_pos = hashpos; 
   }

   return modified_pos; 
}

// for partial update of global conflict table 
void hashtable_counter::set_bitpos(unsigned int bitpos)
{
   assert(bitpos < m_size); 
   m_countertable[bitpos] += 1; 
   m_signature.set(bitpos, true); 
}

// decrement the counter at a position in the hashtable (and set signature accordingly) 
void hashtable_counter::dec_counter(unsigned pos)
{
   m_countertable[pos] -= 1; 
   m_signature.set(pos, (m_countertable[pos] > 0)); // set signature based on counter value 
   assert(m_countertable[pos] >= 0); // guard against overflow 
}

// member removal 
// only possible for counter-based hashtable 
void hashtable_counter::remove(addr_t addr)
{
   unsigned hashpos = hash(addr); 
   assert(hashpos < m_size); 

   dec_counter(hashpos);  
}

// go through the other hashtable's signature, remove exactly one copy 
void hashtable_counter::remove(const hashtable& signature)
{
   const hashtable_bits<htctr_sig_limit>& other_table = (const hashtable_bits<htctr_sig_limit>&) signature; 
   assert(other_table.size() == m_size); 
   
   for (unsigned n = 0; n < m_size; n++) 
   {
      if (other_table.test_signature(n) == true) {
         dec_counter(n); 
      }
   }
}

// match against a single address
bool hashtable_counter::match(addr_t addr) const
{
   return hashtable_bits<htctr_sig_limit>::match(addr); // leverage bitvector version 
}

// match against a signature from another hashtable 
bool hashtable_counter::match(const hashtable& signature) const
{
   return hashtable_bits<htctr_sig_limit>::match(signature); // leverage bitvector version 
}

// clear all entries in the hashtable 
void hashtable_counter::clear()
{
   m_countertable.assign(m_size, 0); 
   hashtable_bits<htctr_sig_limit>::clear(); 
}

void hashtable_counter::print(FILE *fout) const 
{
   hashtable_bits<htctr_sig_limit>::print(fout); 
}

// print counters in all the buckets
void hashtable_counter::print_counters(FILE *fout) const 
{
   for (size_t n = 0; n < m_countertable.size(); n++) {
      fprintf(fout, "[%02llx] = %d\n", (unsigned long long)n, m_countertable[n]); 
   }
}



void passed(const char *test_name, bool condition) 
{
   if (condition == true) { 
      printf("%s = PASSED\n", test_name); 
   } else {
      printf("%s = FAILED\n", test_name); 
   }
}

// #define HASHTABLE_UNITTEST
#ifdef HASHTABLE_UNITTEST

bool distribution_test(unsigned int size, int hashset, int funct_id)
{
   g_hash_function_set = hashset; 
   hashtable_counter htcounter(size, funct_id); 

   srand(1); 
   for (int n = 0; n < 20000000; n++) {
      htcounter.add(rand()); 
   }

   htcounter.print_counters(stdout); 
   return true; 
}

bool conflict_test(unsigned int size, int hashset, int funct_id)
{
   g_hash_function_set = hashset; 
   hashtable_counter htcounter(size, funct_id); 

   srand(1); 
   unsigned int cap = 18;
   unsigned int hit = 0;
   unsigned int trials = 20000; 
   for (int n = 0; n < trials; n++) {
      htcounter.clear(); 
      for (int m = 0; m < cap; m++) {
         htcounter.add(rand()); 
      }
      addr_t x = rand(); 
      hit += (htcounter.match(x))? 1 : 0; 
   }

   printf("Hit rate = %f\n", (float)hit/trials); 
   return true; 
}

bool empty_test(unsigned int size, int funct_id)
{
   hashtable_counter htcounter(size, funct_id); 
   addr_t testvec[5] = {5, 34, 1235, 5564, 21}; 

   bool hit = false; 
   for (int n = 0; n < 5; n++) {
      hit = hit || htcounter.match(testvec[n]); 
   }

   passed(__FUNCTION__, (hit == false)); 
   return (hit == false); 
}

bool insert_match_clear_test(unsigned int size, int funct_id)
{
   hashtable_counter htcounter(size, funct_id); 
   addr_t testvec[5] = {5, 34, 1235, 5564, 21}; 

   // insert value first 
   for (int n = 0; n < 5; n++) {
      htcounter.add(testvec[n]); 
   }

   // these should all hit 
   bool hit = true; 
   for (int n = 0; n < 5; n++) {
      hit = hit && htcounter.match(testvec[n]); 
   }

   bool pass_cond = (hit == true); 

   // these should all miss 
   addr_t testvec_miss[5] = {26, 77, 54, 0xDEADBEEF, 0x7E37900D}; 
   for (int n = 0; n < 5; n++) {
      if ( htcounter.match(testvec_miss[n]) == true ) {
         pass_cond = false; 
      }
   }

   htcounter.clear(); 

   // these should all miss after clear 
   for (int n = 0; n < 5; n++) {
      if ( htcounter.match(testvec[n]) == true ) {
         pass_cond = false; 
      }
   }

   passed(__FUNCTION__, pass_cond); 
   return pass_cond; 
}

bool alias_detection_test(unsigned int size, int funct_id)
{
   hashtable_counter htcounter(size, funct_id); 
   addr_t testvec[5] = {0x55, 0x155, 0x156, 0x8855, 0x9356}; 
   bool expected_outcome[5] = {true, false, true, false, false}; 

   // insert value and check the alias response  
   bool pass_cond = true; 
   for (int n = 0; n < 5; n++) {
      int sig_modified = htcounter.add(testvec[n]); 
      if ((sig_modified != hashtable::s_nullpos) != expected_outcome[n]) {
         printf("%#x: %d, %d\n", testvec[n], sig_modified, expected_outcome[n]); 
         pass_cond = false; 
      }
   } 

   passed(__FUNCTION__, pass_cond); 
   return pass_cond; 
}

bool remove_test(unsigned int size, int funct_id)
{
   hashtable_counter htcounter(size, funct_id); 
   addr_t testvec[8] = {0x55, 0x15F, 0x156, 0x8853, 0xDEADBEEF, 0x9356, 0x7E37900D, 0x883292}; 

   // insert value 
   for (int n = 0; n < 8; n++) {
      htcounter.add(testvec[n]); 
   } 

   // these should all hit 
   bool hit = true; 
   for (int n = 0; n < 8; n++) {
      hit = hit && htcounter.match(testvec[n]); 
   }

   bool pass_cond = (hit == true); 

   for (int n = 0; n < 8; n++) {
      if (n % 2 == 0) htcounter.remove(testvec[n]); 
   }

   bool expected_outcome[8] = {false, true, true/*alias*/, true, false, true, false, true}; 
   for (int n = 0; n < 8; n++) {
      bool hit = htcounter.match(testvec[n]); 
      if (hit != expected_outcome[n]) {
         printf("%#x: %d, %d\n", testvec[n], hit, expected_outcome[n]); 
         pass_cond = false; 
      }
   } 

   passed(__FUNCTION__, pass_cond); 
   return pass_cond; 
}

bool signature_op_test(unsigned int size, int funct_id)
{
   hashtable_counter htcounter(size, funct_id); 
   addr_t testvec[8] = {0x55, 0x15F, 0x156, 0x8855, 0xDEADBEEF, 0x9356, 0x7E37900D, 0x883292}; 

   // insert value and check the alias response  
   for (int n = 0; n < 8; n++) {
      htcounter.add(testvec[n]); 
   } 

   // these should all hit 
   bool hit = true; 
   for (int n = 0; n < 8; n++) {
      hit = hit && htcounter.match(testvec[n]); 
   }
   passed("signature_op_test::init", (hit == true)); 

   // create completely disjoint set 1, signature should not hit 
   hashtable_bits htsig1(size, funct_id); 
   addr_t sig1member[4] = {0x909013, 0xCA7, 0xD06, 0x8888}; 
   for (int n = 0; n < 4; n++) htsig1.add(sig1member[n]); 
   bool sig1hit = htcounter.match(htsig1); 
   passed("signature_op_test::disjoint", (sig1hit == false)); 

   // create set 2 containing a subset of testvec, signature should hit 
   hashtable_bits htsig2(size, funct_id); 
   addr_t sig2member[4] = {0xDEADBEEF, 0x9356, 0x7E37900D, 0x883292}; 
   for (int n = 0; n < 4; n++) htsig2.add(sig2member[n]); 
   bool sig2hit = htcounter.match(htsig2); 
   passed("signature_op_test::subset", (sig2hit == true)); 

   // create set 3 containing a one member from testvec, signature should hit 
   hashtable_bits htsig3(size, funct_id); 
   addr_t sig3member[4] = {0x7E37900D, 0x909013, 0xCA7, 0xD06}; 
   for (int n = 0; n < 4; n++) htsig3.add(sig3member[n]); 
   bool sig3hit = htcounter.match(htsig3); 
   passed("signature_op_test::partial", (sig3hit == true)); 

   // remove everything in htsig2 from htcounter, it should still hit due to aliasing (0x9356 and 0x156)
   htcounter.remove(htsig2); 
   sig2hit = htcounter.match(htsig2); 
   passed("signature_op_test::removal1", (sig2hit == true)); 
   // htsig3 no longer hit 
   sig3hit = htcounter.match(htsig3); 
   passed("signature_op_test::removal2", (sig3hit == false)); 
}

int main()
{
   unsigned int size = 256; 
   int funct_id = 0; 

   printf("size = %u, funct_id = %d\n", size, funct_id); 
   printf("Testing with simplemode hash... \n"); 
   empty_test(size, funct_id); 
   insert_match_clear_test(size, funct_id); 
   alias_detection_test(size, funct_id); 
   remove_test(size, funct_id); 

   signature_op_test(size, funct_id); 

   // testing different hash functions 
   printf("Testing different hash functions... \n"); 
   insert_match_clear_test(size, 1); 
   insert_match_clear_test(size, 2); 

   alias_detection_test(size, 1); // expect fail
   alias_detection_test(size, 2); // expect fail

   // distribution_test(size, 0, 2); 
   // conflict_test(size, 0, 3); 

   return 0; 
}

#endif 


///////////////////////////////////////////////////////////////////////////////
// an implementation of bloomfilter based on an array of hashtables 

bloomfilter::bloomfilter(unsigned int size, const std::vector<int>& funct_ids, unsigned int n_functs, bool counter_based)
   : m_size(size), m_n_hashes(n_functs), m_counter_based(counter_based)
{
   assert(m_n_hashes <= funct_ids.size()); 
   m_hashtables.assign(m_n_hashes, NULL); 

   for (int n = 0; n < m_n_hashes; n++) {
      if (m_counter_based) {
         m_hashtables[n] = new hashtable_counter(m_size, funct_ids[n]); 
      } else {
         if (m_size <= 128) {
            m_hashtables[n] = new hashtable_bits<128>(m_size, funct_ids[n]); 
         } else if (m_size <= 256) {
            m_hashtables[n] = new hashtable_bits<256>(m_size, funct_ids[n]); 
         } else if (m_size <= 512) {
            m_hashtables[n] = new hashtable_bits<512>(m_size, funct_ids[n]); 
         } else if (m_size <= 1024) {
            m_hashtables[n] = new hashtable_bits<1024>(m_size, funct_ids[n]); 
         } else if (m_size <= 2048) {
            m_hashtables[n] = new hashtable_bits<2048>(m_size, funct_ids[n]); 
         } else if (m_size <= 4096) {
            m_hashtables[n] = new hashtable_bits<4096>(m_size, funct_ids[n]); 
         } else {
            assert(0); 
         }
      }
   }
}

bloomfilter::bloomfilter(const bloomfilter& other)
   : m_size(other.m_size), m_n_hashes(other.m_n_hashes), m_counter_based(other.m_counter_based)
{
   m_hashtables.assign(m_n_hashes, NULL); 

   for (int n = 0; n < m_n_hashes; n++) {
      if (m_counter_based) {
         m_hashtables[n] = new hashtable_counter(*((hashtable_counter*)other.m_hashtables[n])); 
      } else {
         if (m_size <= 128) {
            m_hashtables[n] = new hashtable_bits<128>(*((hashtable_bits<128>*)other.m_hashtables[n])); 
         } else if (m_size <= 256) {
            m_hashtables[n] = new hashtable_bits<256>(*((hashtable_bits<256>*)other.m_hashtables[n])); 
         } else if (m_size <= 512) {
            m_hashtables[n] = new hashtable_bits<512>(*((hashtable_bits<512>*)other.m_hashtables[n])); 
         } else if (m_size <= 1024) {
            m_hashtables[n] = new hashtable_bits<1024>(*((hashtable_bits<1024>*)other.m_hashtables[n])); 
         } else if (m_size <= 2048) {
            m_hashtables[n] = new hashtable_bits<2048>(*((hashtable_bits<2048>*)other.m_hashtables[n])); 
         } else if (m_size <= 4096) {
            m_hashtables[n] = new hashtable_bits<4096>(*((hashtable_bits<4096>*)other.m_hashtables[n])); 
         } else {
            assert(0); 
         }
      }
   }
}

bloomfilter& bloomfilter::operator=(const bloomfilter& other)
{
   if (this == &other) return *this; 

   // deallocate all internal hashtables 
   for (int n = 0; n < m_hashtables.size(); n++) { 
      delete m_hashtables[n]; 
   }

   m_size = other.m_size; 
   m_n_hashes = other.m_n_hashes; 
   m_counter_based = other.m_counter_based; 

   for (int n = 0; n < m_n_hashes; n++) {
      if (m_counter_based) {
         m_hashtables[n] = new hashtable_counter(*((hashtable_counter*)other.m_hashtables[n])); 
      } else {
         if (m_size <= 128) {
            m_hashtables[n] = new hashtable_bits<128>(*((hashtable_bits<128>*)other.m_hashtables[n])); 
         } else if (m_size <= 256) {
            m_hashtables[n] = new hashtable_bits<256>(*((hashtable_bits<256>*)other.m_hashtables[n])); 
         } else if (m_size <= 512) {
            m_hashtables[n] = new hashtable_bits<512>(*((hashtable_bits<512>*)other.m_hashtables[n])); 
         } else if (m_size <= 1024) {
            m_hashtables[n] = new hashtable_bits<1024>(*((hashtable_bits<1024>*)other.m_hashtables[n])); 
         } else if (m_size <= 2048) {
            m_hashtables[n] = new hashtable_bits<2048>(*((hashtable_bits<2048>*)other.m_hashtables[n])); 
         } else if (m_size <= 4096) {
            m_hashtables[n] = new hashtable_bits<4096>(*((hashtable_bits<4096>*)other.m_hashtables[n])); 
         } else {
            assert(0); 
         }
      }
   }

   return *this; 
}

bloomfilter::~bloomfilter()
{
   // deallocate all internal hashtables 
   for (int n = 0; n < m_hashtables.size(); n++) {
      delete m_hashtables[n]; 
   }
}

// add an address into the bloomfilter, return true if any of the hash is setting a new bit 
// position of the new bits set returned via mod_pos
bool bloomfilter::add(addr_t addr, std::vector<int>& mod_pos)
{
   bool sig_modified = false; 
   for (int h = 0; h < m_n_hashes; h++) {
      int modified_position = m_hashtables[h]->add(addr); 
      mod_pos.at(h) = modified_position; 
      if (modified_position != hashtable::s_nullpos) 
         sig_modified = true; 
   }

   return sig_modified; 
}

// simple version add() that does not return modified positions in the hashes
bool bloomfilter::add(addr_t addr)
{
   bool sig_modified = false; 
   for (int h = 0; h < m_n_hashes; h++) {
      int modified_position = m_hashtables[h]->add(addr); 
      if (modified_position != hashtable::s_nullpos) 
         sig_modified = true; 
   }

   return sig_modified; 
}

// for partial update of global conflict table 
void bloomfilter::set_bitpos(const std::vector<int>& bitpos)
{
   for (int h = 0; h < m_n_hashes; h++) {
      int updated_position = bitpos.at(h); 
      if (updated_position != hashtable::s_nullpos) 
         m_hashtables[h]->set_bitpos(updated_position); 
   }
}

// only possible for counter-based bloomfilter 
void bloomfilter::remove(addr_t addr)
{
   assert(m_counter_based == true); 
   for (int h = 0; h < m_n_hashes; h++) {
      m_hashtables[h]->remove(addr); 
   }
}

void bloomfilter::remove(const bloomfilter& signature)
{
   assert(m_counter_based == true); 
   assert(m_n_hashes == signature.m_n_hashes); 

   for (int h = 0; h < m_n_hashes; h++) {
      hashtable& other_hashsig = *(signature.m_hashtables[h]); 
      assert(m_hashtables[h]->funct_id() == other_hashsig.funct_id()); // only possible with same hash function 
      m_hashtables[h]->remove(other_hashsig); 
   }
}

// match against a single address
bool bloomfilter::match(addr_t addr) const 
{
   // only match if all hashes match 
   bool hit = true; 
   for (int h = 0; h < m_n_hashes; h++) {
      hit = hit && m_hashtables[h]->match(addr); 
   }

   return hit; 
}

// match against the signature from another bloomfilter 
bool bloomfilter::match(const bloomfilter& signature) const 
{
   assert(m_n_hashes == signature.m_n_hashes); 

   // only match if all hashes match 
   bool hit = true; 
   for (int h = 0; h < m_n_hashes; h++) {
      hashtable& other_hashsig = *(signature.m_hashtables[h]); 
      assert(m_hashtables[h]->funct_id() == other_hashsig.funct_id()); // only possible with same hash function 
      hit = hit && m_hashtables[h]->match(other_hashsig); 
   }

   return hit; 
}

// clear all entries in the filter 
void bloomfilter::clear()
{
   for (int h = 0; h < m_n_hashes; h++) {
      m_hashtables[h]->clear(); 
   }
}

void bloomfilter::print(FILE *fout) const
{
   for (int h = 0; h < m_n_hashes; h++) {
      m_hashtables[h]->print(fout); 
   }
   fprintf(fout, "\n"); 
}


// #define BLOOMFILTER_UNITTEST
#ifdef BLOOMFILTER_UNITTEST

bool insert_match_clear_test(unsigned int size, const std::vector<int>& funct_ids, bool counter_based)
{
   bloomfilter testfilter(size, funct_ids, 4, false); 
   addr_t testvec[8] = {0x55, 0x15F, 0x156, 0x8855, 0xDEADBEEF, 0x9356, 0x7E37900D, 0x883292}; 

   // insert value and check the alias response  
   for (int n = 0; n < 8; n++) {
      testfilter.add(testvec[n]); 
   } 

   // these should all hit 
   bool hit = true; 
   for (int n = 0; n < 5; n++) {
      hit = hit && testfilter.match(testvec[n]); 
   }

   bool pass_cond = (hit == true); 

   // these should all miss 
   addr_t testvec_miss[5] = {5, 34, 1235, 5564, 21}; 
   for (int n = 0; n < 5; n++) {
      if ( testfilter.match(testvec_miss[n]) == true ) {
         pass_cond = false; 
      }
   }

   testfilter.clear(); 

   // these should all miss after clear 
   for (int n = 0; n < 8; n++) {
      if ( testfilter.match(testvec[n]) == true ) {
         pass_cond = false; 
      }
   }

   passed(__FUNCTION__, pass_cond); 

   return pass_cond; 
}

bool alias_detection_test(unsigned int size, const std::vector<int>& funct_ids, bool counter_based)
{
   bloomfilter testfilter(size, funct_ids, 4, counter_based); 
   addr_t testvec[5] = {0x55, 0x155, 0x156, 0x8855, 0x9356}; 
   bool expected_outcome[5] = {true, false, true, false, false}; 

   // insert value and check the alias response  
   bool pass_cond = true; 
   std::vector<int> mod_pos(funct_ids.size()); 
   for (int n = 0; n < 5; n++) {
      bool sig_modified = testfilter.add(testvec[n], mod_pos); 
      for (size_t h = 0; h < funct_ids.size(); h++) {
         // only the simplemod hash should exhibit aliasing 
         bool sig_modified = (mod_pos[h] != hashtable::s_nullpos); 
         if (h == 0) {
            if (sig_modified != expected_outcome[n]) {
               printf("%#x: hash%d, %d, %d\n", testvec[n], h, sig_modified, expected_outcome[n]); 
               pass_cond = false; 
            }
         } else if (sig_modified == false) {
            printf("%#x: hash%d, %d, %d\n", testvec[n], h, sig_modified, expected_outcome[n]); 
            pass_cond = false; 
         }
      }
   } 

   passed(__FUNCTION__, pass_cond); 
   return pass_cond; 
}

bool remove_test(unsigned int size, const std::vector<int>& funct_ids)
{
   bloomfilter testfilter(size, funct_ids, 4, true); 
   addr_t testvec[8] = {0x55, 0x15F, 0x156, 0x8853, 0xDEADBEEF, 0x9356, 0x7E37900D, 0x883292}; 

   // insert value 
   for (int n = 0; n < 8; n++) {
      testfilter.add(testvec[n]); 
   } 

   // these should all hit 
   bool hit = true; 
   for (int n = 0; n < 8; n++) {
      hit = hit && testfilter.match(testvec[n]); 
   }
   bool pass_cond = (hit == true); 

   for (int n = 0; n < 8; n++) {
      if (n % 2 == 0) testfilter.remove(testvec[n]); 
   }

   bool expected_outcome[8] = {false, true, false, true, false, true, false, true}; 
   for (int n = 0; n < 8; n++) {
      bool hit = testfilter.match(testvec[n]); 
      if (hit != expected_outcome[n]) {
         printf("%#x: %d, %d\n", testvec[n], hit, expected_outcome[n]); 
         pass_cond = false; 
      }
   } 

   passed(__FUNCTION__, pass_cond); 
   return pass_cond; 
}

bool signature_op_test(unsigned int size, const std::vector<int>& funct_ids)
{
   bloomfilter testfilter(size, funct_ids, 4, true); 
   addr_t testvec[8] = {0x55, 0x15F, 0x156, 0x8855, 0xDEADBEEF, 0x9356, 0x7E37900D, 0x883292}; 

   // insert value and check the alias response  
   for (int n = 0; n < 8; n++) {
      testfilter.add(testvec[n]); 
   } 

   // these should all hit 
   bool hit = true; 
   for (int n = 0; n < 8; n++) {
      hit = hit && testfilter.match(testvec[n]); 
   }
   passed("signature_op_test::init", (hit == true)); 

   // create completely disjoint set 1, signature should not hit 
   bloomfilter htsig1(size, funct_ids, 4, false); 
   addr_t sig1member[4] = {0x909013, 0xCA7, 0xD06, 0x8888}; 
   for (int n = 0; n < 4; n++) htsig1.add(sig1member[n]); 
   bool sig1hit = testfilter.match(htsig1); 
   passed("signature_op_test::disjoint", (sig1hit == false)); 

   // create set 2 containing a subset of testvec, signature should hit 
   bloomfilter htsig2(size, funct_ids, 4, false); 
   addr_t sig2member[4] = {0xDEADBEEF, 0x9356, 0x7E37900D, 0x883292}; 
   for (int n = 0; n < 4; n++) htsig2.add(sig2member[n]); 
   bool sig2hit = testfilter.match(htsig2); 
   passed("signature_op_test::subset", (sig2hit == true)); 

   // create set 3 containing a one member from testvec, signature should hit 
   bloomfilter htsig3(size, funct_ids, 4, false); 
   addr_t sig3member[4] = {0x7E37900D, 0x909013, 0xCA7, 0xD06}; 
   for (int n = 0; n < 4; n++) htsig3.add(sig3member[n]); 
   bool sig3hit = testfilter.match(htsig3); 
   passed("signature_op_test::partial", (sig3hit == true)); 

   // remove everything in htsig2 from testfilter, it should not hit 
   testfilter.remove(htsig2); 
   sig2hit = testfilter.match(htsig2); 
   passed("signature_op_test::removal1", (sig2hit == false)); 
   // htsig3 no longer hit 
   sig3hit = testfilter.match(htsig3); 
   passed("signature_op_test::removal2", (sig3hit == false)); 
}

#include <set>
bool conflict_test(unsigned int size, int hashset, const std::vector<int>& funct_ids, int n_hashes, int testset_size)
{
   g_hash_function_set = hashset; 
   bloomfilter access_sig(size, funct_ids, n_hashes, true); 
   bloomfilter write_sig(size, funct_ids, n_hashes, true); 
   bloomfilter write_hit_sig(size, funct_ids, n_hashes, true); 

   srand(1); 
   unsigned int cap = testset_size * 1;
   unsigned int n_hit = 0;
   unsigned int n_reverse_hit = 0; 
   unsigned int trials = 2000; 
   std::set<addr_t> true_access_set; 
   std::set<addr_t> true_write_set; 
   for (int n = 0; n < trials; n++) {
      access_sig.clear(); 
      write_sig.clear(); 
      true_access_set.clear(); 
      true_write_set.clear(); 
      int m = 0; 
      while (m < cap) {
         addr_t y = rand(); 
         if (true_access_set.find(y) != true_access_set.end()) 
            continue; 
         true_access_set.insert(y); 
         access_sig.add(y); 
         m++;
      }

      bool hit = false; 
      write_hit_sig.clear(); 
      for (int k = 0; k < testset_size; k++) {
         addr_t x = rand(); 
         while (true_access_set.find(x) != true_access_set.end()) x = rand(); 
         true_write_set.insert(x); 
         write_sig.add(x); 
         if (access_sig.match(x)) {
            hit = true; 
            write_hit_sig.add(x); 
         }
      } 

      if (hit) {
         // match address from true access set via write signature 
         bool reverse_sig_hit = false; 
         bool reverse_hit = false; 
         for (std::set<addr_t>::iterator i_acc = true_access_set.begin(); 
              i_acc != true_access_set.end(); ++i_acc)
         {
            if (true_write_set.find(*i_acc) != true_write_set.end()) {
               reverse_hit = true;  
            }
            if (write_hit_sig.match(*i_acc)) {
               reverse_sig_hit = true;  
            }
         }

         // reverse_hit is exact, so if it is true, signature hit must be true 
         if (reverse_hit) assert(reverse_sig_hit == true); 
         if (reverse_sig_hit == true) {
            n_reverse_hit += 1; 
         }
      }
      n_hit += (hit)? 1 : 0; 
   }

   printf("n_hashes = %d; testset_size = %d; Hit rate = %f; Rev.Hit rate = %f\n", 
          n_hashes, testset_size, (float)n_hit / trials, (float)n_reverse_hit / trials); 
   return true; 
}

bool scaling_test(unsigned int size, int hashset, const std::vector<int>& funct_ids, int n_hashes, 
                  int n_threads, int tx_size)
{
   g_hash_function_set = hashset; 

   std::vector<std::set<addr_t> > threadset(n_threads); 
   std::vector<bloomfilter> thread_bf(n_threads, bloomfilter(size, funct_ids, n_hashes, false)); 

   srand(1); 
   for (size_t t = 0; t < threadset.size(); t++) {
      for (int r = 0; r < tx_size; r++) {
         addr_t a = rand(); 
         threadset[t].insert(a); 
         thread_bf[t].add(a); 
      }
   }

   unsigned long long detected_conflict = 0; 
   unsigned long long false_conflict = 0; 
   unsigned n_thread_detected_conflict = 0; 
   unsigned n_thread_false_conflict = 0; 
   for (size_t t = 0; t < threadset.size(); t++) {
      std::set<addr_t> & committer = threadset[t]; 
      bool overall_bf_hit = false; 
      bool overall_real_hit = false; 
      for (size_t s = t; s < threadset.size(); s++) {
         if (t == s) continue; 
         bool bf_hit = false; 
         bool real_hit = false; 
         for (std::set<addr_t>::iterator iAddr = committer.begin(); iAddr != committer.end(); ++iAddr) {
            bf_hit = bf_hit or thread_bf[s].match(*iAddr); 
            real_hit = real_hit or (threadset[s].count(*iAddr) != 0); 
         }
         if (bf_hit) detected_conflict += 1; 
         if (bf_hit and not real_hit) false_conflict += 1; 
         overall_bf_hit = overall_bf_hit or bf_hit;
         overall_real_hit = overall_real_hit or real_hit; 
      }
      if (overall_bf_hit) n_thread_detected_conflict += 1; 
      if (overall_bf_hit and not overall_real_hit) n_thread_false_conflict += 1; 
   }

   printf("[Scaling test] sig_size = %u; n_thread = %d; detected conflict = %llu; false conflict = %llu; false positive rate = %f; thd detected conflict = %u; thd false conflict = %u; thd false conflict rate = %f;\n",
          size * n_hashes, n_threads, detected_conflict, false_conflict, (float) false_conflict / detected_conflict,
          n_thread_detected_conflict, n_thread_false_conflict, (float) n_thread_false_conflict / n_thread_detected_conflict);
}

int main()
{
   unsigned int size = 256; 
   std::vector<int> funct_ids(4); 
   funct_ids[0] = 0; 
   funct_ids[1] = 1; 
   funct_ids[2] = 2; 
   funct_ids[3] = 3; 

   printf("bloomfilter unit test...\n"); 
   insert_match_clear_test(size, funct_ids, false); 
   insert_match_clear_test(size, funct_ids, true); 

   alias_detection_test(size, funct_ids, false); 
   alias_detection_test(size, funct_ids, true); 

   remove_test(size, funct_ids); 
   signature_op_test(size, funct_ids); 

   int set_id = 2; 
   int n_hashes = 4; 
   conflict_test(size / n_hashes, set_id, funct_ids, n_hashes, 1);
   conflict_test(size / n_hashes, set_id, funct_ids, n_hashes, 2);
   conflict_test(size / n_hashes, set_id, funct_ids, n_hashes, 4);
   conflict_test(size / n_hashes, set_id, funct_ids, n_hashes, 8);
   conflict_test(size / n_hashes, set_id, funct_ids, n_hashes,16);
   conflict_test(size / n_hashes, set_id, funct_ids, n_hashes,32);

   scaling_test(64, set_id, funct_ids, n_hashes, 128, 4); 
   scaling_test(64, set_id, funct_ids, n_hashes, 256, 4); 
   scaling_test(64, set_id, funct_ids, n_hashes, 512, 4); 
   scaling_test(64, set_id, funct_ids, n_hashes, 1024, 4); 
   scaling_test(64, set_id, funct_ids, n_hashes, 2048, 4); 
   scaling_test(64, set_id, funct_ids, n_hashes, 4096, 4); 

   scaling_test(64, set_id, funct_ids, n_hashes, 128, 16); 
   scaling_test(64, set_id, funct_ids, n_hashes, 256, 16); 
   scaling_test(64, set_id, funct_ids, n_hashes, 512, 16); 
   scaling_test(64, set_id, funct_ids, n_hashes, 1024, 16); 
   scaling_test(64, set_id, funct_ids, n_hashes, 2048, 16); 
   scaling_test(64, set_id, funct_ids, n_hashes, 4096, 16); 

   scaling_test(256, set_id, funct_ids, n_hashes, 128, 16); 
   scaling_test(256, set_id, funct_ids, n_hashes, 256, 16); 
   scaling_test(256, set_id, funct_ids, n_hashes, 512, 16); 
   scaling_test(256, set_id, funct_ids, n_hashes, 1024, 16); 
   scaling_test(256, set_id, funct_ids, n_hashes, 2048, 16); 
   scaling_test(256, set_id, funct_ids, n_hashes, 4096, 16); 
   return 0; 
}

#endif


///////////////////////////////////////////////////////////////////////////////
// hashtable for an array of threads  

int hashtable_bits_mt::s_n_tid_subhash = 1; 

hashtable_bits_mt::hashtable_bits_mt(unsigned int size, int funct_id, unsigned int nthreads, unsigned int tid_hashsize)
   : hashtable(size, funct_id), m_nthreads(nthreads), m_tid_hashsize(tid_hashsize),
     m_selected_thread(s_null_tid), m_signature(size), m_hashed_sig(size),
     m_tid_subhashsize(s_n_tid_subhash, 0), m_hash_gen_mask(m_tid_hashsize)
{ 
   assert(m_nthreads <= s_nthread_limit); 
   assert(m_tid_hashsize <= s_tid_hash_limit); 

   // determine the number of bits each subhash has 
   if (s_n_tid_subhash == 1) {
      m_tid_subhashsize[0] = m_tid_hashsize; 
   } else {
      assert(s_n_tid_subhash == 2);
      m_tid_subhashsize[0] = m_tid_hashsize / 2; 
      m_tid_subhashsize[1] = m_tid_hashsize / 2; 
   }

   // make sure they add up to the specified size 
   unsigned int sumbits = 0;
   for (int s = 0; s < s_n_tid_subhash; s++) {
      sumbits += m_tid_subhashsize[s];
   }
   assert(sumbits == m_tid_hashsize); 

   // initialize the mask used for each bit of each sub hash 
   unsigned subhash_offset = 0; 
   for (int s = 0; s < s_n_tid_subhash; s++) {
      hasht_funct::h3_hash h3(m_tid_subhashsize[s], 0x10101 * s); 
      for (unsigned t = 0; t < m_nthreads; t++) {
         // unsigned hash_pos = t % m_tid_subhashsize[s]; // will be replaced as h3 
         unsigned hash_pos = h3.hash(m_tid_subhashsize[s], t); 
         m_hash_gen_mask[subhash_offset + hash_pos].set(t); 
      }
      subhash_offset += m_tid_subhashsize[s]; 
   }
}

void hashtable_bits_mt::reg_options(option_parser_t opp)
{
    #if not UNITTEST
    option_parser_register(opp, "-tm_n_tid_subhash", OPT_INT32, &s_n_tid_subhash, 
                "the number of subhashes used in the tid bloomfilter (default = 1)",
                "1");
    #endif
}

int hashtable_bits_mt::add(addr_t addr)
{
   assert(m_selected_thread != s_null_tid); 
   unsigned hashpos = hash(addr); 
   bool wasSet = m_signature[hashpos].test(m_selected_thread); 
   
   m_signature[hashpos].set(m_selected_thread); 

   int modified_pos = hashtable::s_nullpos; 
   if (wasSet == false) {  // 0 -> 1
      // regenerate hash 
      update_tvec_hash(hashpos);
      modified_pos = hashpos; 
   }

   return modified_pos; 
}

void hashtable_bits_mt::set_bitpos(unsigned int bitpos) { assert(0); } 

// only possible for counter-based hashtable 
void hashtable_bits_mt::remove(addr_t addr) { assert(0); } 
void hashtable_bits_mt::remove(const hashtable& signature) { assert(0); }

// match against a single address
bool hashtable_bits_mt::match(addr_t addr) const
{
   unsigned hashpos = hash(addr); 
   if (m_selected_thread != s_null_tid) {
      return (m_signature[hashpos].test(m_selected_thread)); 
   } else {
      return (m_signature[hashpos].any()); 
   }
}

bool hashtable_bits_mt::match(const hashtable& signature) const
{
   for (unsigned p = 0; p < m_size; p++) {
      bool hit; 
      if (m_selected_thread != s_null_tid) {
         hit = m_signature[p].test(m_selected_thread); 
      } else {
         hit = m_signature[p].any(); 
      }
      if (hit) return hit; 
   }
   return false; 
}

// clear all entries in the hashtable 
void hashtable_bits_mt::clear()
{
   for (unsigned p = 0; p < m_size; p++) {
      bool wasSet = m_signature[p].test(m_selected_thread); 
      m_signature[p].reset(m_selected_thread); 
      if (wasSet) {  // 1 -> 0
         // recalculate hash for this entry
         update_tvec_hash(p); 
      }
   }
}

void hashtable_bits_mt::print(FILE *fout) const
{
   for (unsigned s = 0; s < m_size; s++) {
      std::string str_htvec = m_hashed_sig[s].to_string(); 
      fprintf(fout, "[%3d]:%s\n", s, str_htvec.c_str()); 
   }
}

// clear signatures for all threads
void hashtable_bits_mt::clear_all()
{
   for (unsigned p = 0; p < m_size; p++) {
      m_signature[p].reset(); 
      m_hashed_sig[p].reset(); 
   }
}

void hashtable_bits_mt::update_tvec_hash(unsigned int bitpos)
{
   // each subhash owns a subset of bits in the thread-signature 
   unsigned subhash_offset = 0; 
   m_hashed_sig[bitpos].reset(); 
   for (int s = 0; s < s_n_tid_subhash; s++) {
      for (unsigned b = 0; b < m_tid_subhashsize[s]; b++) {
         const tvec_t& hmask = m_hash_gen_mask[subhash_offset + b]; 
         tvec_t maskedtid = m_signature[bitpos] & hmask; 
         if (maskedtid.any()) 
            m_hashed_sig[bitpos].set(subhash_offset + b); 
      }
      subhash_offset += m_tid_subhashsize[s]; 
   }
}

bool hashtable_bits_mt::hashed_threadsig_populated(const hashed_tvec_t& combined_sig) const
{
   bool populated = true; 
   unsigned subhash_offset = 0; 
   for (int s = 0; s < s_n_tid_subhash; s++) {
      hashed_tvec_t subhash_mask;
      for (unsigned b = 0; b < m_tid_subhashsize[s]; b++) {
         subhash_mask.set(subhash_offset + b); 
      }
      hashed_tvec_t subhash = combined_sig & subhash_mask; 
      populated = populated and subhash.any(); 
      subhash_offset += m_tid_subhashsize[s]; 
   }
   return populated; 
}

hashtable_bits_mt::tvec_t hashtable_bits_mt::expand_hashed_sigbit(const hashed_tvec_t& hashed_sig) const
{
   tvec_t full_tvec; 
   unsigned subhash_offset = 0;
   tvec_t expanded_tvec; 
   for (int s = 0; s < s_n_tid_subhash; s++) {
      expanded_tvec.reset(); 
      for (unsigned b = 0; b < m_tid_subhashsize[s]; b++) {
         if (hashed_sig.test(subhash_offset + b) == true) {
            expanded_tvec |= m_hash_gen_mask[subhash_offset + b]; 
         }
      }
      if (s == 0) 
         full_tvec = expanded_tvec; 
      else 
         full_tvec &= expanded_tvec; 
      subhash_offset += m_tid_subhashsize[s]; 
   }

   return full_tvec; 
}


// #define HASHTABLE_MT_UNITTEST
#ifdef HASHTABLE_MT_UNITTEST

bool insert_match_clear_test(unsigned int size, int funct_id)
{
   int nthreads = 4;
   hashtable_bits_mt testfilter(size, funct_id, nthreads, 2); 
   addr_t testvec[8] = {0x55, 0x15F, 0x156, 0x8855, 0xDEADBEEF, 0x9356, 0x7E37900D, 0x883292}; 

   // insert value and check the alias response  
   for (int n = 0; n < 8; n++) {
      testfilter.select_thread(n % nthreads); 
      testfilter.add(testvec[n]); 
   } 
   testfilter.unselect_thread(); 

   // these should all hit 
   bool hit = true; 
   for (int n = 0; n < 8; n++) {
      testfilter.select_thread(n % nthreads); 
      hit = hit && testfilter.match(testvec[n]); 
   }
   testfilter.unselect_thread(); 

   bool pass_cond = (hit == true); 

   // these should all miss (matching a thread's set against another thread)
   for (int n = 0; n < 8; n++) {
      testfilter.select_thread((n + 2) % nthreads); 
      if (testfilter.match(testvec[n]) == true) {
         pass_cond = false; 
      }
   }
   testfilter.unselect_thread(); 

   // these should all miss 
   addr_t testvec_miss[5] = {5, 34, 1235, 5564, 21}; 
   for (int n = 0; n < 5; n++) {
      if ( testfilter.match(testvec_miss[n]) == true ) {
         pass_cond = false; 
      }
   }

   testfilter.clear_all(); 

   // these should all miss after clear 
   for (int n = 0; n < 8; n++) {
      testfilter.select_thread(n % nthreads); 
      if ( testfilter.match(testvec[n]) == true ) {
         pass_cond = false; 
      }
   }

   passed(__FUNCTION__, pass_cond); 

   return pass_cond; 
}

int main()
{
   unsigned int size = 256; 

   printf("hashtable_mt unit test...\n"); 
   insert_match_clear_test(size, 0); 
   return 0; 
}

#endif


///////////////////////////////////////////////////////////////////////////////
// bloomfilter for an array of threads 

int bloomfilter_mt::s_tid_hashvar = 0; 
void bloomfilter_mt::reg_options(option_parser_t opp)
{
    #if not UNITTEST
    option_parser_register(opp, "-tm_n_tid_hashvar", OPT_INT32, &s_tid_hashvar, 
                "varying the thread hash used in the memory-side bloomfilter (default = 0)",
                "0");
    #endif
}

bloomfilter_mt::bloomfilter_mt(unsigned int size, const std::vector<int>& funct_ids, unsigned int n_functs, unsigned int nthreads, unsigned int tid_hashsize)
   : m_size(size), m_n_hashes(n_functs), m_nthreads(nthreads), m_tid_hashsize(tid_hashsize)
{
   assert(m_n_hashes <= funct_ids.size()); 
   m_hashtables.assign(m_n_hashes, NULL); 

   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n] = new hashtable_bits_mt(m_size, funct_ids[n], nthreads, tid_hashsize - s_tid_hashvar * n ); 
   }
}

bloomfilter_mt::bloomfilter_mt(const bloomfilter_mt& other)
   : m_size(other.m_size), m_n_hashes(other.m_n_hashes), 
     m_nthreads(other.m_nthreads), m_tid_hashsize(other.m_tid_hashsize)
{
   m_hashtables.assign(m_n_hashes, NULL); 

   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n] = new hashtable_bits_mt(*other.m_hashtables[n]); 
   }
}

bloomfilter_mt& bloomfilter_mt::operator=(const bloomfilter_mt& other)
{
   if (this == &other) return *this; 

   // deallocate all internal hashtables 
   for (int n = 0; n < m_hashtables.size(); n++) { 
      delete m_hashtables[n]; 
   }

   m_size = other.m_size; 
   m_n_hashes = other.m_n_hashes; 
   m_nthreads = other.m_nthreads; 
   m_tid_hashsize = other.m_tid_hashsize; 

   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n] = new hashtable_bits_mt(*other.m_hashtables[n]); 
   }

   return *this; 
}

bloomfilter_mt::~bloomfilter_mt()
{
   // deallocate all internal hashtables 
   for (int n = 0; n < m_hashtables.size(); n++) { 
      delete m_hashtables[n]; 
   }
}

void bloomfilter_mt::select_thread(int thread_id)
{
   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n]->select_thread(thread_id);
   }
}

void bloomfilter_mt::unselect_thread()
{
   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n]->unselect_thread();
   }
}


bool bloomfilter_mt::add(addr_t addr)
{
   bool modified = false; 
   for (int n = 0; n < m_n_hashes; n++) {
      int mod_position = m_hashtables[n]->add(addr);
      if (mod_position != hashtable::s_nullpos) 
         modified = true; 
   }
   return modified; 
}


// match against a single address
bool bloomfilter_mt::match(addr_t addr) const
{
   // take the signature bits from each hashtable, and AND them together 
   hashtable_bits_mt::tvec_t match_mask(m_hashtables[0]->get_sigbit(addr)); 
   for (int n = 1; n < m_n_hashes; n++) {
      match_mask &= m_hashtables[n]->get_sigbit(addr); 
   }

   return match_mask.any(); 
}

// match against a single address (using hashed thread vectors)
bool bloomfilter_mt::match_hashed(addr_t addr, hashtable_bits_mt::tvec_t& matched_threads) const
{
   assert(s_tid_hashvar == 0); 
   // take the signature bits from each hashtable, and AND them together 
   hashtable_bits_mt::hashed_tvec_t match_mask(m_hashtables[0]->get_hashed_sigbit(addr)); 
   for (int n = 1; n < m_n_hashes; n++) {
      match_mask &= m_hashtables[n]->get_hashed_sigbit(addr); 
   }

   bool hash_match = m_hashtables[0]->hashed_threadsig_populated(match_mask); 
   bool exact_match = this->match(addr); 

   if (hash_match and not exact_match) {
      // printf("False Conflict Traffic\n"); 
   }

   if (hash_match) {
      hashtable_bits_mt::tvec_t match_mask_expanded = m_hashtables[0]->expand_hashed_sigbit(match_mask); 
      matched_threads |= match_mask_expanded; 
   }

   return hash_match;
}

// match against a single address (using expanded hashed thread vectors)
bool bloomfilter_mt::match_hashexpanded(addr_t addr)
{
   // take the signature bits from each hashtable, and AND them together 
   hashtable_bits_mt::tvec_t match_mask(m_hashtables[0]->get_expanded_hashed_sigbit(addr)); 
   for (int n = 1; n < m_n_hashes; n++) {
      match_mask &= m_hashtables[n]->get_expanded_hashed_sigbit(addr); 
   }

   bool hash_match = match_mask.any(); 
   bool exact_match = this->match(addr); 

   if (hash_match and not exact_match) {
      // printf("False Conflict Traffic\n"); 
   }

   return hash_match;
}

// match against the signature from another bloomfilter 
bool bloomfilter_mt::match(const bloomfilter& signature)
{
   assert(0); 
   return false;
}

// clear all entries in the filter for selected thread 
void bloomfilter_mt::clear()
{
   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n]->clear();
   }
}

// clear all entries in the filter for all thread 
void bloomfilter_mt::clear_all()
{
   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n]->clear_all();
   }
}

///////////////////////////////////////////////////////////////////////////////
// versioning hash table 
// hash table that track the version of its member conservatively 
// (return version that is younger or equal to actual version)

versioning_hashtable::versioning_hashtable(unsigned int size, int funct_id)
   : m_size(size), m_funct_id(funct_id), m_version(m_size, 0)
{
   // always use H3 hash 
   init_h3_hash(size); 
   // set the hash function 
   switch(funct_id) {
   case  0:  m_hash_funct_ptr = &h3_hash1; break; 
   case  1:  m_hash_funct_ptr = &h3_hash2; break; 
   case  2:  m_hash_funct_ptr = &h3_hash3; break; 
   case  3:  m_hash_funct_ptr = &h3_hash4; break; 
   default: abort(); 
   }; 
}

void versioning_hashtable::update_version(addr_t addr, unsigned int version)
{
   unsigned int hpos = hash(addr); 
   m_version[hpos] = std::max(m_version[hpos], version); 
}

unsigned int versioning_hashtable::get_version(addr_t addr) const 
{
   unsigned int hpos = hash(addr); 
   return m_version[hpos];
}

void versioning_hashtable::clear()
{
   m_version.assign(m_size, 0); 
}

void versioning_hashtable::print(FILE *fout) const 
{
   for (unsigned x = 0; x < m_size; x++) {
      fprintf(fout, "[%4d] = %u\n", x, m_version[x]);
   }
}

void versioning_hashtable::unit_test()
{
   versioning_hashtable vht(1024, 1);
   
   std::vector<std::pair<addr_t, unsigned int> > av_pair(64); 

   for (size_t x = 0; x < av_pair.size(); x++) {
      av_pair[x].first = rand() % 20000; 
      av_pair[x].second = rand() % 20000; 
      vht.update_version(av_pair[x].first, av_pair[x].second); 
      // fprintf(stdout, "x= %d: v[%x] = %u\n", x, av_pair[x].first, av_pair[x].second); 
   }

   for (size_t x = 0; x < av_pair.size(); x++) {
      unsigned version = vht.get_version(av_pair[x].first); 
      if (version != av_pair[x].second) {
         fprintf(stderr, "x= %zd: %u != %u\n", x, version, av_pair[x].second); 
         assert (version >= av_pair[x].second);
      }
   }

   printf("versioning_hashtable PASS\n"); 
}


// versioning bloom filter -- track version of its member conservatively 
versioning_bloomfilter::versioning_bloomfilter(unsigned int size, const std::vector<int>& funct_ids, unsigned int n_functs)
   : m_size(size), m_n_hashes(n_functs) 
{
   assert(m_n_hashes <= funct_ids.size()); 
   m_hashtables.assign(m_n_hashes, NULL); 

   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n] = new versioning_hashtable(m_size, funct_ids[n]); 
   }
}

versioning_bloomfilter::versioning_bloomfilter(const versioning_bloomfilter& other)
   : m_size(other.m_size), m_n_hashes(other.m_n_hashes) 
{
   m_hashtables.assign(m_n_hashes, NULL); 

   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n] = new versioning_hashtable(*(other.m_hashtables[n])); 
   }
}

versioning_bloomfilter& versioning_bloomfilter::operator=(const versioning_bloomfilter& other)
{
   if (this == &other) return *this; 

   for (int n = 0; n < m_n_hashes; n++) {
      delete m_hashtables[n]; 
   }

   m_size = other.m_size; 
   m_n_hashes = other.m_n_hashes;
   m_hashtables.assign(m_n_hashes, NULL); 

   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n] = new versioning_hashtable(*(other.m_hashtables[n])); 
   }

   return *this; 
}

versioning_bloomfilter::~versioning_bloomfilter()
{
   for (int n = 0; n < m_n_hashes; n++) {
      delete m_hashtables[n]; 
   }
}

// set version of a given address 
void versioning_bloomfilter::update_version(addr_t addr, unsigned int version)
{
   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n]->update_version(addr, version); 
   }
}

// match against a single address
unsigned int versioning_bloomfilter::get_version(addr_t addr) const
{
   // take minimum of all versions obtained: 
   // - if the recorded version is not aliased by another address, it is equal to the version of the given address 
   // - if it is, the recorded version is equal or larger than version of the given address 
   unsigned int version = m_hashtables[0]->get_version(addr); 
   for (int n = 1; n < m_n_hashes; n++) {
      unsigned int new_version = m_hashtables[n]->get_version(addr);
      version = std::min(version, new_version); 
   }

   return version; 
}

// clear all entries in the hashtable 
void versioning_bloomfilter::clear()
{
   for (int n = 0; n < m_n_hashes; n++) {
      m_hashtables[n]->clear(); 
   }
}

// print hashtable content 
void versioning_bloomfilter::print(FILE *fout) const
{
   for (int n = 0; n < m_n_hashes; n++) {
      fprintf(fout, "Hash #%d\n", n); 
      m_hashtables[n]->print(fout); 
   }
}

void versioning_bloomfilter::unit_test()
{
   std::vector<int> fid(4);
   fid[0] = 0; 
   fid[1] = 1; 
   fid[2] = 2; 
   fid[3] = 3; 
   versioning_bloomfilter vbf(1024, fid, 1);
   
   std::vector<std::pair<addr_t, unsigned int> > av_pair(64); 

   for (size_t x = 0; x < av_pair.size(); x++) {
      av_pair[x].first = rand() % 20000; 
      av_pair[x].second = rand() % 20000; 
      vbf.update_version(av_pair[x].first, av_pair[x].second); 
      // fprintf(stdout, "x= %d: v[%x] = %u\n", x, av_pair[x].first, av_pair[x].second); 
   }

   for (size_t x = 0; x < av_pair.size(); x++) {
      unsigned version = vbf.get_version(av_pair[x].first); 
      if (version != av_pair[x].second) {
         fprintf(stderr, "x= %zd: %u != %u\n", x, version, av_pair[x].second); 
         assert (version >= av_pair[x].second);
      }
   }

   printf("versioning_bloomfilter PASS\n"); 
}


#ifdef VERSIONING_BF_UNITTEST

int main() 
{
   versioning_hashtable::unit_test(); 
   versioning_bloomfilter::unit_test(); 
}

#endif
