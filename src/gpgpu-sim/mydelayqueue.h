#ifndef MYDELAYQUEUE_H
#define MYDELAYQUEUE_H

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <queue>
#include <set>
#include "../abstract_hardware_model.h"
#include "../cuda-sim/memory.h"
#include "gpu-sim.h"
#include "comp.h"

extern unsigned long long  gpu_sim_cycle;
extern unsigned long long  gpu_tot_sim_cycle;

extern gpgpu_sim* g_the_gpu;

class my_delay_queue {
public:
    my_delay_queue(const char* nm, unsigned int size, unsigned int latency)
    {
        assert(latency);

        m_name = nm;
        m_size = size;
        m_latency = latency;
        m_arr_size = size + latency;

        m_data_array = new mem_fetch*[m_arr_size];
        m_is_head_array = new bool[m_arr_size];
        m_is_tail_array = new bool[m_arr_size];

        m_wr_ptr = latency;
        m_rd_ptr = 0;

        for (unsigned i=0; i<latency; i++) {
            m_data_array[i] = NULL;
            m_is_head_array[i] = false;
            m_is_tail_array[i] = false;
        }
    }

    ~my_delay_queue() 
    {
        delete [] m_data_array;
        delete [] m_is_head_array;
        delete [] m_is_tail_array;
    }

    void push(bool is_head, bool is_tail, mem_fetch* mf)
    {
        //if (mf!=NULL) {
        //    printf("MDQ::push %p %d %d %d\n", mf, is_head, is_tail, m_wr_ptr);
        //}
        m_data_array[m_wr_ptr] = mf;
        m_is_head_array[m_wr_ptr] = is_head;
        m_is_tail_array[m_wr_ptr] = is_tail;
        m_wr_ptr = (m_wr_ptr+1) % m_arr_size;
    }
    mem_fetch *pop()
    {
        mem_fetch *result = NULL;
        if (m_is_tail_array[m_rd_ptr]) {
            result = m_data_array[m_rd_ptr];
            //printf("MDQ::pop  %p %d\n", result, m_rd_ptr);
        }
        m_rd_ptr = (m_rd_ptr+1) % m_arr_size;
        return result;
    }

    void print() const
    {
        printf("@%8lld %s : %d, %d\n", gpu_sim_cycle, m_name, m_rd_ptr, m_wr_ptr);
    }
    const char* get_name() { return m_name; }

protected:
    const char* m_name;

    unsigned int m_latency;
    unsigned int m_size;
    unsigned int m_arr_size;

    unsigned int m_wr_ptr;
    unsigned int m_rd_ptr;

    mem_fetch **m_data_array;
    bool *m_is_head_array;
    bool *m_is_tail_array;
};

class oneway_link {
public:
    oneway_link(const char* nm, unsigned latency, unsigned src_cnt, unsigned dst_cnt)
    : m_src_cnt(src_cnt), m_dst_cnt(dst_cnt) {
        strcpy(m_name, nm);
        queue = new my_delay_queue(nm, 4000, latency);   // Queue of FLITs
        m_ready_list = new std::queue<mem_fetch *>[m_src_cnt];
        m_complete_list = new std::queue<mem_fetch *>[m_dst_cnt];

        m_cur_src_id = 0;
        m_cur_flit_cnt = 0;
        m_total_flit_cnt = 0ull;
        m_transfer_flit_cnt = 0ull;
        m_transfer_single_flit_cnt = 0ull;
        m_transfer_multi_flit_cnt = 0ull;
    }
    ~oneway_link() {
        delete queue;
        delete [] m_ready_list;
        delete [] m_complete_list;
    }
    bool full(unsigned src_id) {
        return (m_ready_list[src_id].size()>=4);
    }
    virtual void push(unsigned src_id, mem_fetch *mf) {
        //if (gpu_sim_cycle > 400000000) {
        //    printf("@%08lld %s OL::push() %2d %8u\n", gpu_sim_cycle+gpu_tot_sim_cycle, m_name, src_id, mf==NULL ? 0 : mf->get_request_uid()); mf->print(stdout, false);
        //}
        assert(!full(src_id));
        m_ready_list[src_id].push(mf);
    }
    unsigned get_dst_id(mem_fetch *mf) { return mf->get_sub_partition_id(); }
    bool empty(unsigned dst_id) {
        return (m_complete_list[dst_id].size()==0);
    }
    mem_fetch *top(unsigned dst_id) {
        if (!empty(dst_id)) {
            mem_fetch *mf = m_complete_list[dst_id].front();
            //if (mf!=NULL) {
            //    if (gpu_sim_cycle > 400000000) {
            //        printf("@%08lld %s OL::top()  %2d %8u\n", gpu_sim_cycle+gpu_tot_sim_cycle, m_name, dst_id, mf==NULL ? 0 : mf->get_request_uid());
            //    }
            //}
            return mf;
        }
        return NULL;
    }
    void pop(unsigned dst_id) {
        assert (!empty(dst_id));
        mem_fetch *mf = m_complete_list[dst_id].front();
        assert(mf!=NULL);
        m_complete_list[dst_id].pop();
        //if (gpu_sim_cycle > 400000000) {
        //    printf("@%08lld %s OL::pop()  %2d %8u\n", gpu_sim_cycle+gpu_tot_sim_cycle, m_name, dst_id, mf==NULL ? 0 : mf->get_request_uid());
        //}
    }
    virtual void step_link_pop(unsigned n_flit) {
        // pop old entries
        for (unsigned i=0; i<n_flit; i++) {
            mem_fetch *mf = queue->pop();
            if (mf!=NULL) {
                //printf("QQ:pop  %p %8u\n", mf, mf->get_request_uid());
                unsigned dst_id = get_dst_id(mf);
                //if (m_complete_list[dst_id].size()>=1000) {
                //    assert(0);
                //}
                m_complete_list[dst_id].push(mf);
            }
        }
    }
    virtual void step_link_push(unsigned n_flit) {
        // push new entries
        unsigned n_sent_flit_cnt = 0;
        for (unsigned i=0; (i<m_src_cnt) && (n_sent_flit_cnt<n_flit); i++) {
            unsigned src_id = (m_cur_src_id+i) % m_src_cnt;
            if (m_ready_list[src_id].size()>0) {
                mem_fetch *mf = m_ready_list[src_id].front();
                if (m_cur_flit_cnt==0) {    // first FLIT of a request
                    if ((mf->get_type()==READ_REQUEST)||(mf->get_type()==WRITE_ACK)) {
                        m_packet_bit_size = HT_OVERHEAD;
                    } else if ((mf->get_type()==WRITE_REQUEST)||(mf->get_type()==READ_REPLY)) {
                        m_packet_bit_size = HT_OVERHEAD + mf->get_data_size()*8;
                    } else {
                        assert(0);
                    }
                }

                for (unsigned i=m_cur_flit_cnt*FLIT_SIZE; (i<m_packet_bit_size) && (n_sent_flit_cnt<n_flit); i+=FLIT_SIZE) {
                    bool is_first = (i==0);
                    bool is_last = (i>=(m_packet_bit_size-FLIT_SIZE));
                    queue->push(is_first, is_last, mf);
                    n_sent_flit_cnt++;

                    if (is_last) {
                        m_ready_list[src_id].pop();
                        m_cur_src_id = (src_id+1) % m_src_cnt;
                        m_cur_flit_cnt = 0;
                    } else {
                        m_cur_flit_cnt++;
                    }
                    if (m_packet_bit_size==HT_OVERHEAD) {
                        m_transfer_single_flit_cnt++;
                    } else {
                        m_transfer_multi_flit_cnt++;
                    }
                    m_transfer_flit_cnt++;
                }
            }
        }

        for (; n_sent_flit_cnt < n_flit; n_sent_flit_cnt++) {
            queue->push(false, false, NULL);
        }
    }
    void step(unsigned n_flit)
    {
        m_total_flit_cnt += n_flit;

        step_link_pop(n_flit);

        step_link_push(n_flit);
    }
    void print() const {
        queue->print();
    }
    void print_stat() const {
        printf("%s TOT %f (%lld/%lld)\n", m_name, m_transfer_flit_cnt*1./m_total_flit_cnt, m_transfer_flit_cnt, m_total_flit_cnt);
        printf("%s SIN %f (%lld/%lld)\n", m_name, m_transfer_single_flit_cnt*1./m_total_flit_cnt, m_transfer_single_flit_cnt, m_total_flit_cnt);
        printf("%s MUL %f (%lld/%lld)\n", m_name, m_transfer_multi_flit_cnt*1./m_total_flit_cnt, m_transfer_multi_flit_cnt, m_total_flit_cnt);
    }
protected:
    static const unsigned FLIT_SIZE = 128;      // max packet length in terms of FLIP
    static const unsigned HT_OVERHEAD = 128;    // head+tail overheads
    //static const unsigned MAX_FLIT_CNT = 17;
    //static const unsigned WIDTH = 32;

    char m_name[256];
    my_delay_queue *queue;
    unsigned m_src_cnt, m_dst_cnt;
    unsigned m_cur_src_id;
    unsigned m_cur_flit_cnt;
    unsigned m_packet_bit_size;
    unsigned long long m_total_flit_cnt;
    unsigned long long m_transfer_flit_cnt;
    unsigned long long m_transfer_single_flit_cnt;
    unsigned long long m_transfer_multi_flit_cnt;
    std::queue<mem_fetch *> *m_ready_list;
    std::queue<mem_fetch *> *m_complete_list;
};

//--------------------------------------------------------------------
// Compressed link interface
//--------------------------------------------------------------------
class my_delay_queue2 {
public:
    my_delay_queue2(const char* nm, unsigned int size, unsigned int latency)
    {
        assert(latency);

        m_name = nm;
        m_size = size;
        m_latency = latency;
        m_arr_size = size + latency;

        m_data_array = new mem_fetch*[m_arr_size];
        m_size_array = new unsigned[m_arr_size];
        m_time_array = new unsigned long long[m_arr_size];

        m_wr_ptr = 0;
        m_rd_ptr = 0;

        for (unsigned i=0; i<m_arr_size; i++) {
            m_data_array[i] = NULL;
            m_size_array[i] = 0;
            m_time_array[i] = 0ull;
        }
    }

    ~my_delay_queue2() 
    {
        delete [] m_data_array;
        delete [] m_size_array;
        delete [] m_time_array;
    }

    void push(mem_fetch* mf, unsigned size)
    {
        //if (mf!=NULL) {
        //    printf("MDQ::push %p %d %d %d\n", mf, is_head, is_tail, m_wr_ptr);
        //}
        m_data_array[m_wr_ptr] = mf;
        m_size_array[m_wr_ptr] = size;
        m_time_array[m_wr_ptr] = gpu_sim_cycle + gpu_tot_sim_cycle;
        m_wr_ptr = (m_wr_ptr+1) % m_arr_size;
    }
    pair<mem_fetch *, unsigned> top()
    {
        mem_fetch* mf = m_data_array[m_rd_ptr];
        unsigned size = 0;
        if (mf!=NULL) {
            unsigned long long time = m_time_array[m_rd_ptr];
            if ((gpu_sim_cycle+gpu_tot_sim_cycle) > (time + m_latency)) {
                size = m_size_array[m_rd_ptr];
            } else {
                mf = NULL;
            }
        }
        return make_pair(mf, size);
    }

    void pop()
    {
        m_data_array[m_rd_ptr] = NULL;
        m_rd_ptr = (m_rd_ptr+1) % m_arr_size;
    }

    void print() const
    {
        printf("@%8lld %s : %d, %d\n", gpu_sim_cycle, m_name, m_rd_ptr, m_wr_ptr);
    }
    const char* get_name() { return m_name; }

protected:
    const char* m_name;

    unsigned int m_latency;
    unsigned int m_size;
    unsigned int m_arr_size;

    unsigned int m_wr_ptr;
    unsigned int m_rd_ptr;

    mem_fetch **m_data_array;
    unsigned *m_size_array;
    unsigned long long *m_time_array;
};


class compressed_oneway_link : public oneway_link {
public:
    compressed_oneway_link(const char* nm, unsigned latency, unsigned src_cnt, unsigned dst_cnt)
    : oneway_link(nm, latency, src_cnt, dst_cnt) {
        m_ready_long_list = new std::queue<mem_fetch *>[src_cnt];
        m_ready_short_list = new std::queue<mem_fetch *>[src_cnt];
        m_ready_decompressed = new my_delay_queue2(nm, 4000, 10);    // 10 cycle latency

        is_current_long = false;
        m_cur_comp_id = 0;
        m_leftover = 0;
    }

    void push(unsigned mem_id, mem_fetch *mf) {
        assert(!full(mem_id));
        if ((mf->get_type()==WRITE_REQUEST)||(mf->get_type()==READ_REPLY)) {
            m_ready_long_list[mem_id].push(mf);
        } else {
            m_ready_short_list[mem_id].push(mf);
        }
        //printf("OL:push %p %8u\n", mf, mf->get_request_uid());
    }

    bool push(mem_fetch *mf, unsigned packet_bit_size, unsigned& n_sent_flit_cnt, unsigned n_flit, bool update = true) {
        //printf("PP %p %d %d (%d %d)\n", mf, m_cur_flit_cnt, packet_bit_size, n_sent_flit_cnt, n_flit);
        for (unsigned i=m_cur_flit_cnt*FLIT_SIZE; i<packet_bit_size; i+=FLIT_SIZE) {
            if (n_sent_flit_cnt==n_flit) {
                return false;
            }
            bool is_first = (i==0);
            bool is_last = (i>=(packet_bit_size-FLIT_SIZE));
            queue->push(is_first, is_last, mf);
            //printf(" - %p %d %d\n", mf, is_first, is_last);
            n_sent_flit_cnt++;
            if (update) {
                if (is_last) {
                    m_cur_flit_cnt = 0;
                } else {
                    m_cur_flit_cnt++;
                }
            }
            if (packet_bit_size==HT_OVERHEAD) {
                m_transfer_single_flit_cnt++;
            } else {
                m_transfer_multi_flit_cnt++;
            }
            m_transfer_flit_cnt++;
        }
        return true;
    }

public:
    std::queue<mem_fetch *> *m_ready_long_list;
    std::queue<mem_fetch *> *m_ready_short_list;
    my_delay_queue2 *m_ready_compressed;
    my_delay_queue2 *m_ready_decompressed;
    unsigned m_cur_comp_id;
    bool is_current_long;
    unsigned m_leftover;
};

class compressed_dn_link : public compressed_oneway_link {
public:
    compressed_dn_link(const char* nm, unsigned latency, unsigned src_cnt, unsigned dst_cnt)
    : compressed_oneway_link(nm, latency, src_cnt, dst_cnt) {
        m_ready_compressed = new my_delay_queue2(nm, 4000, 1);
    }
    void step_link_push(unsigned n_flit) {
        unsigned n_sent_flit_cnt = 0;

        // Priorities
        // 1. Write request if there is an on-going write request
        // 2. Read request if no left-over space
        // 3. Write request if no read request

        // 1. Write request if there is an on-going write request
        while ((n_sent_flit_cnt<n_flit) && (m_cur_flit_cnt!=0)) {
            auto it = m_ready_compressed->top();
            if (it.first!=NULL) {
                assert(it.first->get_type()==WRITE_REQUEST);
                if (m_cur_flit_cnt==0) {    // this is the first FLIT of a packet
                    unsigned packet_bit_size = HT_OVERHEAD+it.second-m_leftover;
                    m_packet_bit_size = ((packet_bit_size+FLIT_SIZE-1)/FLIT_SIZE)*FLIT_SIZE;
                    m_leftover = ((packet_bit_size%FLIT_SIZE)==0) ? 0 : FLIT_SIZE - (packet_bit_size%FLIT_SIZE);
                }

                bool is_complete = push(it.first, m_packet_bit_size, n_sent_flit_cnt, n_flit);
                if (is_complete) {
                    m_ready_compressed->pop();
                }
            } else {
                break;
            }
        }

        // 2. Read request if no left-over space
        for (unsigned i=0; (i<m_src_cnt) && (n_sent_flit_cnt<n_flit); i++) {
            unsigned src_id = (m_cur_src_id+i) % m_src_cnt;
            if (m_ready_short_list[src_id].size()>0) {
                mem_fetch *mf = m_ready_short_list[src_id].front();
                assert(mf->get_type()==READ_REQUEST);
                m_packet_bit_size = HT_OVERHEAD;
                bool is_complete = push(mf, m_packet_bit_size, n_sent_flit_cnt, n_flit, false);
                assert(is_complete);
                m_ready_short_list[src_id].pop();
            }
        }

        // 3. Write request if no read request
        while (n_sent_flit_cnt<n_flit) {
            auto it = m_ready_compressed->top();
            if (it.first!=NULL) {
                assert(it.first->get_type()==WRITE_REQUEST);
                if (m_cur_flit_cnt==0) {
                    unsigned packet_bit_size = HT_OVERHEAD+it.second-m_leftover;
                    m_packet_bit_size = ((packet_bit_size+FLIT_SIZE-1)/FLIT_SIZE)*FLIT_SIZE;
                    m_leftover = ((packet_bit_size%FLIT_SIZE)==0) ? 0 : FLIT_SIZE - (packet_bit_size%FLIT_SIZE);
                }

                bool is_complete = push(it.first, m_packet_bit_size, n_sent_flit_cnt, n_flit);
                if (is_complete) {
                    m_ready_compressed->pop();
                }
            } else {
                break;
            }
        }

        for (; n_sent_flit_cnt < n_flit; n_sent_flit_cnt++) {
            queue->push(false, false, NULL);
            m_leftover = 0;     // left-over space is discarded
        }
        assert(n_sent_flit_cnt==n_flit);

        // Compress write requests
        for (unsigned i=0; i<m_src_cnt; i++) {
            unsigned src_id = (m_cur_comp_id+i) % m_src_cnt;
            assert(m_ready_long_list[src_id].size()<=1);
            if (m_ready_long_list[src_id].size()>0) {
                unsigned char buffer[128];
                unsigned comp_bit_size;

                // compress
                mem_fetch *mf = m_ready_long_list[src_id].front();
                if (mf->get_data_size() == 128) {   // for now, compress only 128B blocks only
                    static int cnt = 0;
                    g_the_gpu->get_global_memory()->read(mf->get_addr(), mf->get_data_size(), buffer);
                    comp_bit_size = g_comp->compress(mf->get_vstream_id(), buffer, mf->get_addr(), mf->get_data_size());
                    cnt += comp_bit_size;
                    if (cnt > 1024) {   // spread over two packets
                        cnt -= 1024;
                    } else {            // compacted packet --> TAG overhead
                        comp_bit_size += 11;
                    }
                } else {
                    comp_bit_size = mf->get_data_size() * 8;
                }

                m_ready_compressed->push(mf, comp_bit_size);
                m_ready_long_list[src_id].pop();
            }
        }
    }
    void step_link_pop(unsigned n_flit) {
        // pop old entries
        for (unsigned i=0; i<n_flit; i++) {
            mem_fetch *mf = queue->pop();
            if (mf!=NULL) {
                //printf("QQ:pop  %p %8u\n", mf, mf->get_request_uid());
                if (mf->get_type()==READ_REQUEST) { // no decompression
                    unsigned dst_id = get_dst_id(mf);
                    if (m_complete_list[dst_id].size()>=1000) {
                        assert(0);
                    }
                    m_complete_list[dst_id].push(mf);
                } else {
                    assert(mf->get_type()==WRITE_REQUEST);  // decompression
                    unsigned dst_id = get_dst_id(mf);
                    if (m_complete_list[dst_id].size()>=1000) {
                        assert(0);
                    }
                    m_complete_list[dst_id].push(mf);
                }
            }
        }
    }
};

class compressed_up_link : public compressed_oneway_link {
public:
    compressed_up_link(const char* nm, unsigned latency, unsigned src_cnt, unsigned dst_cnt)
    : compressed_oneway_link(nm, latency, src_cnt, dst_cnt) {
        m_ready_compressed = new my_delay_queue2(nm, 4000, 1);
    }
    void step_link_push(unsigned n_flit) {
        unsigned n_sent_flit_cnt = 0;

        // Priorities
        // 1. Read data
        // 2. Write acknowledge if no read data

        // 1. Read data
        while (n_sent_flit_cnt<n_flit) {
            pair<mem_fetch *, unsigned> it = m_ready_compressed->top();
            if (it.first!=NULL) {
                //printf("TOP1 @%08d %p %d\n", gpu_sim_cycle, it.first, it.first->get_request_uid());
                //it.first->print(stdout, false);
                assert(it.first->get_type()==READ_REPLY);
                if (m_cur_flit_cnt==0) {
                    unsigned packet_bit_size = HT_OVERHEAD+it.second-m_leftover;
                    m_packet_bit_size = ((packet_bit_size+FLIT_SIZE-1)/FLIT_SIZE)*FLIT_SIZE;
                    m_leftover = ((packet_bit_size%FLIT_SIZE)==0) ? 0 : FLIT_SIZE - (packet_bit_size%FLIT_SIZE);
                }

                bool is_complete = push(it.first, m_packet_bit_size, n_sent_flit_cnt, n_flit);
                if (is_complete) {
                    //printf("POP1 @%08d %p %d\n", gpu_sim_cycle, it.first, it.first->get_request_uid());
                    m_ready_compressed->pop();
                }
            } else {
                break;
            }
        }

        // 2. Write acknowledge if no read data
        for (unsigned i=0; (i<m_src_cnt) && (n_sent_flit_cnt<n_flit); i++) {
            unsigned src_id = (m_cur_src_id+i) % m_src_cnt;
            if (m_ready_short_list[src_id].size()>0) {
                mem_fetch *mf = m_ready_short_list[src_id].front();
                assert(mf->get_type()==WRITE_ACK);
                m_packet_bit_size = HT_OVERHEAD;
                bool is_complete = push(mf, m_packet_bit_size, n_sent_flit_cnt, n_flit);
                assert(is_complete);
                m_ready_short_list[src_id].pop();
            }
        }

        for (; n_sent_flit_cnt < n_flit; n_sent_flit_cnt++) {
            queue->push(false, false, NULL);
            m_leftover = 0;
        }
        assert(n_sent_flit_cnt==n_flit);

        // Compress read data
        for (unsigned i=0; i<m_src_cnt; i++) {
            unsigned src_id = (m_cur_comp_id+i) % m_src_cnt;
            assert(m_ready_long_list[src_id].size()<=1);
            if (m_ready_long_list[src_id].size()>0) {
                unsigned char buffer[128];
                unsigned comp_bit_size;

                // compress
                mem_fetch *mf = m_ready_long_list[src_id].front();
                if (mf->get_data_size() == 128) {
                    static int cnt = 0;

                    g_the_gpu->get_global_memory()->read(mf->get_addr(), mf->get_data_size(), buffer);
                    comp_bit_size = g_comp->compress(mf->get_vstream_id(), buffer, mf->get_addr(), mf->get_data_size());

                    cnt += comp_bit_size;
                    if (cnt > 1024) {   // spread over two packets
                        cnt -= 1024;
                    } else {            // compacted packet --> TAG overhead
                        comp_bit_size += 11;
                    }
                } else {
                    comp_bit_size = mf->get_data_size() * 8;
                }

                //printf("PUSH @%08d %p %d\n", gpu_sim_cycle, mf, mf->get_request_uid());
                m_ready_compressed->push(mf, comp_bit_size);
                m_ready_long_list[src_id].pop();
            }
        }
    }
    /*
    void step_link_push(unsigned n_flit) {
        unsigned n_sent_flit_cnt = 0;
        unsigned loop = 0;
        for (unsigned loop = 0; loop < 2; loop++) {
            std::queue<mem_fetch *> *ready_list;
            if ( ((loop==0) && is_current_long) || ((loop==1) && !is_current_long) ) {
                ready_list = m_ready_long_list;
            } else {
                ready_list = m_ready_short_list;
            }
            for (unsigned i=0; (i<m_src_cnt) && (n_sent_flit_cnt<n_flit); i++) {
                unsigned src_id = (m_cur_src_id+i) % m_src_cnt;
                if (ready_list[src_id].size()>0) {
                    mem_fetch *mf = ready_list[src_id].front();
                    if (m_cur_flit_cnt==0) {    // first FLIT of a request
                        if ((mf->get_type()==READ_REQUEST)||(mf->get_type()==WRITE_ACK)) {
                            m_packet_bit_size = HT_OVERHEAD;
                        } else if ((mf->get_type()==WRITE_REQUEST)||(mf->get_type()==READ_REPLY)) {
                            if (mf->get_data_size() == 128) {
                                unsigned char buffer[128];
                                unsigned comp_size;
                                unsigned packet_bit_size;
                                g_the_gpu->get_global_memory()->read(mf->get_addr(), mf->get_data_size(), buffer);
                                comp_size = g_comp->compress(mf->get_vstream_id(), buffer, mf->get_data_size());
                                packet_bit_size = HT_OVERHEAD + comp_size - m_leftover;
                                m_packet_bit_size = ((packet_bit_size+FLIT_SIZE-1) / FLIT_SIZE) * FLIT_SIZE;
                                m_leftover = ((packet_bit_size%FLIT_SIZE)==0) ? 0 : FLIT_SIZE - (packet_bit_size%FLIT_SIZE);
                            } else {
                                m_packet_bit_size = HT_OVERHEAD + mf->get_data_size()*8;
                            }
                        } else {
                            assert(0);
                        }
                    }

                    for (unsigned i=m_cur_flit_cnt*FLIT_SIZE; (i<m_packet_bit_size) && (n_sent_flit_cnt<n_flit); i+=FLIT_SIZE) {
                        bool is_first = (i==0);
                        bool is_last = (i>=(m_packet_bit_size-FLIT_SIZE));
                        queue->push(is_first, is_last, mf);
                        //printf("QQ:push %p %8u (%d %d)\n", mf, mf->get_request_uid(), is_first, is_last);
                        n_sent_flit_cnt++;

                        if (is_last) {
                            ready_list[src_id].pop();
                            m_cur_src_id = (src_id+1) % m_src_cnt;
                            if (m_packet_bit_size==HT_OVERHEAD) {
                                m_transfer_single_flit_cnt++;
                            } else {
                                m_transfer_multi_flit_cnt += (m_packet_bit_size/FLIT_SIZE);
                            }
                            m_cur_flit_cnt = 0;
                        } else {
                            m_cur_flit_cnt++;
                        }
                    }
                }
            }
            if (n_sent_flit_cnt==n_flit) {
                break;
            }
        }
        if (loop==2) {  // both are empty
            is_current_long = false;
        }

        //if (n_sent_flit_cnt < n_flit) {
        //    printf("@%08lld %s fill %d\n", gpu_sim_cycle+gpu_tot_sim_cycle, m_name, n_flit - n_sent_flit_cnt);
        //}
        m_transfer_flit_cnt += n_sent_flit_cnt;
        for (; n_sent_flit_cnt < n_flit; n_sent_flit_cnt++) {
            queue->push(false, false, NULL);
        }
    }
    */
};

class memory_link {
public:
    memory_link(const char* nm, unsigned int latency, const struct memory_config *config)
    : m_config(config) {
        strcpy(m_nm, nm);

        char link_nm[256];
        sprintf(link_nm, "%s.dn", nm);
        m_dn = new oneway_link(link_nm, latency, config->m_n_mem * config->m_n_sub_partition_per_memory_channel, config->m_n_mem * config->m_n_sub_partition_per_memory_channel);
        sprintf(link_nm, "%s.up", nm);
        m_up = new oneway_link(link_nm, latency, config->m_n_mem * config->m_n_sub_partition_per_memory_channel, config->m_n_mem * config->m_n_sub_partition_per_memory_channel);

        dnlink_remainder = 0.;
        uplink_remainder = 0.;
    }
    ~memory_link() {
        delete m_dn;
        delete m_up;
    }
    void dnlink_step(double n_flit) {
        unsigned flit_rounded = (unsigned) (n_flit + dnlink_remainder);
        m_dn->step(flit_rounded);
        dnlink_remainder = (n_flit + dnlink_remainder) - flit_rounded;
    }
    bool dnlink_full(unsigned mem_id) { return m_dn->full(mem_id); }
    void dnlink_push(unsigned mem_id, mem_fetch *mf) { m_dn->push(mem_id, mf); }
    bool dnlink_empty(unsigned mem_id) { return m_dn->empty(mem_id); }
    mem_fetch *dnlink_top(unsigned mem_id) { return m_dn->top(mem_id); }
    void dnlink_pop(unsigned mem_id) { m_dn->pop(mem_id); }

    void uplink_step(double n_flit) {
        unsigned flit_rounded = (unsigned) (n_flit + uplink_remainder);
        m_up->step(flit_rounded);
        uplink_remainder = (n_flit + uplink_remainder) - flit_rounded;
    }
    bool uplink_full(unsigned mem_id) { return m_up->full(mem_id); }
    void uplink_push(unsigned mem_id, mem_fetch *mf) { m_up->push(mem_id, mf); }
    bool uplink_empty(unsigned mem_id) { return m_up->empty(mem_id); }
    mem_fetch *uplink_top(unsigned mem_id) { return m_up->top(mem_id); }
    void uplink_pop(unsigned mem_id) { m_up->pop(mem_id); }

    void print() const {
        m_dn->print();
        m_up->print();
    }
    void print_stat() const {
        m_dn->print_stat();
        m_up->print_stat();
    }
protected:
    double dnlink_remainder;
    double uplink_remainder;

    const struct memory_config *m_config;
    char m_nm[256];
    class oneway_link *m_dn;
    class oneway_link *m_up;
};

class compressed_memory_link : public memory_link {
public:
    compressed_memory_link(const char* nm, unsigned int latency, const struct memory_config *config)
    : memory_link(nm, latency, config) {
        strcpy(m_nm, nm);

        char link_nm[256];
        sprintf(link_nm, "%s.dn", nm);
        m_dn = new compressed_dn_link(link_nm, latency, config->m_n_mem * config->m_n_sub_partition_per_memory_channel, config->m_n_mem * config->m_n_sub_partition_per_memory_channel);
        sprintf(link_nm, "%s.up", nm);
        m_up = new compressed_up_link(link_nm, latency, config->m_n_mem * config->m_n_sub_partition_per_memory_channel, config->m_n_mem * config->m_n_sub_partition_per_memory_channel);
    }
};

class compressed_unpacked_dn_link : public compressed_oneway_link {
public:
    compressed_unpacked_dn_link(const char* nm, unsigned latency, unsigned src_cnt, unsigned dst_cnt)
    : compressed_oneway_link(nm, latency, src_cnt, dst_cnt) {
        m_ready_compressed = new my_delay_queue2(nm, 4000, 1);
    }
    void step_link_push(unsigned n_flit) {
        unsigned n_sent_flit_cnt = 0;

        // Priorities
        // 1. Write request if there is an on-going write request
        // 2. Read request if no left-over space
        // 3. Write request if no read request

        // 1. Write request if there is an on-going write request
        while ((n_sent_flit_cnt<n_flit) && (m_cur_flit_cnt!=0)) {
            auto it = m_ready_compressed->top();
            if (it.first!=NULL) {
                assert(it.first->get_type()==WRITE_REQUEST);
                if (m_cur_flit_cnt==0) {    // this is the first FLIT of a packet
                    assert(it.second%FLIT_SIZE==0);
                    m_packet_bit_size = HT_OVERHEAD+it.second;
                }

                bool is_complete = push(it.first, m_packet_bit_size, n_sent_flit_cnt, n_flit);
                if (is_complete) {
                    m_ready_compressed->pop();
                }
            } else {
                break;
            }
        }

        // 2. Read request if no left-over space
        for (unsigned i=0; (i<m_src_cnt) && (n_sent_flit_cnt<n_flit); i++) {
            unsigned src_id = (m_cur_src_id+i) % m_src_cnt;
            if (m_ready_short_list[src_id].size()>0) {
                mem_fetch *mf = m_ready_short_list[src_id].front();
                assert(mf->get_type()==READ_REQUEST);
                m_packet_bit_size = HT_OVERHEAD;
                bool is_complete = push(mf, m_packet_bit_size, n_sent_flit_cnt, n_flit, false);
                assert(is_complete);
                m_ready_short_list[src_id].pop();
            }
        }

        // 3. Write request if no read request
        while (n_sent_flit_cnt<n_flit) {
            auto it = m_ready_compressed->top();
            if (it.first!=NULL) {
                assert(it.first->get_type()==WRITE_REQUEST);
                if (m_cur_flit_cnt==0) {
                    assert(it.second%FLIT_SIZE==0);
                    m_packet_bit_size = HT_OVERHEAD+it.second;
                }

                bool is_complete = push(it.first, m_packet_bit_size, n_sent_flit_cnt, n_flit);
                if (is_complete) {
                    m_ready_compressed->pop();
                }
            } else {
                break;
            }
        }

        for (; n_sent_flit_cnt < n_flit; n_sent_flit_cnt++) {
            queue->push(false, false, NULL);
            m_leftover = 0;     // left-over space is discarded
        }
        assert(n_sent_flit_cnt==n_flit);

        // Compress write requests
        for (unsigned i=0; i<m_src_cnt; i++) {
            unsigned src_id = (m_cur_comp_id+i) % m_src_cnt;
            assert(m_ready_long_list[src_id].size()<=1);
            if (m_ready_long_list[src_id].size()>0) {
                unsigned char buffer[128];
                unsigned comp_bit_size;

                // compress
                mem_fetch *mf = m_ready_long_list[src_id].front();
                if (mf->get_data_size() == 128) {   // for now, compress only 128B blocks only
                    g_the_gpu->get_global_memory()->read(mf->get_addr(), mf->get_data_size(), buffer);
                    comp_bit_size = g_comp->compress(mf->get_vstream_id(), buffer, mf->get_addr(), mf->get_data_size());
                    comp_bit_size = ((comp_bit_size+FLIT_SIZE-1)/FLIT_SIZE)*FLIT_SIZE;
                } else {
                    comp_bit_size = mf->get_data_size() * 8;
                }

                m_ready_compressed->push(mf, comp_bit_size);
                m_ready_long_list[src_id].pop();
            }
        }
    }
    void step_link_pop(unsigned n_flit) {
        // pop old entries
        for (unsigned i=0; i<n_flit; i++) {
            mem_fetch *mf = queue->pop();
            if (mf!=NULL) {
                //printf("QQ:pop  %p %8u\n", mf, mf->get_request_uid());
                if (mf->get_type()==READ_REQUEST) { // no decompression
                    unsigned dst_id = get_dst_id(mf);
                    if (m_complete_list[dst_id].size()>=1000) {
                        assert(0);
                    }
                    m_complete_list[dst_id].push(mf);
                } else {
                    assert(mf->get_type()==WRITE_REQUEST);  // decompression
                    unsigned dst_id = get_dst_id(mf);
                    if (m_complete_list[dst_id].size()>=1000) {
                        assert(0);
                    }
                    m_complete_list[dst_id].push(mf);
                }
            }
        }
    }
};

class compressed_unpacked_up_link : public compressed_oneway_link {
public:
    compressed_unpacked_up_link(const char* nm, unsigned latency, unsigned src_cnt, unsigned dst_cnt)
    : compressed_oneway_link(nm, latency, src_cnt, dst_cnt) {
        m_ready_compressed = new my_delay_queue2(nm, 4000, 1);
    }
    void step_link_push(unsigned n_flit) {
        unsigned n_sent_flit_cnt = 0;

        // Priorities
        // 1. Read data
        // 2. Write acknowledge if no read data

        // 1. Read data
        while (n_sent_flit_cnt<n_flit) {
            pair<mem_fetch *, unsigned> it = m_ready_compressed->top();
            if (it.first!=NULL) {
                //printf("TOP1 @%08d %p %d\n", gpu_sim_cycle, it.first, it.first->get_request_uid());
                //it.first->print(stdout, false);
                assert(it.first->get_type()==READ_REPLY);
                if (m_cur_flit_cnt==0) {
                    assert(it.second%FLIT_SIZE==0);
                    m_packet_bit_size = HT_OVERHEAD+it.second;
                }

                bool is_complete = push(it.first, m_packet_bit_size, n_sent_flit_cnt, n_flit);
                if (is_complete) {
                    //printf("POP1 @%08d %p %d\n", gpu_sim_cycle, it.first, it.first->get_request_uid());
                    m_ready_compressed->pop();
                }
            } else {
                break;
            }
        }

        // 2. Write acknowledge if no read data
        for (unsigned i=0; (i<m_src_cnt) && (n_sent_flit_cnt<n_flit); i++) {
            unsigned src_id = (m_cur_src_id+i) % m_src_cnt;
            if (m_ready_short_list[src_id].size()>0) {
                mem_fetch *mf = m_ready_short_list[src_id].front();
                assert(mf->get_type()==WRITE_ACK);
                m_packet_bit_size = HT_OVERHEAD;
                bool is_complete = push(mf, m_packet_bit_size, n_sent_flit_cnt, n_flit);
                assert(is_complete);
                m_ready_short_list[src_id].pop();
            }
        }

        for (; n_sent_flit_cnt < n_flit; n_sent_flit_cnt++) {
            queue->push(false, false, NULL);
        }
        assert(n_sent_flit_cnt==n_flit);

        // Compress read data
        for (unsigned i=0; i<m_src_cnt; i++) {
            unsigned src_id = (m_cur_comp_id+i) % m_src_cnt;
            assert(m_ready_long_list[src_id].size()<=1);
            if (m_ready_long_list[src_id].size()>0) {
                unsigned char buffer[128];
                unsigned comp_bit_size;

                // compress
                mem_fetch *mf = m_ready_long_list[src_id].front();
                if (mf->get_data_size() == 128) {
                    g_the_gpu->get_global_memory()->read(mf->get_addr(), mf->get_data_size(), buffer);
                    comp_bit_size = g_comp->compress(mf->get_vstream_id(), buffer, mf->get_addr(), mf->get_data_size());
                    comp_bit_size = ((comp_bit_size+FLIT_SIZE-1)/FLIT_SIZE)*FLIT_SIZE;
                } else {
                    comp_bit_size = mf->get_data_size() * 8;
                }

                //printf("PUSH @%08d %p %d\n", gpu_sim_cycle, mf, mf->get_request_uid());
                m_ready_compressed->push(mf, comp_bit_size);
                m_ready_long_list[src_id].pop();
            }
        }
    }
};

class compressed_unpacked_memory_link : public memory_link {
public:
    compressed_unpacked_memory_link(const char* nm, unsigned int latency, const struct memory_config *config)
    : memory_link(nm, latency, config) {
        strcpy(m_nm, nm);

        char link_nm[256];
        sprintf(link_nm, "%s.dn", nm);
        m_dn = new compressed_unpacked_dn_link(link_nm, latency, config->m_n_mem * config->m_n_sub_partition_per_memory_channel, config->m_n_mem * config->m_n_sub_partition_per_memory_channel);
        sprintf(link_nm, "%s.up", nm);
        m_up = new compressed_unpacked_up_link(link_nm, latency, config->m_n_mem * config->m_n_sub_partition_per_memory_channel, config->m_n_mem * config->m_n_sub_partition_per_memory_channel);
    }
};

#endif
