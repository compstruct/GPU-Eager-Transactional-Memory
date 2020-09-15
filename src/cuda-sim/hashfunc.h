#ifndef HASHFUNC_H
#define HASHFUNC_H

// name space containing all the hash function 
namespace hasht_funct {

// toy hash functions 
inline unsigned int simplemod(unsigned int size, addr_t addr)
{
   return (addr % size); 
}

template<int upperlowerbound> unsigned int upperloweradd(unsigned int size, addr_t addr)
{
   addr_t upper = addr >> upperlowerbound; 
   return ( (addr + upper) % size); 
}

template<int upperlowerbound> unsigned int upperlowerxor(unsigned int size, addr_t addr)
{
   addr_t upper = addr >> upperlowerbound; 
   return ( (addr ^ upper) % size); 
}

// gskew hash functions (from skewed associative cache)
class gskew_hash
{
private:
   unsigned int m_size; 
   unsigned int m_size_log2; 

public:
   gskew_hash(unsigned int size)
      : m_size(size), m_size_log2(0)
   {
      assert(m_size > 0);
      assert(((m_size - 1) & m_size) == 0); 
      while (((unsigned)1 << m_size_log2) < m_size) m_size_log2++;
   }

   unsigned int subhash1(addr_t addr)
   {
      std::bitset<32> addr_b(addr & (m_size - 1)); 
      unsigned msb = m_size_log2 - 1; 
      bool msb_new = addr_b.test(msb) xor addr_b.test(0); 
      addr_b >>= 1; 
      addr_b.set(msb, msb_new); 
      return addr_b.to_ulong(); 
   }

   unsigned int subhash2(addr_t addr)
   {
      std::bitset<32> addr_b(addr & (m_size - 1)); 
      unsigned msb = m_size_log2 - 1; 
      bool lsb_new = addr_b.test(msb) xor addr_b.test(msb - 1); 
      addr_b <<= 1; 
      addr_b.set(0, lsb_new); 
      return (addr_b.to_ulong() & (m_size - 1)); 
   }

   template<int a, int b, int c>
   unsigned int hash(unsigned int size, addr_t addr)
   {
      assert(m_size == size); 
      addr_t subaddr[2]; 
      addr_t addr_short = addr;
      addr_short = addr ^ (addr >> 16); 
      subaddr[0] = addr_short & (m_size - 1); 
      subaddr[1] = (addr_short >> m_size_log2) & (m_size - 1); 
      addr_t output = subhash1(subaddr[a]) ^ subhash2(subaddr[b]) ^ subaddr[c]; 
      return output; 
   }
}; 


// h3 hash functions (used in later variants of LogTM-SE)
class h3_hash
{
private:
   unsigned int m_size; 
   unsigned int m_size_log2; 

   // one mask for each bit in the output 
   unsigned int m_seed; 
   std::vector<addr_t> m_qstring; 

public:
   h3_hash(unsigned int size, unsigned int seed)
      : m_size(size), m_size_log2(0), m_seed(seed), m_qstring(0)
   {
      assert(m_size > 0);
      assert(((m_size - 1) & m_size) == 0); 
      while (((unsigned)1 << m_size_log2) < m_size) m_size_log2++;

      m_qstring.resize(m_size_log2); 
      srand(seed); 
      for (unsigned n = 0; n < m_size_log2; n++) {
         m_qstring[n] = rand(); 
      }
   }

   unsigned int hash(unsigned int size, addr_t addr)
   {
      assert(m_size == size); 
      std::bitset<32> output; 
      output.reset(); 
      for (unsigned n = 0; n < m_size_log2; n++) {
         std::bitset<32> qnx = addr & m_qstring[n]; 
         bool bit = (qnx.count() % 2); // xor odd #1s = 1, even #1s = 0 
         output.set(n, bit); 
      }
      return output.to_ulong(); 
   }
}; 

}; 

static hasht_funct::gskew_hash *g_gskew = NULL;
inline unsigned int gskew_hash1(unsigned int size, addr_t addr)
{
   assert(g_gskew != NULL);
   return g_gskew->hash<0,1,1>(size, addr);
}
inline unsigned int gskew_hash2(unsigned int size, addr_t addr)
{
   assert(g_gskew != NULL);
   return g_gskew->hash<0,1,0>(size, addr);
}
inline unsigned int gskew_hash3(unsigned int size, addr_t addr)
{
   assert(g_gskew != NULL);
   return g_gskew->hash<1,0,1>(size, addr);
}
inline unsigned int gskew_hash4(unsigned int size, addr_t addr)
{
   assert(g_gskew != NULL);
   return g_gskew->hash<1,0,0>(size, addr);
}

void init_h3_hash(unsigned int size); 
unsigned int h3_hash1(unsigned int size, addr_t addr);
unsigned int h3_hash2(unsigned int size, addr_t addr);
unsigned int h3_hash3(unsigned int size, addr_t addr);
unsigned int h3_hash4(unsigned int size, addr_t addr);


#endif
