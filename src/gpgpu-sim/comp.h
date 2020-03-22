#ifndef __COMP_H__
#define __COMP_H__

#include <string.h>
#include <map>
#include <unordered_map>
#include <list>
#include <list>
#include <vector>
#include <algorithm>
#include <assert.h>

#include "../abstract_hardware_model.h"
#include "function.h"
#include "common.hh"

//------------------------------------------------------------------------------
typedef unsigned long long mword;
typedef unsigned long long virtual_stream_id;

#define VSC_FIFO_DEPTH  32
#define WORDS_PER_BLK   16
#define BYTES_PER_BLK   (WORDS_PER_BLK*8)

//------------------------------------------------------------------------------
using namespace std;

class virtual_stream_comp;


bool sign_extended(UINT64 value, UINT8 bit_size);

//------------------------------------------------------------------------------
class profile_data: public map<unsigned long long, unsigned long long> {
public:
    profile_data() {
        word_count = 0ull;
        escape_count = 0ull;
    }
public:
    unsigned long long get_word_count() { return word_count; }
    unsigned long long get_pattern_count(unsigned ID) {
        auto it = pattern_map.find(ID);
        if (it==pattern_map.end()) {
            return 0ull;
        } else {
            return it->second;
        }
    }
    unsigned long long get_escape_count() { return escape_count; }

    void count_word() {
        word_count++;
    }
    void count_pattern(unsigned ID) {
        auto it = pattern_map.find(ID);
        if (it==pattern_map.end()) {
            pattern_map.insert(pair<unsigned long long, unsigned long long>(ID, 1ull));
        } else {
            it->second++;
        }
    }
    void count_escape(mword value) {
        escape_count++;
        auto it = find(value);
        if (it==end()) {
            insert(pair<unsigned long long, unsigned long long>(value, 1ull));
        } else {
            it->second++;
        }
    }

    void dump_escapes(FILE *fd) {
        fprintf(fd, "Escape  \t%16lld\t%8.6f\n", escape_count, escape_count*100./word_count);

        // 1. print processed data
        // 1.1. clone
        map<unsigned long long, unsigned long long> tmp2;
        for (auto it=begin(); it!=end(); ++it) {
            tmp2.insert(pair<unsigned long long, unsigned long long>(it->first, it->second));
        }

        // 2. unscreened patterns
        // 2.1. clone
        vector<pair<unsigned long long, unsigned long long>> sort_vector;
        auto pos = sort_vector.begin();
        for (auto it=tmp2.begin(); it!=tmp2.end(); ++it) {
            pos = sort_vector.insert(pos, pair<unsigned long long, unsigned long long>(it->second, it->first));
        }
        // 2.2. sort
        sort(sort_vector.begin(), sort_vector.end());

        // print raw data sorted by frequency
        for (auto it=sort_vector.begin(); it!=sort_vector.end(); ++it) {
            if (it->first>2) {
                fprintf(fd, "%016llx\t%16lld\t%8.6f\n", it->second, it->first, (it->first*100.)/word_count);
            }
        }
    }

public:
    unsigned long long word_count;
    unsigned long long escape_count;

    map<unsigned, unsigned long long> pattern_map;
    map<unsigned long long, unsigned long long> escape_map;
};

//------------------------------------------------------------------------------
class mblock {
public:
    mblock() {
        for (int i=0; i<16; i++) {
            words[i] = 0ull;
        }
    }
    mblock(class mblock& src) {
        for (int i=0; i<16; i++) {
            words[i] = src.words[i];
        }
    }
public:
    mword words[16];        // 128-byte -> 16 x 64-bit
};

//------------------------------------------------------------------------------
class virtual_stream : public list<mword> {
public:
    virtual_stream(virtual_stream_id _id, int _fifo_depth)
    : id(_id), fifo_depth(_fifo_depth) {
        prof_data = new profile_data();

        for (int i=0; i<fifo_depth-2; i++) {
            push_back(0ull);
        }
        if (fifo_depth > 1) {
            push_back(0xFFFFFFFFFFFFFFFFull);
        }
        push_back(0ull);
    }
    ~virtual_stream() {
        //delete fifo;
        delete prof_data;
    }

public:
    void push(mword new_entry) { pop_front(); push_back(new_entry); }
    //void push(mblock& new_entry);
    //unsigned measure_HD(mblock *block, int idx);

public:
    virtual_stream_id id;
    int fifo_depth;
    profile_data *prof_data;
    //mblock *fifo;
};

//------------------------------------------------------------------------------
class compressor {
public:
    compressor() {}

    virtual unsigned compress(virtual_stream_id id, unsigned char *in, new_addr_type addr, size_t size) { return 0; }
    virtual void dump_profile(FILE *fd) {}
};

class virtual_stream_comp : public compressor {
public:
    virtual_stream_comp(int _fifo_depth);
    ~virtual_stream_comp();
public:
    void init();
    //void registerPatternInfo(string name, unsigned opcodeSize);
    unsigned compress(virtual_stream_id id, unsigned char *in, new_addr_type addr, size_t size);
    void dump_profile(FILE *fd);
    void dump_pattern_info(FILE *fd);
    void dump_escape_info(FILE *fd);
public:
    vector<PatternInfo> patternInfoVector;
    int fifo_depth;
    unordered_map<virtual_stream_id, virtual_stream *> vsmap;
};

class dump_stream_comp: public compressor {
public:
    dump_stream_comp() {}
    ~dump_stream_comp() {
        for (auto it = fdmap.begin(); it!=fdmap.end(); ++it) {
            fclose(it->second);
        }
    }

public:
    unsigned compress(virtual_stream_id id, unsigned char *in, new_addr_type addr, size_t size) {
        // 1. find an existing FD
        auto it = fdmap.find(id);
        if (it==fdmap.end()) {
            char buf[256];
            sprintf(buf, "stream.%020llx", id);
            FILE *new_fd = fopen(buf, "wb");
            assert(new_fd!=NULL);
            fdmap.insert(std::pair<virtual_stream_id, FILE *>(id, new_fd));
            it = fdmap.find(id);
        }
        assert(it!=fdmap.end());
        FILE *fd = it->second;

        fwrite(&addr, sizeof(new_addr_type), 1, fd);
        fwrite(in, sizeof(char), size, fd);
    }
private:
    unordered_map<virtual_stream_id, FILE *> fdmap;
};

class BPSCompressor : public compressor {
public:
    BPSCompressor() {
        prev_data = 0;
        total_line_cnt = 0ull;
        for (unsigned i=0; i<1025; i++) {
            comp_line_size_cnt[i] = 0ull;
        }
    }

    unsigned compress(virtual_stream_id id, unsigned char*in, new_addr_type addr, size_t size) {
        assert(size==128);
        // copy
        CACHELINE_DATA raw_buffer;
        for (unsigned i=0; i<_MAX_BYTES_PER_LINE; i++) {
            raw_buffer.byte[i] = in[i];
        } 
        // delta
        CACHELINE_DATA diff_buffer;
        for (unsigned i=0; i<_MAX_DWORDS_PER_LINE; i++) {
            diff_buffer.dword[i] = (raw_buffer.dword[i] - prev_data);
            prev_data = raw_buffer.dword[i];
        }

        // BP, BPX
        CACHELINE_DATA bp_buffer;
        CACHELINE_DATA bpx_buffer;
        for (int j=31; j>=0; j--) {
            INT32 bufBP = 0;
            INT32 bufBPX = 0;
            for (unsigned i=0; i<_MAX_DWORDS_PER_LINE; i++) {
                bufBP <<= 1;
                bufBPX <<= 1;
                bufBP |= ((diff_buffer.dword[i]>>j)&1);
                if (j==31) {
                    bufBPX |= ((diff_buffer.dword[i]>>j)&1);
                } else {
                    bufBPX |= (((diff_buffer.dword[i]>>j)^(diff_buffer.dword[i]>>(j+1)))&1);
                }
            }
            bp_buffer.dword[j] = bufBP;
            bpx_buffer.dword[j] = bufBPX;
        }

        unsigned length = 0;
        unsigned run_length = 0;
        // zero run (1)         : 000        (3-bit)
        // zero run (2~32)      : 001bbbbb   (8-bit)
        // 1-one                : 010ccccc   (8-bit)    // 32+a
        // first one            : 0110       (4-bit)
        // consecutive 2-ones   : 01110ddddd (10-bit)   // 64+a
        // BP zero              : 01111      (5-bit)
        // Others               : 1zzzzzzzz  (33-bit)   // 96
        for (int i=_MAX_DWORDS_PER_LINE-1; i>=0; i--) {
            // zero sequence: 0, 1~48
            // non-zero sequence: 1, 1~16
            if (bpx_buffer.dword[i]==0) {
                run_length++;
            }
            else {
                if (run_length>0) {
                    assert(run_length!=32);
                    if (run_length ==1) {
                        length += 3;
                    } else {
                        length += 8;
                    }
                }
                run_length = 0;

                int oneCnt = 0;
                int firstPos = -1;
                for (int j=0; j<32; j++) {
                    if ((bpx_buffer.dword[i]>>j)&1) {
                        if (firstPos==-1) {
                            firstPos = j;
                        }
                        oneCnt++;
                    }
                }

                if (bp_buffer.dword[i]==0) {
                    length += 5;
                } else if (oneCnt==1) {
                    if (firstPos==31) {
                        length += 4;
                    } else {
                        length += 8;
                    }
                } else {
                    length += 33;
                }
            }
        }
        if (run_length>0) {
            if (run_length <=1) {
                length += 3;
            } else {
                assert(run_length<=32);
                length += 8;
            }
        }

        total_line_cnt++;
        if (length>1024) {
            comp_line_size_cnt[1024]++;
        } else {
            comp_line_size_cnt[length]++;
        }
        return length;
    }
    void dump_profile(FILE *fd) {
        for (unsigned i=0; i<1025; i++) {
            if (comp_line_size_cnt[i]!=0) {
                printf("%4d\t%f\n", i, comp_line_size_cnt[i]*1./total_line_cnt);
            }
        }
    }
protected:
    INT32 prev_data;
    int run_length;
    unsigned long long total_line_cnt;
    unsigned long long comp_line_size_cnt[1025];
};

class BPCompressor : public compressor {
public:
    BPCompressor() {}
    unsigned compress(virtual_stream_id id, unsigned char*in, new_addr_type addr, size_t size) {
        assert(size==128);
        // copy
        CACHELINE_DATA raw_buffer;
        for (unsigned i=0; i<_MAX_BYTES_PER_LINE; i++) {
            raw_buffer.byte[i] = in[i];
        } 

        INT64 deltas[31];
        for (unsigned i=1; i<_MAX_DWORDS_PER_LINE; i++) {
            deltas[i-1] = ((INT64) raw_buffer.s_dword[i]) - ((INT64) raw_buffer.s_dword[i-1]);
        }

        INT32 prevDBP = 0;
        INT32 DBP[33];
        INT32 DBX[33];
        for (int j=63; j>=0; j--) {
            INT32 buf = 0;
            for (int i=30; i>=0; i--) {
                buf <<= 1;
                buf |= ((deltas[i]>>j)&1);
            }
            if (j==63) {
                DBP[32] = buf;
                DBX[32] = buf;
                prevDBP = buf;
            } else if (j<32) {
                DBP[j] = buf;
                DBX[j] = buf^prevDBP;
                prevDBP = buf;
            } else {
                assert(buf==prevDBP);
                prevDBP = buf;
            }
        }
        
        // first 32-bit word in original form
        unsigned blkLength = encodeFirst(raw_buffer.dword[0]);
        blkLength += encodeDeltas(DBP, DBX);

        return blkLength;
    }

    unsigned encodeFirst(INT32 sym) {
        if (sym==0) {
            return 3;
        } else if (sign_extended(sym, 4)) {
            return (3+4);
        } else if (sign_extended(sym, 8)) {
            return (3+8);
        } else if (sign_extended(sym, 16)) {
            return (3+16);
        } else {
            return (1+32);
        }
    }
    unsigned encodeDeltas(INT32* DBP, INT32* DBX) {
        static const unsigned ZRL_CODE_SIZE[34] = {0, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
        static const unsigned singleOneSize = 10;
        static const unsigned consecutiveDoubleOneSize = 10;
        static const unsigned allOneSize = 5;
        static const unsigned zeroDBPSize = 5;
        // 1        -> uncompressed
        // 01       -> Z-RLE: 2~33
        // 001      -> Z-RLE: 1
        // 00000    -> single 1
        // 00001    -> consecutive two 1´s
        // 00010    -> zero DBP
        // 00011    -> All 1´s

        unsigned length = 0;
        unsigned run_length = 0;
        for (int i=32; i>=0; i--) {
            if (DBX[i]==0) {
                run_length++;
            }
            else {
                if (run_length>0) {
                    assert(run_length!=33);
                    length += ZRL_CODE_SIZE[run_length];
                }
                run_length = 0;

                if (DBP[i]==0) {
                    length += zeroDBPSize;
                } else if (DBX[i]==0x7fffffff) {
                    length += allOneSize;
                } else {
                    int oneCnt = 0;
                    for (int j=0; j<32; j++) {
                        if ((DBX[i]>>j)&1) {
                            oneCnt++;
                        }
                    }
                    unsigned two_distance = 0;
                    int firstPos = -1;
                    if (oneCnt<=2) {
                        for (int j=0; j<32; j++) {
                            if ((DBX[i]>>j)&1) {
                                if (firstPos==-1) {
                                    firstPos = j;
                                } else {
                                    two_distance = j - firstPos;
                                }
                            }
                        }
                    }
                    if (oneCnt==1) {
                        length += singleOneSize;
                    } else if ((oneCnt==2) && (two_distance==1)) {
                        length += consecutiveDoubleOneSize;
                    } else {
                        length += 32;
                    }
                }
            }
        }
        if (run_length>0) {
            length += ZRL_CODE_SIZE[run_length];
            assert(run_length<=33);
        }
        return length;
    }
};

class CPackCompressor: public compressor {
public:
    CPackCompressor() {}

    unsigned compress(virtual_stream_id id, unsigned char*in, new_addr_type addr, size_t size) {
        assert(size==128);
        // copy
        CACHELINE_DATA raw_buffer;
        for (unsigned i=0; i<_MAX_BYTES_PER_LINE; i++) {
            raw_buffer.byte[i] = in[i];
        } 

        LENGTH blkLength = 0;

        int wrPtr = 0;
        for (int i=0; i<16; i++) {
            dictionary[i] = 0;
        }

        for (UINT32 i=0; i<_MAX_DWORDS_PER_LINE; i++) {
            // code 00: zzzz
            if (raw_buffer.dword[i]==0) {
                blkLength+=2;
            }
            else {
                bool matchedFull = false;
                bool matched3B = false;
                bool matched2B = false;
                for (int j=0; j<16; j++) {
                    if (raw_buffer.dword[i]==dictionary[j]) {
                        matchedFull = true;
                    }
                    if ((raw_buffer.dword[i]&0xFFFFFF00)==(dictionary[j]&0xFFFFFF00)) {
                        matched3B = true;
                    }
                    if ((raw_buffer.dword[i]&0xFFFF0000)==(dictionary[j]&0xFFFF0000)) {
                        matched2B = true;
                    }
                }

                // code 10: mmmm
                if (matchedFull) {
                    blkLength+=6;
                }
                // code 1101: zzzx  -> 1101+8-bit
                else if ((raw_buffer.byte[i*4+3]==0)&&(raw_buffer.byte[i*4+2]==0)&&(raw_buffer.byte[i*4+1]==0)) {
                    blkLength+=12;
                }
                // code 1110: mmmx  -> 1110+4-bit+8-bit
                else if (matched3B) {
                    blkLength+=16;
                }
                // code 1100: mmxx  -> 1100+4-bit+8-bitx2
                else if (matched2B) {
                    blkLength+=24;
                }
                // code: 01: xxxx -> 34 bit
                else {
                    blkLength+=34;
                    dictionary[wrPtr] = raw_buffer.dword[i];
                    wrPtr = (wrPtr+1)%16;
                }
            }
        }

        return blkLength;
    }

protected:
    UINT32 dictionary[16];
};

extern compressor *g_comp;

#endif /* __COMP_H__*/
