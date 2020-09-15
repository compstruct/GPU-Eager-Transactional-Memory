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

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <set>
#include "assert.h"

#ifndef SCOREBOARD_H_
#define SCOREBOARD_H_

#include "../abstract_hardware_model.h"
#include "../option_parser.h"

void scoreboard_reg_options(option_parser_t opp);

enum data_hazard_t {
   NO_HAZARD = 0,
   ALU_HAZARD,
   MEM_HAZARD
};

class Scoreboard {
public:
    Scoreboard( unsigned sid, unsigned n_warps, simt_stack **simt, bool serialize_tx_warps = false );
    virtual ~Scoreboard() { }

    void reserveRegisters(const warp_inst_t *inst);
    void releaseRegisters(const warp_inst_t *inst);
    void releaseRegister(unsigned wid, unsigned regnum);

    void dec_inflight_insn(unsigned warp_id); 

    void enableTMToken() { m_enable_tm_token = true; }
    void disableTMToken() { m_enable_tm_token = false; }

    void releaseTMToken(unsigned warp_id); 

    void reserveTMRollback(unsigned warp_id, unsigned releasers);
    void releaseTMRollback();

    void maskTMToken(unsigned warp_id);
    bool couldMaskTMToken(unsigned warp_id);
    void recoverMaskedTMToken();
    bool isMaskedTMToken(unsigned warp_id) const;
    unsigned numMaskedTMToken() const { return m_tm_masked_token.size(); }

    void startTMCommit(unsigned warp_id);
    void doneTMCommit();

    void startTxCommit(unsigned warp_id); 
    void doneTxCommit(unsigned warp_id);
    bool inTxCommit(unsigned warp_id) const;

    void doneTxRestart(unsigned warp_id);
    bool inTxRestart(unsigned warp_id) const;

    virtual bool checkCollision(unsigned wid, const inst_t *inst) const;
    bool pendingWrites(unsigned wid) const;
    void printContents() const;
    const bool islongop(unsigned warp_id, unsigned regnum);

    bool checkTMToken(unsigned wid, const inst_t *inst) const;

    bool someInCommit() const;

    void set_num_tm_tokens(unsigned num_tokens);

    data_hazard_t getDataHazardType(unsigned wid, const inst_t *inst) const;
protected:
    void reserveRegister(unsigned wid, unsigned regnum, data_hazard_t hazard_type);
    int get_sid() const { return m_sid; }

    unsigned m_sid;

    // keeps track of pending writes to registers
    // indexed by warp id, reg_id => pending write count
    std::vector< std::set<unsigned> > reg_table;

    // keeps track of type of hazard that a reserved register can cause
    std::vector< std::map<unsigned,enum data_hazard_t> > reg_hazard_table;

    // keep track of the number of issued in-flight instructions
    // for load instructions, this is decremented when the accessq is emptied and not when pending accesses returns
    std::vector<int> m_issued_inflight_insn;

    // keeps track of which warp is running the TM currently 
    // - other warp will not obtain the token until the current warp release it
    bool m_enable_tm_token; 
    unsigned m_n_tm_tokens; // number of available tokens for TM 
    std::set<unsigned> m_tm_token;
    std::list<unsigned> m_tm_masked_token; 
    // static const unsigned c_free_tm_token = -1; 
    void reserveTMToken(const warp_inst_t *inst);

    unsigned m_tm_rollback; 
    bool m_tm_commit; 

    std::vector<bool> m_in_tx_commit; // track which warp is currently committing

    //Register that depend on a long operation (global, local or tex memory)
    std::vector< std::set<unsigned> > longopregs;

    friend class simt_stack;
    simt_stack **m_simt_stack;
};

class SimpleScoreboard : public Scoreboard {
public:
    SimpleScoreboard( unsigned sid, unsigned n_warps, simt_stack **simt ) 
        : Scoreboard(sid, n_warps, simt) { } 
    
    virtual bool checkCollision(unsigned wid, const inst_t *inst) const; 
};

#endif /* SCOREBOARD_H_ */
