#ifndef __VALUE_CACHE_HH__
#define __VALUE_CACHE_HH__

template <typename DATA_TYPE>
class ValueCache {
public:
    ValueCache(int s) {
        size = s;
        hitAccessCount = 0ull;
        totalAccessCount = 0ull;
        maxValue = (1ull<<(sizeof(DATA_TYPE)*8)) - 1;

        values = new DATA_TYPE[size];
        ages = new UINT8[size];

        init();
    }

    int getSize() { return size; }
    void init() {
        values[0] = 0;
        values[1] = maxValue;
        for (size_t i=0; i<(sizeof(DATA_TYPE)*8); i++) {
            values[i+2] = 1<<i;
        }
        for (size_t i=0; i<(sizeof(DATA_TYPE)*8); i++) {
            values[i+2+(sizeof(DATA_TYPE)*8)] = maxValue ^ (1<<i);
        }
        for (int i=0; i<size; i++) {
            ages[i] = 0;
        }
    }
    virtual bool access(DATA_TYPE data, int &index) {
        this->totalAccessCount++;

        // 1. find the exact match
        for (int i=0; i<this->size; i++) {
            if (data==this->values[i]) {    // hit
                // update ages for LRU replacement
                for (int j=0; j<this->size; j++) {
                    if (i!=j) {
                        if (this->ages[i]!=0xFF)
                            this->ages[j]++;
                    } else {
                        this->ages[j] = 0;
                    }
                }
                this->hitAccessCount++;
                index = i;
                return true;
            }
        }

        // miss -> find a victim
        int maxAge = -1;
        int maxAgeIndex = -1;
        for (int i=2; i<this->size; i++) {        // start from 2: -> no eviction on all 0 and all 1
            if (this->ages[i] > maxAge) {
                maxAgeIndex = i;
                maxAge = this->ages[i];
            }
        }
        // found the victim
        this->values[maxAgeIndex] = data;
        for (int i=0; i<this->size; i++) {
            if (i!=maxAgeIndex) {
                if (this->ages[i]!=0xFF)
                    this->ages[i]++;
            } else {
                this->ages[i] = 0;
            }
        }
        index = maxAgeIndex;
        return false;
    }
    void printDetails(FILE *fd) const {
        fprintf(fd, "Total acccess count\t%lld\n", totalAccessCount);
        fprintf(fd, "Hit acccess count\t%lld\n", hitAccessCount);
        fprintf(fd, "Hit ratio\t%f\n", (hitAccessCount * 1.) / totalAccessCount);
    }
protected:
    DATA_TYPE maxValue;
    DATA_TYPE *values;
    UINT8 *ages;
    int size;

    CNT totalAccessCount;
    CNT hitAccessCount;
};

template <typename DATA_TYPE>
class ValueCache2 : public ValueCache<DATA_TYPE> {
public:
    ValueCache2(int s) : ValueCache<DATA_TYPE>(s) {}

    bool access(DATA_TYPE data, int &index) {
        this->totalAccessCount++;

        // 1. find the exact match
        for (int i=0; i<this->size; i++) {
            if (data==this->values[i]) {    // hit
                // update ages for LRU replacement
                for (int j=0; j<this->size; j++) {
                    if (i!=j) {
                        if (this->ages[i]!=0xFF)
                            this->ages[j]++;
                    } else {
                        this->ages[j] = 0;
                    }
                }
                this->hitAccessCount++;
                index = i;
                return true;
            }
        }
        // 2. find a HD=1 match
        for (int i=0; i<this->size; i++) {
            DATA_TYPE diff = data ^ this->values[i];
            int HDcnt = 0;
            for (size_t k=0; k<(sizeof(DATA_TYPE)*8); k++) {
                if ((diff>>k)&1) HDcnt++;
            }
            if (HDcnt==1) {    // hit
                // update ages for LRU replacement
                for (int j=0; j<this->size; j++) {
                    if (i!=j) {
                        if (this->ages[i]!=0xFF)
                            this->ages[j]++;
                    } else {
                        this->ages[j] = 0;
                    }
                }
                if (i>2) {      // update
                    this->values[i] = data;
                }
                this->hitAccessCount++;
                index = i + (this->size+1);
                return true;
            }
        }

        // miss -> find a victim
        int maxAge = -1;
        int maxAgeIndex = -1;
        for (int i=2; i<this->size; i++) {        // start from 2: -> no eviction on all 0 and all 1
            if (this->ages[i] > maxAge) {
                maxAgeIndex = i;
                maxAge = this->ages[i];
            }
        }
        // found the victim
        this->values[maxAgeIndex] = data;
        for (int i=0; i<this->size; i++) {
            if (i!=maxAgeIndex) {
                if (this->ages[i]!=0xFF)
                    this->ages[i]++;
            } else {
                this->ages[i] = 0;
            }
        }
        index = maxAgeIndex;
        return false;
    }
};

#endif /* __VALUE_CACHE__ */
