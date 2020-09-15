#include "../abstract_hardware_model.h" 
#include "hashfunc.h" 

static std::map<unsigned int, hasht_funct::h3_hash *> g_h3_1;
static std::map<unsigned int, hasht_funct::h3_hash *> g_h3_2;
static std::map<unsigned int, hasht_funct::h3_hash *> g_h3_3;
static std::map<unsigned int, hasht_funct::h3_hash *> g_h3_4;
void init_h3_hash(unsigned int size)
{
   if (g_h3_1[size] != NULL) return;
   g_h3_1[size] = new hasht_funct::h3_hash(size, 0x10203040);
   g_h3_2[size] = new hasht_funct::h3_hash(size, 0x20304010);
   g_h3_3[size] = new hasht_funct::h3_hash(size, 0x30401020);
   g_h3_4[size] = new hasht_funct::h3_hash(size, 0x40102030);
}
unsigned int h3_hash1(unsigned int size, addr_t addr) { return g_h3_1[size]->hash(size, addr); }
unsigned int h3_hash2(unsigned int size, addr_t addr) { return g_h3_2[size]->hash(size, addr); }
unsigned int h3_hash3(unsigned int size, addr_t addr) { return g_h3_3[size]->hash(size, addr); }
unsigned int h3_hash4(unsigned int size, addr_t addr) { return g_h3_4[size]->hash(size, addr); }

