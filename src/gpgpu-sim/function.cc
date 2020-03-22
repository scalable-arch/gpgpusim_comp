#include "function.h"
#include "comp.h"

//------------------------------------------------------------------------------
inline bool word_all_zero(mword word) {
    return word==0ull;
}

inline bool word_all_one(mword word) {
    return word==0xFFFFFFFFFFFFFFFFull;
}

inline bool word_repeated_byte(mword word) {
    unsigned long long byte = word& 0xFFull;
    if (byte!=((word>> 8)&0xFFull)) return false;
    if (byte!=((word>>16)&0xFFull)) return false;
    if (byte!=((word>>24)&0xFFull)) return false;
    if (byte!=((word>>32)&0xFFull)) return false;
    if (byte!=((word>>40)&0xFFull)) return false;
    if (byte!=((word>>48)&0xFFull)) return false;
    if (byte!=((word>>56)&0xFFull)) return false;
    return true;
}

inline bool word_one_hot(mword word) {
    unsigned one_count = 0;
    for (unsigned i=0; i<64; i++) {
        if ((word>>i)&0x1) {
            one_count++;
        }
    }
    return (one_count==1);
}

inline bool word_sign_extended(mword word, unsigned upper, unsigned lower) {
    if (   (((word>>lower)&((1ull<<(32-lower))-1)) != 0ull)
        && (((word>>lower)&((1ull<<(32-lower))-1)) != ((1ull<<(32-lower))-1)) ) {
        return false;
    }
    if (   (((word>>(upper+32))&((1ull<<(32-upper))-1)) != 0ull)
        && (((word>>(upper+32))&((1ull<<(32-upper))-1)) != ((1ull<<(32-upper))-1)) ) {
        return false;
    }
    return true;
}

inline bool word_sign_extended_3_3(mword word) { return word_sign_extended(word, 3, 3); }
inline bool word_sign_extended_7_7(mword word) { return word_sign_extended(word, 7, 7); }
inline bool word_sign_extended_15_15(mword word) { return word_sign_extended(word, 15, 15); }
inline bool word_sign_extended_0_31(mword word) { return word_sign_extended(word, 0, 31); }
inline bool word_sign_extended_31_0(mword word) { return word_sign_extended(word, 31, 0); }

//------------------------------------------------------------------------------
bool B00_all_zero(virtual_stream *stream, mblock *block, int idx) {
    return word_all_zero(block->words[idx]);        
}

bool B01_all_one(virtual_stream *stream, mblock *block, int idx) {
    return word_all_one(block->words[idx]);        
}

bool B02_repeated_byte(virtual_stream *stream, mblock *block, int idx) {
    return word_repeated_byte(block->words[idx]);        
}

bool B03_one_hot(virtual_stream *stream, mblock *block, int idx) {
    return word_one_hot(block->words[idx]);        
}

bool B04_SE_3_3(virtual_stream *stream, mblock *block, int idx) {
    return word_sign_extended_3_3(block->words[idx]);
}

bool B05_SE_7_7(virtual_stream *stream, mblock *block, int idx) {
    return word_sign_extended_7_7(block->words[idx]);
}

bool B06_SE_15_15(virtual_stream *stream, mblock *block, int idx) {
    return word_sign_extended_15_15(block->words[idx]);
}

bool B07_SE_0_31(virtual_stream *stream, mblock *block, int idx) {
    return word_sign_extended_0_31(block->words[idx]);
}

bool B08_SE_31_0(virtual_stream *stream, mblock *block, int idx) {
    return word_sign_extended_31_0(block->words[idx]);
}

//------------------------------------------------------------------------------
inline bool XOR(virtual_stream *stream, mblock *block, int idx, WordFunctionPointer fp) {
    unsigned long long new_word = block->words[idx];

    for (auto it = stream->begin(); it != stream->end(); ++it) {
        unsigned long long word_xor = new_word ^ (*it);
        if (fp(word_xor)) {
            return true;
        }
    }
    return false;
}

inline bool XORXOR(virtual_stream *stream, mblock *block, int idx, WordFunctionPointer fp) {
    unsigned long long new_word = block->words[idx];

    for (auto it = stream->begin(); it != stream->end(); ++it) {
        for (auto it2 = stream->begin(); it2 != it; ++it2) {
            unsigned long long word_xor = new_word ^ (*it) ^ (*it2);
            if (fp(word_xor)) {
                return true;
            }
        }
    }
    return false;
}

inline bool XORXORXOR(virtual_stream *stream, mblock *block, int idx, WordFunctionPointer fp) {
    unsigned long long new_word = block->words[idx];

    for (auto it = stream->begin(); it != stream->end(); ++it) {
        for (auto it2 = stream->begin(); it2 != it; ++it2) {
            for (auto it3 = stream->begin(); it3 != it2; ++it3) {
                unsigned long long word_xor = new_word ^ (*it) ^ (*it2) ^ (*it3);
                if (fp(word_xor)) {
                    return true;
                }
            }
        }
    }
    return false;
}

//------------------------------------------------------------------------------
bool I00_all_zero(virtual_stream *stream, mblock *block, int idx) {
    return XOR(stream, block, idx, word_all_zero);
}

bool I01_all_one(virtual_stream *stream, mblock *block, int idx) {
    return XOR(stream, block, idx, word_all_one);
}

bool I02_repeated_byte(virtual_stream *stream, mblock *block, int idx) {
    return XOR(stream, block, idx, word_repeated_byte);
}

bool I03_one_hot(virtual_stream *stream, mblock *block, int idx) {
    return XOR(stream, block, idx, word_one_hot);
}

bool I04_SE_3_3(virtual_stream *stream, mblock *block, int idx) {
    return XOR(stream, block, idx, word_sign_extended_3_3);
}

bool I05_SE_7_7(virtual_stream *stream, mblock *block, int idx) {
    return XOR(stream, block, idx, word_sign_extended_7_7);
}

bool I06_SE_15_15(virtual_stream *stream, mblock *block, int idx) {
    return XOR(stream, block, idx, word_sign_extended_15_15);
}

bool I07_SE_0_31(virtual_stream *stream, mblock *block, int idx) {
    return XOR(stream, block, idx, word_sign_extended_0_31);
}

bool I08_SE_31_0(virtual_stream *stream, mblock *block, int idx) {
    return XOR(stream, block, idx, word_sign_extended_31_0);
}

//------------------------------------------------------------------------------
bool J00_all_zero(virtual_stream *stream, mblock *block, int idx) {
    return XORXOR(stream, block, idx, word_all_zero);
}

bool J01_all_one(virtual_stream *stream, mblock *block, int idx) {
    return XORXOR(stream, block, idx, word_all_one);
}

bool J02_repeated_byte(virtual_stream *stream, mblock *block, int idx) {
    return XORXOR(stream, block, idx, word_repeated_byte);
}

bool J03_one_hot(virtual_stream *stream, mblock *block, int idx) {
    return XORXOR(stream, block, idx, word_one_hot);
}

bool J04_SE_3_3(virtual_stream *stream, mblock *block, int idx) {
    return XORXOR(stream, block, idx, word_sign_extended_3_3);
}

bool J05_SE_7_7(virtual_stream *stream, mblock *block, int idx) {
    return XORXOR(stream, block, idx, word_sign_extended_7_7);
}

bool J06_SE_15_15(virtual_stream *stream, mblock *block, int idx) {
    return XORXOR(stream, block, idx, word_sign_extended_15_15);
}

bool J07_SE_0_31(virtual_stream *stream, mblock *block, int idx) {
    return XORXOR(stream, block, idx, word_sign_extended_0_31);
}

bool J08_SE_31_0(virtual_stream *stream, mblock *block, int idx) {
    return XORXOR(stream, block, idx, word_sign_extended_31_0);
}

//------------------------------------------------------------------------------
bool K00_all_zero(virtual_stream *stream, mblock *block, int idx) {
    return XORXORXOR(stream, block, idx, word_all_zero);
}

bool K01_all_one(virtual_stream *stream, mblock *block, int idx) {
    return XORXORXOR(stream, block, idx, word_all_one);
}

bool K02_repeated_byte(virtual_stream *stream, mblock *block, int idx) {
    return XORXORXOR(stream, block, idx, word_repeated_byte);
}

bool K03_one_hot(virtual_stream *stream, mblock *block, int idx) {
    return XORXORXOR(stream, block, idx, word_one_hot);
}

bool K04_SE_3_3(virtual_stream *stream, mblock *block, int idx) {
    return XORXORXOR(stream, block, idx, word_sign_extended_3_3);
}

bool K05_SE_7_7(virtual_stream *stream, mblock *block, int idx) {
    return XORXORXOR(stream, block, idx, word_sign_extended_7_7);
}

bool K06_SE_15_15(virtual_stream *stream, mblock *block, int idx) {
    return XORXORXOR(stream, block, idx, word_sign_extended_15_15);
}

bool K07_SE_0_31(virtual_stream *stream, mblock *block, int idx) {
    return XORXORXOR(stream, block, idx, word_sign_extended_0_31);
}

bool K08_SE_31_0(virtual_stream *stream, mblock *block, int idx) {
    return XORXORXOR(stream, block, idx, word_sign_extended_31_0);
}

bool NCP_nocompression(virtual_stream *stream, mblock *block, int idx) {
    // 1.1. within current block (previous words)
    unsigned long long new_word = block->words[idx];

    printf("%016llx: ", new_word);
    for (auto it = stream->begin(); it != stream->end(); ++it) {
        printf("%016llx ", (*it));
    }
    return true;
}

//------------------------------------------------------------------------------
vector<FunctionInfo> functionInfoVector = {
    {0,   "B00",    0,  0,  &B00_all_zero},
    {1,   "B01",    0,  0,  &B01_all_one},
    {2,   "B02",    0,  8,  &B02_repeated_byte},
    {3,   "B03",    0,  8,  &B03_one_hot},
    {4,   "B04",    0,  8,  &B04_SE_3_3},
    {5,   "B05",    0,  16, &B05_SE_7_7},
    {6,   "B06",    0,  32, &B06_SE_15_15},
    {7,   "B07",    0,  32, &B07_SE_0_31},
    {8,   "B08",    0,  32, &B08_SE_31_0},

    {9,   "I00",    1,  0,  &I00_all_zero},
    {10,  "I01",    1,  0,  &I01_all_one},
    {11,  "I02",    1,  8,  &I02_repeated_byte},
    {12,  "I03",    1,  8,  &I03_one_hot},
    {13,  "I04",    1,  8,  &I04_SE_3_3},
    {14,  "I05",    1,  16, &I05_SE_7_7},
    {15,  "I06",    1,  32, &I06_SE_15_15},
    {16,  "I07",    1,  32, &I07_SE_0_31},
    {17,  "I08",    1,  32, &I08_SE_31_0},

    {18,  "J00",    2,  0,  &J00_all_zero},
    {19,  "J01",    2,  0,  &J01_all_one},
    {20,  "J02",    2,  8,  &J02_repeated_byte},
    {21,  "J03",    2,  8,  &J03_one_hot},
    {22,  "J04",    2,  8,  &J04_SE_3_3},
    {23,  "J05",    2,  16, &J05_SE_7_7},
    {24,  "J06",    2,  32, &J06_SE_15_15},
    {25,  "J07",    2,  32, &J07_SE_0_31},
    {26,  "J08",    2,  32, &J08_SE_31_0},

    {27,  "K00",    3,  0,  &K00_all_zero},
    {28,  "K01",    3,  0,  &K01_all_one},
    {29,  "K02",    3,  8,  &K02_repeated_byte},
    {30,  "K03",    3,  8,  &K03_one_hot},
    {31,  "K04",    3,  8,  &K04_SE_3_3},
    {32,  "K05",    3,  16, &K05_SE_7_7},
    {33,  "K06",    3,  32, &K06_SE_15_15},
    {34,  "K07",    3,  32, &K07_SE_0_31},
    {35,  "K08",    3,  32, &K08_SE_31_0}

    //{36,  "NCP",    0,  64, &NCP_nocompression}
};

//------------------------------------------------------------------------------
