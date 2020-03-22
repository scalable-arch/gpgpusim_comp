#include <stdio.h>
#include <limits.h>
#include "comp.h"

compressor *g_comp;

//------------------------------------------------------------------------------
//void virtual_stream::push(mblock& new_block)
//{
//    // shift blocks
//    for (int i=fifo_depth-2; i>=0; i--) {
//        fifo[i+1] = fifo[i];
//    }
//
//    // insert the new entry
//    fifo[0] = new_block;
//}

//------------------------------------------------------------------------------
virtual_stream_comp::virtual_stream_comp(int _fifo_depth)
    : compressor(), fifo_depth(_fifo_depth) {
    int buffer = fifo_depth;
    int log2 = 0;
    while (buffer>>=1) log2++;

    for (unsigned i=0; i<functionInfoVector.size(); i++) {
        patternInfoVector.push_back({&(functionInfoVector[i]), functionInfoVector[i].level * log2});
    }

    init();
}

virtual_stream_comp::~virtual_stream_comp() {
    for (auto it=vsmap.begin(); it!=vsmap.end(); ++it) {
        delete it->second;
    }
    vsmap.clear();
}

//void virtual_stream_comp::registerPatternInfo(string name, unsigned opcodeSize) {
//    for (auto it = functionInfoVector.begin();
//           it != functionInfoVector.end(); ++it) {
//        if (it->name.compare(name)==0) {
//            patternInfoVector.push_back({&(*it), opcodeSize});
//            return;
//        }
//    }
//    assert(0);
//}

void virtual_stream_comp::init() {
    std::sort(patternInfoVector.begin(), patternInfoVector.end());
}

unsigned virtual_stream_comp::compress(virtual_stream_id id, unsigned char *in, new_addr_type addr, size_t size) {
    assert(size==BYTES_PER_BLK);

    // 1. find an existing stream
    auto it = vsmap.find(id);
    if (it==vsmap.end()) {
        virtual_stream *new_stream = new virtual_stream(id, fifo_depth);
        vsmap.insert(std::pair<virtual_stream_id, virtual_stream *>(id, new_stream));
        it = vsmap.find(id);
    }
    assert(it!=vsmap.end());
    virtual_stream *vs = it->second;

    // 2. compress a block
    mblock new_block;
    mword new_word;

    unsigned block_length = 0;
    for (int i=0; i<WORDS_PER_BLK; i++) {
        new_word = 0ull;
        for (int j=0; j<8; j++) {
            new_word = (new_word<<8) | in[i*8+(7-j)];
        }
        new_block.words[i] = new_word;

        // measure Hamming Distance
        //measure_HD(&new_block, i);

        unsigned min_word_length = UINT_MAX;
        for (auto it = patternInfoVector.begin(); it!=patternInfoVector.end(); ++it) {
            bool result = it->fp(vs, &new_block, i);
            if (result) {
                if (it->size < min_word_length) {
                    min_word_length = it->size;
                    vs->prof_data->count_pattern(it->ID);
                    break;
                }
            }
        }

        if (min_word_length == UINT_MAX) {
            vs->prof_data->count_escape(new_word);
            min_word_length = 64+1;
        }
        block_length += min_word_length;
        vs->push(new_word);
        vs->prof_data->count_word();
    }

    return block_length;
}

void virtual_stream_comp::dump_profile(FILE *fd) {
    dump_pattern_info(fd);
    //dump_escape_info(fd);
    //g_profile_data.dump_HD(fd);
    fflush(fd);
}

void virtual_stream_comp::dump_pattern_info(FILE *fd) {
    unsigned long long total = 0ull;
    unsigned long long total_rd = 0ull;
    unsigned long long total_wr = 0ull;

    // count total
    for (auto it = vsmap.begin(); it != vsmap.end(); ++it) {
        total += it->second->prof_data->get_word_count();
        if ((it->first>>63) == 0) {
            total_rd += it->second->prof_data->get_word_count();
        } else {
            total_wr += it->second->prof_data->get_word_count();
        }
    }

    unsigned long long accum_pattern_count;
    unsigned long long accum_word_size;
    unsigned long long this_pattern_count;
    // print pattern info
    //----------------------------------------------------------------------------------------
    fprintf(fd, "TotalRD\t\t\t\t%16lld\n", total_rd);
    accum_pattern_count = 0ull;
    accum_word_size = 0ull;
    for (auto it = patternInfoVector.begin(); it!=patternInfoVector.end(); ++it) {
        this_pattern_count = 0ull;
        for (auto it2 = vsmap.begin(); it2 != vsmap.end(); ++it2) {
            if ((it2->first>>63) == 0) {
                this_pattern_count += it2->second->prof_data->get_pattern_count(it->ID);
            }
        }
        accum_pattern_count += this_pattern_count;
        accum_word_size += it->size * this_pattern_count;
        fprintf(fd, "%s\t%d\t%16lld\t%8.6f\t%8.6f\n", it->name.c_str(), it->size, this_pattern_count, this_pattern_count*100./total_rd, accum_pattern_count*100./total_rd);
    }
    this_pattern_count = 0ull;
    for (auto it2 = vsmap.begin(); it2 != vsmap.end(); ++it2) {
        if ((it2->first>>63) == 0) {
            this_pattern_count += it2->second->prof_data->get_escape_count();
        }
    }
    fprintf(fd, "%s\t%d\t%16lld\t%8.6f\t%8.6f\n", "ESC", 64, this_pattern_count, this_pattern_count*100./total_rd, 100 - accum_pattern_count*100./total_rd);
    accum_word_size += 64 * this_pattern_count;
    fprintf(fd, "Average word\t\t\t\t%8.6f\n", accum_word_size*1./total_rd);

    //----------------------------------------------------------------------------------------
    fprintf(fd, "TotalWR\t\t\t\t%16lld\n", total_wr);
    accum_pattern_count = 0ull;
    accum_word_size = 0ull;
    for (auto it = patternInfoVector.begin(); it!=patternInfoVector.end(); ++it) {
        this_pattern_count = 0ull;
        for (auto it2 = vsmap.begin(); it2 != vsmap.end(); ++it2) {
            if ((it2->first>>63) == 1) {
                this_pattern_count += it2->second->prof_data->get_pattern_count(it->ID);
            }
        }
        accum_pattern_count += this_pattern_count;
        accum_word_size += it->size * this_pattern_count;
        fprintf(fd, "%s\t%d\t%16lld\t%8.6f\t%8.6f\n", it->name.c_str(), it->size, this_pattern_count, this_pattern_count*100./total_wr, accum_pattern_count*100./total_wr);
    }
    this_pattern_count = 0ull;
    for (auto it2 = vsmap.begin(); it2 != vsmap.end(); ++it2) {
        if ((it2->first>>63) == 1) {
            this_pattern_count += it2->second->prof_data->get_escape_count();
        }
    }
    fprintf(fd, "%s\t%d\t%16lld\t%8.6f\t%8.6f\n", "ESC", 64, this_pattern_count, this_pattern_count*100./total_wr, 100 - accum_pattern_count*100./total_wr);
    accum_word_size += 64 * this_pattern_count;
    fprintf(fd, "Average word\t\t\t\t%8.6f\n", accum_word_size*1./total_wr);

    //----------------------------------------------------------------------------------------
    fprintf(fd, "Total\t\t\t\t%16lld\n", total);
    accum_pattern_count = 0ull;
    accum_word_size = 0ull;
    for (auto it = patternInfoVector.begin(); it!=patternInfoVector.end(); ++it) {
        this_pattern_count = 0ull;
        for (auto it2 = vsmap.begin(); it2 != vsmap.end(); ++it2) {
            this_pattern_count += it2->second->prof_data->get_pattern_count(it->ID);
        }
        accum_pattern_count += this_pattern_count;
        accum_word_size += it->size * this_pattern_count;
        fprintf(fd, "%s\t%d\t%16lld\t%8.6f\t%8.6f\n", it->name.c_str(), it->size, this_pattern_count, this_pattern_count*100./total, accum_pattern_count*100./total);
    }
    this_pattern_count = 0ull;
    for (auto it2 = vsmap.begin(); it2 != vsmap.end(); ++it2) {
        this_pattern_count += it2->second->prof_data->get_escape_count();
    }
    fprintf(fd, "%s\t%d\t%16lld\t%8.6f\t%8.6f\n", "ESC", 64, this_pattern_count, this_pattern_count*100./total, 100 - accum_pattern_count*100./total);
    accum_word_size += 64 * this_pattern_count;
    fprintf(fd, "Average word\t\t\t\t%8.6f\n", accum_word_size*1./total);

    //----------------------------------------------------------------------------------------
    fprintf(fd, "Total\t(new)\t\t\t%16lld\n", total);
    accum_pattern_count = 0ull;
    accum_word_size = 0ull;
    for (unsigned i=0; i<patternInfoVector.size(); i++) {
        vector<PatternInfo>::iterator it;
        for (it = patternInfoVector.begin(); it!=patternInfoVector.end(); ++it) {
            if (it->ID==i) {
                break;
            }
        }
        assert(it->ID==i);
        this_pattern_count = 0ull;
        for (auto it2 = vsmap.begin(); it2 != vsmap.end(); ++it2) {
            this_pattern_count += it2->second->prof_data->get_pattern_count(it->ID);
        }
        accum_pattern_count += this_pattern_count;
        accum_word_size += it->size * this_pattern_count;
        fprintf(fd, "%s\t%d\t%16lld\t%8.6f\t%8.6f\n", it->name.c_str(), it->size, this_pattern_count, this_pattern_count*100./total, accum_pattern_count*100./total);
    }
    this_pattern_count = 0ull;
    for (auto it2 = vsmap.begin(); it2 != vsmap.end(); ++it2) {
        this_pattern_count += it2->second->prof_data->get_escape_count();
    }
    fprintf(fd, "%s\t%d\t%16lld\t%8.6f\t%8.6f\n", "ESC", 64, this_pattern_count, this_pattern_count*100./total, 100 - accum_pattern_count*100./total);
    accum_word_size += 64 * this_pattern_count;
    fprintf(fd, "Average word\t\t\t\t%8.6f\n", accum_word_size*1./total);

}

void virtual_stream_comp::dump_escape_info(FILE *fd) {
    map<unsigned long long, unsigned long long> map_total;
    // 1. merge escape maps
    for (auto it = vsmap.begin(); it != vsmap.end(); ++it) {
        profile_data *prof_data = it->second->prof_data;
        for (auto it2 = prof_data->begin(); it2 != prof_data->end(); ++it2) {
            auto it3 = map_total.find(it2->first);
            if (it3==map_total.end()) {
                map_total.insert(pair<unsigned long long, unsigned long long>(it2->first, it2->second));
            } else {
                it3->second += it2->second;
            }
        }
    }

    // 2. unscreened patterns
    // 2.1. clone
    vector<pair<unsigned long long, unsigned long long>> sort_vector;
    auto pos = sort_vector.begin();
    for (auto it=map_total.begin(); it!=map_total.end(); ++it) {
        pos = sort_vector.insert(pos, pair<unsigned long long, unsigned long long>(it->second, it->first));
    }
    // 2.2. sort
    sort(sort_vector.begin(), sort_vector.end());

    // print raw data sorted by frequency
    for (auto it=sort_vector.begin(); it!=sort_vector.end(); ++it) {
        if (it->first>2) {
            fprintf(fd, "%016llx\t%16lld\n", it->second, it->first);
        }
    }
}

bool sign_extended(UINT64 value, UINT8 bit_size) {
    UINT64 max = (1ULL << (bit_size-1)) - 1;    // bit_size: 4 -> ...00000111
    UINT64 min = ~max;                          // bit_size: 4 -> ...11111000
    return (value <= max) | (value >= min);
}
