#ifndef TM_CONFLICT_DETECTOR_INCLUDED
#define TM_CONFLICT_DETECTOR_INCLUDED

#include "gpu-cache.h"
#include "shader.h"

#include <bitset>

#define MAX_CORES 64
#define TM_PACKET_SIZE 8

class tm_conflict_detector {
public:
    tm_conflict_detector( cache_config &config, mem_fetch_interface *port, const shader_core_config *shader_config, 
                          unsigned partition_id )
        : m_tags(config,0,0)
    { 
        m_data.resize(config.get_num_lines());
        m_response_port = port;
        m_nstid=1;
        m_invalidate_count=0;
        m_shader_config = shader_config;
        m_partition_id = partition_id;
    }

    // process queued work
    void cycle()
    {
        // e.g., send an abort message
        if( !m_response_queue.empty() ) {
            if( !m_response_port->full(TM_PACKET_SIZE,0) ) {
                mem_fetch *mf = m_response_queue.front();
                m_response_port->push(mf);
                m_response_queue.pop_front();
            }
        }
    }

    void update_skip_vector( unsigned tid )
    {
        m_skip_vector.set(tid-m_nstid); 
        while( m_skip_vector.test(0) ) {
            m_nstid++;
            m_skip_vector >>= 1; 
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u]         : nstid=%u, skip vector = %s\n", m_partition_id, m_nstid, m_skip_vector.to_string().c_str() );
#endif
        }
    }

    // return false if request should be sent to DRAM after accessing directory
    bool access( mem_fetch *mf, unsigned time )
    {
        bool done = false; // true simply means this request does not need to access DRAM

        enum mf_type  access_type = mf->get_type();
        new_addr_type addr = mf->get_addr();
        unsigned      sid  = mf->get_sid();
        unsigned      tpc  = mf->get_tpc();
        unsigned      tid  = mf->get_transaction_id(); // not set for all types of requests

        enum cache_request_status status;
        unsigned index;
        switch(access_type) {
        case TR_LOAD_REQ: 
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_LOAD : addr=0x%llx, sid=%u, tpc=%u\n", m_partition_id, addr, sid, tpc );
#endif
            status = m_tags.access(addr,time,index);
            assert( status != RESERVATION_FAIL );
            if( status == MISS ) 
                m_tags.fill(index,time,false);
            m_data[index].m_read_set.set(sid); 
            break;
        case TR_SKIP:
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_SKIP : tid=%u\n", m_partition_id, tid );
#endif
            update_skip_vector(tid); 
            done = true;
            delete mf;
            break;
        case TR_NSTID_PROBE_REQ: {
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_NSTID_PROBE_REQ : addr=0x%llx, sid=%u, tpc=%u => nstid=%u\n", m_partition_id, addr, sid, tpc, m_nstid );
#endif
            mem_fetch *r = new mem_fetch( mem_access_t(TR_MSG,addr,0,false),NULL,TM_PACKET_SIZE,-1,sid,tpc,NULL );
            r->set_type( TR_NSTID_PROBE_REPLY );
            r->set_transaction_id( m_nstid );
            r->set_is_transactional();
            r->set_memory_partition_id( m_partition_id );
            m_response_queue.push_back(r);
            done = true;
            delete mf;
            break;
        }
        case TR_MARK: 
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_MARK (tid=%u) : addr=0x%llx, sid=%u, tpc=%u\n", m_partition_id, tid,addr,sid,tpc );
#endif
            status = m_tags.access(addr,time,index);
            assert( status != RESERVATION_FAIL );
            if( status == MISS ) 
                m_tags.fill(index,time,false);
            assert( !m_data[index].m_marked );
            m_data[index].m_marked = true;
            m_data[index].m_tid = tid; 
            done = true;
            delete mf;
            break;
        case TR_COMMIT:
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_COMMIT (tid=%u) : sid=%u, tpc=%u\n", m_partition_id, tid ,sid, tpc );
#endif
            // should have received all marks by now...
            assert( m_invalidate_count == 0 );
            for( unsigned idx=0; idx < m_tags.size(); idx++ ) {
                blk_info &blk = m_data[idx];
                if( blk.m_marked ) {
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_COMMIT (tid=%u) : found marked line %u\n", m_partition_id, tid , idx );
#endif
                    assert( blk.m_tid == tid ); // if false, race condition?
                    blk.m_read_set.reset(sid); // reset self bit if set
                    new_addr_type block_addr = m_tags.get_block(idx).m_block_addr;
                    unsigned invalidates_sent=0;
                    for( unsigned r=0; r < blk.m_read_set.size(); r++ ) {
                        if( blk.m_read_set.test(r) ) {
                            if( r != sid ) {
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_COMMIT (tid=%u) : found read conflict with sid=%u\n", m_partition_id, tid, r );
#endif
                                mem_fetch *mf = new mem_fetch( mem_access_t(TR_MSG,block_addr,0,false),NULL,TM_PACKET_SIZE,-1,r,
                                                               m_shader_config->sid_to_cluster(r),NULL );
                                mf->set_type( TR_INVALIDATE );
                                mf->set_memory_partition_id( m_partition_id );
                                mf->set_is_transactional();
                                m_response_queue.push_back(mf);
                                invalidates_sent++;
                            }
                        }
                    }
                    if( invalidates_sent == 0 ) {
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_COMMIT (tid=%u) : no conflicts, clearing mark on idx=%u\n", m_partition_id, tid, idx );
#endif
                       blk.m_marked = false;
                       blk.m_valid = false;
                       cache_block_t &tag = m_tags.get_block(idx);
                       tag.invalidate();
                    }
                    m_invalidate_count+=invalidates_sent;
                }
            }
            // advance nstid if none of the marked lines has triggered a invalidate 
            if ( m_invalidate_count == 0 ) {
                update_skip_vector(tid); 
            }
            delete mf;
            done = true;
            break;
        case TR_INVALIDATE_ACK: {
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_INVALIDATE_ACK (tid=%u) : sid=%u, tpc=%u, addr=0x%llx\n", m_partition_id, tid ,sid, tpc, addr );
#endif
            status = m_tags.access(addr,time,index); 
            assert( status == HIT );
            blk_info &blk = m_data[index];
            assert( blk.m_tid != tid );
            blk.m_read_set.reset(sid);
            if( blk.m_read_set.none() ) {
                blk.m_marked = false;
                blk.m_valid = false;
                cache_block_t &tag = m_tags.get_block(index);
                tag.invalidate();
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_INVALIDATE_ACK (tid=%u) : clearing mark on idx=%u\n", m_partition_id, tid, index );
#endif
            }
            assert( m_invalidate_count > 0 );
            m_invalidate_count--;
            if( m_invalidate_count == 0 ) {
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_INVALIDATE_ACK (tid=%u) : sid=%u, tpc=%u -- DONE COMMIT,clearing mark\n", m_partition_id, tid ,sid, tpc );
#endif
//#ifdef DEBUG_TM
//            printf(" [tm conf. det.] [part=%u] TR_INVALIDATE_ACK (tid=%u) : clearing mark on idx=%u\n", m_partition_id, tid, index );
//#endif
            blk.m_marked=false;
            //cache_block_t &tag = m_tags.get_block(index);

                // now commit is done...
                m_skip_vector.set(0);
                while( m_skip_vector.test(0) ) {
                    m_nstid++;
                    m_skip_vector >>= 1; 
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u]          : nstid=%u, skip vector = %s\n", m_partition_id, m_nstid, m_skip_vector.to_string().c_str() );
#endif
                }
            }
            done = true;
            delete mf;
            break;
        }
        case TR_ABORT:
#ifdef DEBUG_TM
            printf(" [tm conf. det.] [part=%u] TR_ABORT (tid=%u) : sid=%u, tpc=%u\n", m_partition_id, tid ,sid, tpc );
#endif
            // clear marked lines
            for( unsigned idx=0; idx < m_tags.size(); idx++ ) {
                blk_info &blk = m_data[idx];
                if( blk.m_marked && blk.m_tid == tid )
                    blk.m_marked = false;
                blk.m_read_set.reset(sid);
                if( blk.m_read_set.none() && !blk.m_marked ) {
                    cache_block_t &tag = m_tags.get_block(idx);
                    tag.invalidate();
                    blk.m_valid = false;
                }
            }
            update_skip_vector(tid); 
            done = true;
            delete mf;
            break;
        default:
            break;
        }
        return done;
    }

private:

    struct blk_info {
        blk_info() { m_valid = false; m_marked=false;}
        void clear() { m_read_set.reset(); }

        bool m_valid;
        bool m_marked;
        unsigned m_tid; // tid that marked this line

        std::bitset<MAX_CORES> m_read_set;
    };

    unsigned m_nstid;
    std::bitset<MAX_CORES> m_skip_vector;
    unsigned m_invalidate_count; // number of invaliate acks to wait for before updating nstid

    // the directory 
    tag_array             m_tags;
    std::vector<blk_info> m_data;

    // interfaces
    std::list<mem_fetch*> m_response_queue; // abort messages generated
    mem_fetch_interface *m_response_port;

    const shader_core_config *m_shader_config;
    unsigned m_partition_id;
};


class tm_tid_vendor {
public:
    tm_tid_vendor( const shader_core_config *shader_config ) 
    {
        m_shader_config = shader_config;
        m_next_tid=1; 
        m_overflow_state=false;
        m_overflow_req=NULL;
        m_overflow_core=-1;
    }

    bool full() const
    {
        return m_requests.size() >= m_queue_size;
    }

    void push( mem_fetch *mf )
    {
        assert(!full());
        m_requests.push_back(mf);
    }

    mem_fetch *top()
    {
        if( m_response.empty() ) 
            return NULL;
        else
            return m_response.front();
    }
    mem_fetch *pop()
    {
        mem_fetch *result = NULL;
        if( !m_response.empty() ) {
            result = m_response.front();
            m_response.pop_front();
        }
        return result;
    }

    void cycle() 
    {
        if( !m_requests.empty() && m_response.size() < m_queue_size ) {
            mem_fetch *mf = m_requests.front();
            m_requests.pop_front();
            switch( mf->get_type() ) {
            case TR_TID_REQUEST:
                #ifdef DEBUG_TM
                printf(" [tm tid vendor] TR_TID_REQUEST : sid = %u, tid => %u\n", mf->get_sid(), m_next_tid );
                #endif
                mf->set_type(TR_TID_REPLY);
                mf->set_transaction_id(m_next_tid);
                m_next_tid++;
                m_response.push_back(mf);
                break;
            case TR_OVERFLOW_REQUEST_START:
                if( m_overflow_state ) 
                    //reply_abort(mf); // can't handle more than one... send abort back
                	reply_overflow_stop(mf);
                else {
                    m_overflow_state = true;
                    m_overflow_req = mf;
                    m_overflow_core = mf->get_sid();
                    for( unsigned i=0; i < m_shader_config->num_shader(); i++ ) {
                        if( i == mf->get_sid() ) 
                            continue;
                        // send request for overflow 
                        mem_fetch *mf = new mem_fetch( mem_access_t(TR_MSG,0,0,false),NULL,TM_PACKET_SIZE,
                                                       -1,i,m_shader_config->sid_to_cluster(i),NULL );
                        mf->set_type(TR_OVERFLOW_STOP);
                        mf->set_is_transactional();
                        m_response.push_back(mf); // allow this to go over limit for uncommon case
                        m_req_acks.set(i);
                    }
                }
                break;
            case TR_OVERFLOW_STOP_ACK:
                assert( m_req_acks.test( mf->get_sid() ) );
                m_req_acks.reset( mf->get_sid() );
                delete mf;
                if( m_req_acks.none() ) {
                    m_overflow_req->set_type(TR_OVERFLOW_REQUEST_START_ACK);
                    m_response.push_back(m_overflow_req);
                    m_overflow_req = NULL;
                }
                break;
            case TR_OVERFLOW_DONE:
                for( unsigned i=0; i < m_shader_config->num_shader(); i++ ) {
                    if( i == mf->get_sid() ) 
                        continue;
                    // send request for overflow 
                    mem_fetch *r = new mem_fetch( mem_access_t(TR_MSG,0,0,false),NULL,TM_PACKET_SIZE,
                                                   -1,i,m_shader_config->sid_to_cluster(i),NULL );
                    r->set_type(TR_OVERFLOW_RESUME);
                    r->set_is_transactional();
                    m_response.push_back(r); // allow this to go over limit for uncommon case
                }
                m_overflow_state=false;
                delete mf;
                break;
            default:
                abort();
            }
        }
    }
    
private:
    void reply_abort( mem_fetch *mf )
    {
        mf->set_type(TR_ABORT);
        m_response.push_back(mf);
    }

    void reply_overflow_stop( mem_fetch *mf )
	{
            mf->set_type(TR_OVERFLOW_STOP);
            m_response.push_back(mf);
	}
    const shader_core_config *m_shader_config;

    unsigned m_next_tid;

    static const unsigned m_queue_size = 4;
    std::list<mem_fetch*> m_requests;
    std::list<mem_fetch*> m_response;

    // arbitration of overflow transactions
    bool m_overflow_state;
    mem_fetch *m_overflow_req;
    unsigned m_overflow_core;
    std::bitset<MAX_CORES> m_req_acks; // set to 1 when overflow start request sent, set to 0 when ack received
};

#endif
