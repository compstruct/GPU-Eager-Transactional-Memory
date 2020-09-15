// Copyright (c) 2009-2011, Tor M. Aamodt, Inderpreet Singh
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

#include "scoreboard.h"
#include "shader.h"
#include "../cuda-sim/ptx_sim.h"
#include "shader_trace.h"
#include "../option_parser.h"

#include <set>
#include <utility> 

// command line options 
class scoreboard_options 
{
public:
   unsigned m_tm_token_cnt;  // number of TM tokens per core 
   unsigned m_g_tm_token_cnt;  // number of TM tokens across the GPU 

   void reg_options(option_parser_t opp) {
      option_parser_register(opp, "-scb_tm_token_cnt", OPT_INT32, &m_tm_token_cnt, 
                  "Number of TM tokens per core",
                  "1");
      option_parser_register(opp, "-scb_g_tm_token_cnt", OPT_INT32, &m_g_tm_token_cnt, 
                  "Number of TM tokens globally (default = 0 = infinite)",
                  "0");
   }
}; 

scoreboard_options g_scb_options; 

void scoreboard_reg_options(option_parser_t opp)
{
   g_scb_options.reg_options(opp);
}

// global scoreboard for TM concurrency control study 
class global_scoreboard
{
public:
   // acquire a tm token for a warp 
   void acquire_token(unsigned sid, unsigned wid); 
   // release a tm token for a warp 
   void release_token(unsigned sid, unsigned wid); 
   // test if a warp has acquired a token 
   bool has_token(unsigned sid, unsigned wid) const; 
   // does the global scoreboard has free token remain?
   bool has_free_token() const; 

   void print(FILE *fout) const; 
private:
   // set<sid, wid>
   typedef std::set<std::pair<unsigned, unsigned> > global_tm_token;
   global_tm_token m_tm_token; 
};

void global_scoreboard::acquire_token(unsigned sid, unsigned wid)
{
   if (g_scb_options.m_g_tm_token_cnt == 0) return; 
   assert(has_free_token() or has_token(sid, wid)); 
   m_tm_token.insert(std::make_pair(sid, wid)); 
   //printf("g_token_acq: %u,%u\n", sid, wid);
}

void global_scoreboard::release_token(unsigned sid, unsigned wid)
{
   if (g_scb_options.m_g_tm_token_cnt == 0) return; 
   assert(has_token(sid, wid)); 
   m_tm_token.erase(std::make_pair(sid, wid)); 
   //printf("g_token_rel: %u,%u\n", sid, wid);
}

bool global_scoreboard::has_token(unsigned sid, unsigned wid) const 
{
   if (g_scb_options.m_g_tm_token_cnt == 0) return true; 
   return (m_tm_token.count(std::make_pair(sid, wid))); 
}

bool global_scoreboard::has_free_token() const 
{
   return (g_scb_options.m_g_tm_token_cnt == 0 or m_tm_token.size() < g_scb_options.m_g_tm_token_cnt); 
}

void global_scoreboard::print(FILE *fout) const 
{
   fprintf(fout, "g_scoreboard = "); 
   for (global_tm_token::const_iterator itoken = m_tm_token.begin(); itoken != m_tm_token.end(); ++itoken) 
      fprintf(fout, "%u,%u", itoken->first, itoken->second); 
   fprintf(fout, "\n"); 
}

global_scoreboard g_scoreboard; 

//Constructor
Scoreboard::Scoreboard( unsigned sid, unsigned n_warps, simt_stack **simt, bool serialize_tx_warps )
: longopregs()
{
   m_sid = sid;
   //Initialize size of table
   reg_table.resize(n_warps);
   longopregs.resize(n_warps);
   reg_hazard_table.resize(n_warps);
   m_issued_inflight_insn.resize(n_warps, 0); 
   m_enable_tm_token = serialize_tx_warps; 
   m_tm_rollback = 0;
   m_tm_commit = false;
   m_in_tx_commit.resize(n_warps, false); 
   m_simt_stack = simt;
   m_n_tm_tokens = g_scb_options.m_tm_token_cnt; 
}

// Print scoreboard contents
void Scoreboard::printContents() const
{
	printf("scoreboard contents (sid=%d): \n", m_sid);
	for(unsigned i=0; i<reg_table.size(); i++) {
		if(reg_table[i].size() == 0 ) continue;
		printf("  wid = %2d: ", i);
		std::set<unsigned>::const_iterator it;
		for( it=reg_table[i].begin() ; it != reg_table[i].end(); it++ )
			printf("%u ", *it);
		printf("\n");
	}
    printf("  inflight_insn_count: "); 
    for (unsigned w = 0; w < m_issued_inflight_insn.size(); w++) {
        if (m_issued_inflight_insn[w] == 0) continue; 
        printf("w%2d(%d) ", w, m_issued_inflight_insn[w]); 
    }
    printf("\n"); 
    printf("  TM Token = ");
    for (std::set<unsigned>::iterator itoken = m_tm_token.begin(); itoken != m_tm_token.end(); ++itoken)
        printf("%d ", *itoken);
    printf(" Rollback = %d, Commit = %s\n", m_tm_rollback, (m_tm_commit)? "true":"false"); 
    printf("  In Tx Commit: "); 
    for (unsigned w = 0; w < m_in_tx_commit.size(); w++) 
        printf("%d", ((m_in_tx_commit[w])? 1 : 0)); 
    printf("\n"); 
}

void Scoreboard::reserveRegister(unsigned wid, unsigned regnum, data_hazard_t hazard_type)
{
	if( !(reg_table[wid].find(regnum) == reg_table[wid].end()) ){
		printf("Error: trying to reserve an already reserved register (sid=%d, wid=%d, regnum=%d).", m_sid, wid, regnum);
        abort();
	}
    SHADER_DPRINTF( SCOREBOARD,
                    "Reserved Register - warp:%d, reg: %d\n", wid, regnum );
	reg_table[wid].insert(regnum);
	reg_hazard_table[wid][regnum] = hazard_type;
}

// Unmark register as write-pending
void Scoreboard::releaseRegister(unsigned wid, unsigned regnum) 
{
	if( !(reg_table[wid].find(regnum) != reg_table[wid].end()) ) 
        return;
    SHADER_DPRINTF( SCOREBOARD,
                    "Release register - warp:%d, reg: %d\n", wid, regnum );
	reg_table[wid].erase(regnum);
	reg_hazard_table[wid].erase(regnum);
}

const bool Scoreboard::islongop (unsigned warp_id,unsigned regnum) {
	return longopregs[warp_id].find(regnum) != longopregs[warp_id].end();
}

data_hazard_t Scoreboard::getDataHazardType(unsigned wid, const inst_t *inst) const
{
   // Get list of all input and output registers
   std::set<int> inst_regs;

   if(inst->out[0] > 0) inst_regs.insert(inst->out[0]);
   if(inst->out[1] > 0) inst_regs.insert(inst->out[1]);
   if(inst->out[2] > 0) inst_regs.insert(inst->out[2]);
   if(inst->out[3] > 0) inst_regs.insert(inst->out[3]);
   if(inst->in[0] > 0) inst_regs.insert(inst->in[0]);
   if(inst->in[1] > 0) inst_regs.insert(inst->in[1]);
   if(inst->in[2] > 0) inst_regs.insert(inst->in[2]);
   if(inst->in[3] > 0) inst_regs.insert(inst->in[3]);
   if(inst->pred > 0) inst_regs.insert(inst->pred);
   if(inst->ar1 > 0) inst_regs.insert(inst->ar1);
   if(inst->ar2 > 0) inst_regs.insert(inst->ar2);

   // Check for collision, get the intersection of reserved registers and instruction registers
   std::set<int>::const_iterator it2;
   for ( it2=inst_regs.begin() ; it2 != inst_regs.end(); it2++ )
      if(reg_table[wid].find(*it2) != reg_table[wid].end()) {
         std::map<unsigned, enum data_hazard_t>::const_iterator hazard_it = reg_hazard_table[wid].find(*it2);
         assert(hazard_it != reg_hazard_table[wid].end());
         assert(hazard_it->second != NO_HAZARD);
         return hazard_it->second;
      }
   return NO_HAZARD;
}

void Scoreboard::dec_inflight_insn(unsigned warp_id) 
{
    m_issued_inflight_insn[warp_id] -= 1;
    assert(m_issued_inflight_insn[warp_id] >= 0); 
}

void Scoreboard::reserveRegisters(const class warp_inst_t* inst) 
{
    data_hazard_t hazard_type = (inst->op == LOAD_OP) ? MEM_HAZARD : ALU_HAZARD;
    for( unsigned r=0; r < 4; r++) {
	if (inst->is_dummy) assert(inst->out[r] == 0);
        if(inst->out[r] > 0) {
            reserveRegister(inst->warp_id(), inst->out[r], hazard_type);
            SHADER_DPRINTF( SCOREBOARD,
                            "Reserved register - warp:%d, reg: %d\n",
                            inst->warp_id(),
                            inst->out[r] );
        }
    }

    m_issued_inflight_insn[inst->warp_id()] += 1;
    reserveTMToken(inst); 

    //Keep track of long operations
    if (inst->is_load() &&
    		(	inst->space.get_type() == global_space ||
    			inst->space.get_type() == local_space ||
                inst->space.get_type() == param_space_kernel ||
                inst->space.get_type() == param_space_local ||
                inst->space.get_type() == param_space_unclassified ||
    			inst->space.get_type() == tex_space)){
    	for ( unsigned r=0; r<4; r++) {
    		if(inst->out[r] > 0) {
                SHADER_DPRINTF( SCOREBOARD,
                                "New longopreg marked - warp:%d, reg: %d\n",
                                inst->warp_id(),
                                inst->out[r] );
                longopregs[inst->warp_id()].insert(inst->out[r]);
            }
    	}
    }
}

// Release registers for an instruction
void Scoreboard::releaseRegisters(const class warp_inst_t *inst) 
{
    for( unsigned r=0; r < 4; r++) {
        if(inst->out[r] > 0) {
            SHADER_DPRINTF( SCOREBOARD,
                            "Register Released - warp:%d, reg: %d\n",
                            inst->warp_id(),
                            inst->out[r] );
            releaseRegister(inst->warp_id(), inst->out[r]);
            longopregs[inst->warp_id()].erase(inst->out[r]);
        }
    }
}

/** 
 * Checks to see if registers used by an instruction are reserved in the scoreboard
 *  
 * @return 
 * true if WAW or RAW hazard (no WAR since in-order issue)
 **/ 
bool Scoreboard::checkCollision( unsigned wid, const class inst_t *inst ) const
{
    bool dependencyHazard = (getDataHazardType(wid,inst) != NO_HAZARD);

   // in logical timestamp based tm manager, prevent execution while cleaning number of writing threads, 
   if (m_simt_stack[wid]->is_tm_restarted()) {
       dependencyHazard = true;
   }

   if (isMaskedTMToken(wid) == true)
      dependencyHazard = true;

   // prevent execution of subsequent instructions before commit of a warp is done 
   if (inTxCommit(wid) == true)  {
      dependencyHazard = true; 
   }

   // prevent execution of __tcommit() before all issued instructions from this warp are done 
   if (inst->is_tcommit) {
      if (m_issued_inflight_insn[wid] > 0 or (not reg_table[wid].empty())) {
         dependencyHazard = true; 
      }
   }

    // Check for TM token hazard
    bool tmHazard = checkTMToken(wid, inst); 

    return (dependencyHazard || tmHazard); 
}

bool Scoreboard::pendingWrites(unsigned wid) const
{
	return !reg_table[wid].empty();
}

void Scoreboard::reserveTMToken(const warp_inst_t *inst) 
{
    if (m_enable_tm_token == false) return; 
    if (inst->is_tbegin == true) {
        unsigned warp_id = inst->warp_id();
        assert(m_tm_token.size() < m_n_tm_tokens || m_tm_token.count(warp_id) != 0);
        m_tm_token.insert(warp_id);
        if (m_tm_masked_token.size() > 0) {
           m_tm_masked_token.push_back(warp_id);
        } 	
        g_scoreboard.acquire_token(m_sid, warp_id); 
    }
}

void Scoreboard::releaseTMToken(unsigned warp_id) 
{
    if (m_enable_tm_token == false) return; 
    assert(m_tm_token.count(warp_id)); 
    m_tm_token.erase(warp_id); 
    g_scoreboard.release_token(m_sid, warp_id); 
    if (m_tm_masked_token.size() > 0 and m_tm_token.size() == m_tm_masked_token.size()) {
        m_tm_masked_token.pop_front();
    }
}

void Scoreboard::maskTMToken(unsigned warp_id)
{
    if (m_enable_tm_token == false) {
	assert(m_tm_masked_token.empty());
	return; 
    }
    assert(m_tm_token.count(warp_id));
    m_tm_masked_token.push_back(warp_id);
}

bool Scoreboard::couldMaskTMToken(unsigned warp_id) 
{
    if (m_enable_tm_token == false) {
	assert(m_tm_masked_token.empty());
	return false;
    }
    assert(m_tm_token.count(warp_id));
    assert(m_tm_token.size() > m_tm_masked_token.size());
    if (m_tm_token.size() - m_tm_masked_token.size() == 1) {
        return false;
    } else {
        return true;
    }
}

bool Scoreboard::isMaskedTMToken(unsigned warp_id) const 
{
    if (m_enable_tm_token == false) {
	assert(m_tm_masked_token.empty());
	return false;
    }
    for (auto iter = m_tm_masked_token.begin(); iter != m_tm_masked_token.end(); iter++) {
        if (*iter == warp_id)
	    return true;
    }
    return false;
}

void Scoreboard::recoverMaskedTMToken() 
{
    if (m_enable_tm_token == false) {
	assert(m_tm_masked_token.empty());
	return;
    }
    if (m_tm_masked_token.size() == 0) return;
    m_tm_masked_token.pop_front();
}

bool Scoreboard::checkTMToken(unsigned wid, const inst_t *inst) const 
{
    // do not check for any TM conflict if TM token is not enabled 
    if (m_enable_tm_token == false) return false; 

    // for tm token owner, prevent issue during rollback or commit
    if (m_tm_token.count(wid) && m_tm_rollback > 0) {
        return true; 
    } else if (m_tm_token.count(wid) && m_tm_commit == true) {
        return true; 
    }

    // for non-owner, just check for tm_token 
    if (inst->is_tbegin == true) {
    	// no a conflict if tm token is free or this warp is a tm token owner
    	bool core_tm_token_ok = (m_tm_token.size() < m_n_tm_tokens || m_tm_token.count(wid));
      // same for global tm token
      bool global_tm_token_ok = (g_scoreboard.has_free_token() || g_scoreboard.has_token(m_sid, wid)); 
      if (core_tm_token_ok and global_tm_token_ok)
    		return false;
    	else
    		return true;
    } else if (inst->is_tcommit == true) {
        // ensure that a commit is issued after all of the warp's inflight instructions are done 
        if (m_issued_inflight_insn[wid] > 0) {
            return true;
        } else {
            return false; 
        }
    } else {
        return false; // no conflict 
    }
}

void Scoreboard::reserveTMRollback(unsigned warp_id, unsigned releasers) 
{ 
    if (m_enable_tm_token == false) return;

    assert(m_tm_token.count(warp_id)); 
    m_tm_rollback = releasers; 
}

void Scoreboard::releaseTMRollback() 
{ 
    if (m_enable_tm_token == false) return;

    assert(m_tm_rollback > 0); 
    m_tm_rollback -= 1; 
} 

void Scoreboard::startTMCommit(unsigned warp_id) 
{ 
    if (m_enable_tm_token == false) return;

    assert(m_tm_token.count(warp_id)); 
    m_tm_commit = true; 
} 

void Scoreboard::doneTMCommit() 
{ 
    if (m_enable_tm_token == false) return;

    m_tm_commit = false; 
} 

void Scoreboard::startTxCommit(unsigned warp_id)
{
   m_in_tx_commit[warp_id] = true; 
}

void Scoreboard::doneTxCommit(unsigned warp_id)
{
   m_in_tx_commit[warp_id] = false; 
}

bool Scoreboard::inTxCommit(unsigned warp_id) const
{ 
   return m_in_tx_commit[warp_id];
}

bool Scoreboard::inTxRestart(unsigned warp_id) const
{   
    if (m_in_tx_commit[warp_id])
	m_simt_stack[warp_id]->clear_tm_restarted();
    return m_simt_stack[warp_id]->is_tm_restarted();
}

void Scoreboard::doneTxRestart(unsigned warp_id) 
{
    m_simt_stack[warp_id]->clear_tm_restarted();
}

bool Scoreboard::someInCommit() const 
{
    if (m_tm_token.empty()) return false;	
    for (auto iter = m_tm_token.begin(); iter != m_tm_token.end(); iter++) {
        unsigned wid = *iter;
	if (m_in_tx_commit[wid])
	    return true;
    }
    return false;
}

void Scoreboard::set_num_tm_tokens(unsigned num_tokens)
{
    if (num_tokens == 0) {
        m_n_tm_tokens = 1;
    } else if (num_tokens > g_scb_options.m_tm_token_cnt) {
        m_n_tm_tokens = g_scb_options.m_tm_token_cnt;
    } else {
        m_n_tm_tokens = num_tokens;
    }
}

/** 
 * Checks to see if registers used by an instruction are reserved in the scoreboard
 * As long as there is a in-flight instruction, the collision detection is triggered. 
 * This prevents a warp to simultaneously run multiple instruction (no execution beyond a load miss). 
 *  
 * @return 
 * true if warp is running another instruction 
 **/ 
bool SimpleScoreboard::checkCollision( unsigned wid, const class inst_t *inst ) const
{
    bool dependencyHazard = false; 

	// If the warp has a register registered, then it is running another instruction 
    // Because a load always writes to a register, this will make sure the warp waits for the load
    if (reg_table[wid].empty() == false) {
        dependencyHazard = true; 
    }

    // Check for TM token hazard
    bool tmHazard = checkTMToken(wid, inst); 

    return (dependencyHazard || tmHazard); 
}


