#ifndef __COMMON_HH__
#define __COMMON_HH__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <iostream>
#include <sstream>

//--------------------------------------------------------------------
//#define LSIZE (512)  // in bits
#define LSIZE (1024)  // in bits

//--------------------------------------------------------------------
using namespace std;

typedef uint8_t             UINT8;
typedef uint16_t            UINT16;
typedef uint32_t            UINT32;
typedef uint64_t            UINT64;
typedef int8_t              INT8;
typedef int16_t             INT16;
typedef int32_t             INT32;
typedef int64_t             INT64;
typedef float               FLT32;
typedef double              FLT64;

typedef unsigned long long  CNT;
typedef unsigned            LENGTH;
typedef INT64               KEY;

static const UINT32 _MAX_BYTES_PER_LINE     = LSIZE/8;
static const UINT32 _MAX_WORDS_PER_LINE     = LSIZE/16;
static const UINT32 _MAX_DWORDS_PER_LINE    = LSIZE/32;
static const UINT32 _MAX_QWORDS_PER_LINE    = LSIZE/64;
static const UINT32 _MAX_FLOATS_PER_LINE    = LSIZE/32;
static const UINT32 _MAX_DOUBLES_PER_LINE   = LSIZE/64;

typedef struct { UINT64 m : 52; UINT64 e : 11; UINT64 s : 1; } FLT64_P;
typedef struct { UINT32 m : 23; UINT32 e : 8;  UINT32 s : 1; } FLT32_P;

//--------------------------------------------------------------------
typedef union CACHELINE_DATA {
    UINT8   byte[_MAX_BYTES_PER_LINE];
    UINT16  word[_MAX_WORDS_PER_LINE];
    UINT32  dword[_MAX_DWORDS_PER_LINE];
    UINT64  qword[_MAX_QWORDS_PER_LINE];

    INT8    s_byte[_MAX_BYTES_PER_LINE];
    INT16   s_word[_MAX_WORDS_PER_LINE];
    INT32   s_dword[_MAX_DWORDS_PER_LINE];
    INT64   s_qword[_MAX_QWORDS_PER_LINE];

    FLT32   flt[_MAX_FLOATS_PER_LINE];
    FLT64   dbl[_MAX_DOUBLES_PER_LINE];
    FLT32_P flt_p[_MAX_FLOATS_PER_LINE];
    FLT64_P dbl_p[_MAX_DOUBLES_PER_LINE];
} CACHELINE_DATA;

//--------------------------------------------------------------------
typedef struct _SYM {
    int sym;
    int offset_size;

    bool operator<(const _SYM& in) const
    {
        if (offset_size < in.offset_size) {
            return true;
        } else if (offset_size > in.offset_size) {
            return false;
        } else {    // same size
            int abs_sym = (sym > 0) ? sym : -sym;
            int abs_in_sym = (in.sym > 0) ? in.sym : -in.sym;
            return (abs_sym <= abs_in_sym);
        }
    }
} SYM;


//--------------------------------------------------------------------
class Compressor {
public:
    // constructor / destructor        
    Compressor(const string _name) : name(_name) { }
    Compressor(const string _name, size_t _patternCount)
        : name(_name), patternCount(_patternCount) {
        patternMap = new CNT[patternCount];
    }
    virtual ~Compressor() {
        if (patternMap) delete[] patternMap;
    }
public:
    // methods
    string getName() const { return name; }

    virtual LENGTH compressLine(CACHELINE_DATA* line, UINT64 line_addr) = 0;

    CNT getPatternResult(unsigned id) {
        return patternMap[id];            
    }
    virtual void printSummary(FILE* fd) {
        UINT64 accum = 0ull;
        for (auto it = lengthMap.cbegin(); it != lengthMap.cend(); ++it) {
            accum += (it->first * it->second);
        }
        fprintf(fd, "RESULT %s total   \t%lld\n", name.c_str(), totalLineCnt);
        fprintf(fd, "RESULT %s 64-bit  \t%f\n", name.c_str(), accum*1./totalLineCnt/_MAX_QWORDS_PER_LINE);
        fprintf(fd, "RESULT %s 32-bit  \t%f\n", name.c_str(), accum*1./totalLineCnt/_MAX_DWORDS_PER_LINE);
    }
    virtual void printDetails(FILE* fd, string bench_name) const {
        CNT accumLineCnt;

        fprintf(fd, "%s\t%s\n", bench_name.c_str(), name.c_str());
        fprintf(fd, "\t\t");
        for (int i=0; i<600; i+=5) {
            fprintf(fd, "%d\t", i);
        }
        fprintf(fd, "\n");
        accumLineCnt = 0ull;
        for (int i=0; i<600; i++) {
            auto it = lengthMap.find(i);
            if (it!=lengthMap.end()) {
                accumLineCnt += it->second;
            }
            if (i%5==0) {
                fprintf(fd, "%f\t", accumLineCnt*1./totalLineCnt);
            }
        }
        fprintf(fd, "\n");

        fprintf(fd, "Pattern frequency\n");
        for (size_t i=0; i<patternCount; i++) {
            // the sum of percentages will not be 100% because of zero runs.
            fprintf(fd, "%02zu\t%16lld\t%f\n", i, patternMap[i], patternMap[i]*1./ totalLineCnt / _MAX_DWORDS_PER_LINE);
        }
        fprintf(fd, "Compressed line size\n");
        for (auto it = lengthMap.cbegin(); it != lengthMap.cend(); ++it) {
            fprintf(fd, "%02d\t%16lld\t%f\n", it->first, it->second, it->second*1./ totalLineCnt);
        }
    }
    virtual void resetHistory() {
        totalLineCnt = 0ull;
        for (size_t i=0; i<patternCount; i++) {
            patternMap[i] = 0ull;
        }
        lengthMap.clear();
    }
protected:
    void compressFile(FILE *fd) {
        CACHELINE_DATA line;

        resetHistory();

        while (true) {
            if (fread(&line, _MAX_BYTES_PER_LINE, 1, fd)!=1) {
                break;
            }
            compressLine(&line, 0);
            //LENGTH length = compressLine(&line, 0);
            //if (length > 480) {
            //    compressLineDebug(&line, 0);
            //}
        }
    }
    virtual void addPatternResult(unsigned id) {
        patternMap[id]++;
    }
    virtual void addLineResult(LENGTH length) {
        totalLineCnt++;
        auto it = lengthMap.find(length);
        if (it==lengthMap.end()) {
            lengthMap.insert(pair<LENGTH, CNT>(length, 1ull));
        } else {
            it->second++;
        }
    }
    double getCoverage(int thresholdBitSize) {
        CNT accumLineCnt = 0ull;
        for (int i=0; i<=thresholdBitSize; i++) {
            auto it = lengthMap.find(i);
            if (it!=lengthMap.end()) {
                accumLineCnt += it->second;
            }
        }
        return accumLineCnt*1./totalLineCnt;
    }
    inline void updateLength(LENGTH newLength, LENGTH& minLength, unsigned newPattern, unsigned& minPattern) {
        if (newLength < minLength) {
            minPattern = newPattern;
            minLength = newLength;
        }
    }
protected:
    string name;

    CNT totalLineCnt;
    size_t patternCount;
    CNT *patternMap;
    map<LENGTH, CNT> lengthMap;
};

bool sign_extended(UINT64 value, UINT8 bit_size);
bool zero_extended(UINT64 value, UINT8 bit_size);

//--------------------------------------------------------------------
#endif /* __COMMON_HH__ */
