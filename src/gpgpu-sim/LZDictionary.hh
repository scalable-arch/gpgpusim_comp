#ifndef __LZ_DICTIONARY_HH__
#define __LZ_DICTIONARY_HH__

// implementation of LZ78, dictionary-based compression

class LZDictionary {
public:
    LZDictionary(unsigned dSize, int mDepth, UINT8 mValue) : dictSize(dSize), maxValue(mValue) {
        dictWidth = (unsigned) log2(dictSize);
        maxWeightedDepth = mDepth * 1.;
        init();
    }
    void init() {
        table.clear();

        for (int i=0; i<128; i++) {
            table[std::make_pair(i, 0)] = i+1;
        }
        for (int i=1; i<=maxValue; i++) {
            table[std::make_pair(0, (UINT8) i)] = i+128;
        }

        insertPos = maxValue+129;
        curPos = 0;
        curDepth = 0;
        curWeightedDepth = 0.;

        for (int i=0; i<129; i++) {
            depthArray[i] = 0ull;
        }
    }

    bool find(UINT8 input, bool flush) {
        pair<unsigned, UINT8> akey = make_pair(11, 0);
        auto ait = table.find(akey);
        //printf("%4d (%02x) %d: ", curPos, input, (ait!=table.end()));
        pair<unsigned, UINT8> key = make_pair(curPos, input);
        auto it = table.find(key);

        if (it==table.end()) {
            if (!flush) {
                if (insertPos < dictSize) {
                    table[key] = insertPos++;
                } else {
                    printf("reset\n");
                    init();
                }
            }
            key = make_pair(0, input);
            it = table.find(key);
            assert(it != table.end());
            depthArray[curDepth]++;
            curPos = table[key];
            curDepth = 0;
            curWeightedDepth = 0.;
            //printf("E (%4d: %02x)\n", insertPos, input);
            return false;
        } else {
            if (flush) {
                //printf("F\n");
                depthArray[curDepth]++;
                curPos = 0;
                curDepth = 0;
                curWeightedDepth = 0.;
                return false;
            } else {
                //printf("-> ");
                curPos = it->second;
                curDepth++;
                if (input!=0) {
                    curWeightedDepth += 1.;
                }
                return true;
            }
        }
        //if ((curWeightedDepth >= maxWeightedDepth) || flush) {
    }
    void printDetails(FILE *fd) const {
        for (int i=0; i<33; i++) {
            fprintf(fd, "%lld\t", depthArray[i]);
        }
        fprintf(fd, "\n");
    }
    inline unsigned getWidth() const { return dictWidth; }
private:
    std::map<std::pair<unsigned, UINT8>, unsigned> table;
    unsigned dictSize, dictWidth;
    int maxValue;
    unsigned curPos, insertPos;
    unsigned curDepth;
    double curWeightedDepth;
    double maxWeightedDepth;
    CNT depthArray[129];
};

#endif /* __LZ_DICTIONARY_HH__ */
