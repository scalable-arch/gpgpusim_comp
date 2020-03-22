#ifndef __FUNCTION_H__
#define __FUNCTION_H__

#include <string>
#include <vector>

//------------------------------------------------------------------------------
using namespace std;

typedef unsigned long long mword;
class mblock;
class virtual_stream;

//------------------------------------------------------------------------------
typedef bool (*BlockFunctionPointer)(virtual_stream *stream, mblock *block, int idx);
typedef bool (*WordFunctionPointer)(mword word);

//------------------------------------------------------------------------------
class FunctionInfo {
public:
    FunctionInfo(unsigned _ID, string _name, int _level, unsigned _data_size, BlockFunctionPointer _fp)
    : ID(_ID), name(_name), level(_level), data_size(_data_size), fp(_fp) {}
    FunctionInfo(FunctionInfo *src)
    : ID(src->ID), name(src->name), level(src->level), data_size(src->data_size), fp(src->fp) {}
public:
    unsigned ID;
    string name;
    int level;
    unsigned data_size;
    BlockFunctionPointer fp;
};

//------------------------------------------------------------------------------
class PatternInfo : public FunctionInfo {
public:
    PatternInfo(FunctionInfo *info, unsigned _opcode_size)
    : FunctionInfo(info), size(info->data_size+_opcode_size) {}
    bool operator<(const PatternInfo& rhs) const {
        if (size==rhs.size) {
            return (level==0);
        } else {
            return size < rhs.size;
        }
    }
public:
    unsigned size;
};

//------------------------------------------------------------------------------
bool B00_all_zero(virtual_stream *stream, mblock *block, int idx);
bool B01_all_one(virtual_stream *stream, mblock *block, int idx);
bool B02_repeated_byte(virtual_stream *stream, mblock *block, int idx);
bool B03_one_hot(virtual_stream *stream, mblock *block, int idx);
bool B04_SE_3_3(virtual_stream *stream, mblock *block, int idx);
bool B05_SE_7_7(virtual_stream *stream, mblock *block, int idx);
bool B06_SE_15_15(virtual_stream *stream, mblock *block, int idx);
bool B07_SE_0_31(virtual_stream *stream, mblock *block, int idx);
bool B08_SE_31_0(virtual_stream *stream, mblock *block, int idx);

bool I00_all_zero(virtual_stream *stream, mblock *block, int idx);
bool I01_all_one(virtual_stream *stream, mblock *block, int idx);
bool I02_repeated_byte(virtual_stream *stream, mblock *block, int idx);
bool I03_one_hot(virtual_stream *stream, mblock *block, int idx);
bool I04_SE_3_3(virtual_stream *stream, mblock *block, int idx);
bool I05_SE_7_7(virtual_stream *stream, mblock *block, int idx);
bool I06_SE_15_15(virtual_stream *stream, mblock *block, int idx);
bool I07_SE_0_31(virtual_stream *stream, mblock *block, int idx);
bool I08_SE_31_0(virtual_stream *stream, mblock *block, int idx);

bool J00_all_zero(virtual_stream *stream, mblock *block, int idx);
bool J01_all_one(virtual_stream *stream, mblock *block, int idx);
bool J02_repeated_byte(virtual_stream *stream, mblock *block, int idx);
bool J03_one_hot(virtual_stream *stream, mblock *block, int idx);
bool J04_SE_3_3(virtual_stream *stream, mblock *block, int idx);
bool J05_SE_7_7(virtual_stream *stream, mblock *block, int idx);
bool J06_SE_15_15(virtual_stream *stream, mblock *block, int idx);
bool J07_SE_0_31(virtual_stream *stream, mblock *block, int idx);
bool J08_SE_31_0(virtual_stream *stream, mblock *block, int idx);

bool K00_all_zero(virtual_stream *stream, mblock *block, int idx);
bool K01_all_one(virtual_stream *stream, mblock *block, int idx);
bool K02_repeated_byte(virtual_stream *stream, mblock *block, int idx);
bool K03_one_hot(virtual_stream *stream, mblock *block, int idx);
bool K04_SE_3_3(virtual_stream *stream, mblock *block, int idx);
bool K05_SE_7_7(virtual_stream *stream, mblock *block, int idx);
bool K06_SE_15_15(virtual_stream *stream, mblock *block, int idx);
bool K07_SE_0_31(virtual_stream *stream, mblock *block, int idx);
bool K08_SE_31_0(virtual_stream *stream, mblock *block, int idx);

bool NCP_nocompression(virtual_stream *stream, mblock *block, int idx);

//------------------------------------------------------------------------------
extern vector<FunctionInfo> functionInfoVector;

#endif /* __FUNCTION_H__ */
