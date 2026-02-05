#ifndef FOURWISE_XOR_BINARY_FUSE_FILTER_XOR_FILTER_LOWMEM_H_
#define FOURWISE_XOR_BINARY_FUSE_FILTER_XOR_FILTER_LOWMEM_H_

#include <vector>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <iostream>
#include <fstream>
#include <string>
#include "uint128.h"
#include <random>

bool IS_COMPRESS = false;
//using namespace std;
#define XOR_MAX_SIZE UINT32_MAX - 3
#define XOR_MIN_SIZE INT32_MAX - 3
#define CHAR_BIT 8

//#define XOR_ULTRA_SIZE XOR_MAX_SIZE * 2
class TwoIndependentMultiplyShift {
    uint128_t multiply_, add_;
public:
    TwoIndependentMultiplyShift() {
        ::std::random_device random;
        for (auto v : { &multiply_, &add_ }) {
            *v = random();
            for (int i = 1; i <= 4; ++i) {
                *v = *v << 32;
                *v |= random();
            }
        }
    }
    inline uint64_t operator()(uint64_t key) const {
        return (add_ + multiply_ * static_cast<decltype(multiply_)>(key)) >> 64;
    }

};


class SimpleMixSplit {

public:
    SimpleMixSplit() {
    }

    inline static uint64_t murmur64(uint64_t h) {
        h ^= h >> 33;
        h *= UINT64_C(0xff51afd7ed558ccd);
        h ^= h >> 33;
        h *= UINT64_C(0xc4ceb9fe1a85ec53);
        h ^= h >> 33;
        return h;
    }

    //inline static uint64_t murmur128(uint128_t h) {
    //    h ^= h >> 64;
    //    h *= uint128_t(UINT64_C(0xff51afd7ed558ccd)) << 64 | UINT64_C(0xff51afd7ed558ccd);
    //    h ^= h >> 64;
    //    h *= uint128_t(UINT64_C(0xc4ceb9fe1a85ec53)) << 64 | UINT64_C(0xc4ceb9fe1a85ec53);
    //    h ^= h >> 64;

    inline static uint64_t murmur128(uint128_t h) {
        //uint64_t h = (hash.m_hi ^ hash.m_lo);
        h ^= h >> 33;
        h *= UINT64_C(0xff51afd7ed558ccd);
        h ^= h >> 33;
        h *= UINT64_C(0xc4ceb9fe1a85ec53);
        h ^= h >> 33;
        return h;
    }

    //    return (h.m_hi ^ h.m_lo);
    //}
    //inline static uint64_t murmur128(uint128_t h) {
    //    h ^= h >> 64;
    //    h *= uint128_t(0x9e3779b97f4a7c15) << 64 | 0x9e3779b97f4a7c15;
    //    h ^= h >> 64;
    //    h *= uint128_t(0xc6a4a7935bd1e995) << 64 | 0xc6a4a7935bd1e995;
    //    h ^= h >> 64;
    //    return  (h.m_hi ^ h.m_lo);;
    //}

    inline uint64_t operator()(uint64_t key) const {
        return murmur64(key);
    }
    //inline uint128_t operator()(const void* key) const {
    //    return murmur128(&key, sizeof(key));
    //}
    inline uint128_t operator()(uint128_t key) const {

        return murmur128(key);
    }
};



uint64_t calculateSegmentLength(uint64_t arity, uint64_t size) {
    uint64_t segmentLength;
    if (arity == 3) {
        // We deliberately divide a log by a log so that the reader does not have
        // to ask about the basis of the log.
        segmentLength = 1ULL << (int)floor(log(size) / log(3.33) + 2.25);
    }
    else if (arity == 4) {
        segmentLength = 1ULL << (int)floor(log(size) / log(2.91) - 0.5);
    }
    else {
        // not supported
        segmentLength = 65536;
    }
    return segmentLength;
}

double calculateSizeFactor(uint64_t arity, uint64_t size) {
    if (size <= 2) { size = 2; }
    double sizeFactor;
    if (arity == 3) {
        sizeFactor = fmax(1.125, 0.875 + 0.25 * log(1000000) / log(size));
    }
    else if (arity == 4) {
        sizeFactor = fmax(1.075, 0.77 + 0.305 * log(600000) / log(size));
    }
    else {
        // not supported
        sizeFactor = 2.0;
    }
    return sizeFactor;
}
/**
 * As of July 2021, the lowmem versions of the binary fuse filters are
 * the recommended defaults.
 */
namespace xorbinaryfusefilter_lowmem4wise {

    // status returned by a xor filter operation
    enum Status {
        Ok = 0,
        NotFound = 1,
        NotEnoughSpace = 2,
        NotSupported = 3,
    };

    inline uint32_t reduce(uint32_t hash, uint32_t n) {
        return (uint32_t)(((uint64_t)hash * n) >> 32);
    }

    template <typename ItemType, typename FingerprintType, typename HashFamily = SimpleMixSplit>
    class XorBinaryFuseFilter {
    public:
        uint64_t Seed;
        uint64_t size;
        uint64_t arrayLength;
        uint64_t segmentCount;
        uint64_t segmentCountLength;
        uint64_t segmentLength;
        uint64_t segmentLengthMask;
        static constexpr uint64_t arity = 4;
        FingerprintType* fingerprints;
        HashFamily* hasher;
        uint64_t hashIndex{ 0 };


        static inline FingerprintType fingerprint(uint64_t hash)
        {

             return hash ^ (hash >> 32);

           
        }

        static inline uint64_t rng_splitmix64(uint64_t* seed)
        {
            uint64_t z = (*seed += UINT64_C(0x9E3779B97F4A7C15));
            z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
            z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
            return z ^ (z >> 31);
        }
        /*inline FingerprintType fingerprint(const uint128_t hash) const {
            return (FingerprintType)hash;
        }*/
        //inline FingerprintType fingerprint(const uint64_t hash) const {
        //    //uint32_t low = hash & 0xFFFFFFFF; 
        //    //uint32_t high = (hash >> 32) & 0xFFFFFFFF; 
        //    return static_cast<FingerprintType>(hash); 
        //}


        static inline uint64_t rotateLeft(uint64_t n, unsigned int c) {
            const unsigned int mask = (CHAR_BIT * sizeof(n) - 1);
            c &= mask;
            return (n << c) | (n >> ((-c) & mask));
        }

        static inline uint64_t rotateRight(uint64_t n, unsigned int c) {
            const unsigned int mask = (CHAR_BIT * sizeof(n) - 1);
            c &= mask;
            return (n >> c) | (n << ((-c) & mask));
        }

        /*inline uint64_t getHashFromHash(uint64_t hash, int index) const {
            uint128_t x = (uint128_t)hash * (uint128_t)segmentCountLength;
            uint64_t h = (uint64_t)(x >> 64);
            h += index * segmentLength;
            uint64_t hh = hash;
            if (index > 0) {
                h ^= hh >> ((index - 1) * 16) & segmentLengthMask;
            }
            return h;
        }*/
        inline uint64_t getHashFromHash(uint64_t hash, int index)  const
            //  const binary_fuse_t *filter)
        {
            /*if (IS_COMPRESS) {

                uint128_t x = (uint128_t)hash * (uint128_t)segmentCountLength;
                uint64_t h = (uint64_t)(x >> 64);
                h += index * segmentLength;
                uint64_t hh = hash;
                if (index > 0) {
                    h ^= hh >> ((index - 1) * 16) & segmentLengthMask;
                }
                return h;

            }
            else {*/
                uint128_t x = (uint128_t)hash * (uint128_t)segmentCountLength;
                uint64_t h = (uint64_t)(x >> 64);
                h += index * segmentLength;
                // keep the lower 36 bits
                uint64_t hh = hash & ((1ULL << 32) - 1);
                // index 0: right shift by 36; index 1: right shift by 18; index 2: no shift
                if (index < 3)
                {
                    h ^= (uint64_t)((hh >> (36 - 18 * index)) & segmentLengthMask);
                }
                return h;
            //}
        }




        explicit XorBinaryFuseFilter(const uint64_t sizes) {
            uint64_t rng_counter = 0x726b2b9d438b9d4d;
            Seed = rng_splitmix64(&rng_counter);
            uint64_t size = sizes;
            if (size < 2)
            {
                size = 2;
            }
            /*if (size % 2 != 0)
            {
                size++;
            }*/
            hasher = new HashFamily();
            this->size = size;
            this->segmentLength = calculateSegmentLength(arity, size);
           /* if (this->segmentLength > (1 << 18)) {
                this->segmentLength = (1 << 18);
            }*/
            double sizeFactor = calculateSizeFactor(arity, size);
            uint64_t capacity = size * sizeFactor;
            uint64_t segmentCount = (capacity + segmentLength - 1) / segmentLength - (arity - 1);
            this->arrayLength = (segmentCount + arity - 1) * segmentLength;
            this->segmentLengthMask = this->segmentLength - 1;
            this->segmentCount = (this->arrayLength + this->segmentLength - 1) / this->segmentLength;
            this->segmentCount = this->segmentCount <= arity - 1 ? 1 : this->segmentCount - (arity - 1);
            this->arrayLength = (this->segmentCount + arity - 1) * this->segmentLength;
            this->segmentCountLength = this->segmentCount * this->segmentLength;
            fingerprints = new FingerprintType[arrayLength]();
            std::fill_n(fingerprints, arrayLength, 0);
        }
        explicit XorBinaryFuseFilter() {
            uint64_t rng_counter = 0x726b2b9d438b9d4d;
            Seed = rng_splitmix64(&rng_counter);
            uint64_t size = 1;
            hasher = new HashFamily();
            this->size = size;
            this->segmentLength = calculateSegmentLength(arity, size);
           /* if (this->segmentLength > (1 << 18)) {
                this->segmentLength = (1 << 18);
            }*/
            double sizeFactor = calculateSizeFactor(arity, size);
            uint64_t capacity = size * sizeFactor;
            uint64_t segmentCount = (capacity + segmentLength - 1) / segmentLength - (arity - 1);
            this->arrayLength = (segmentCount + arity - 1) * segmentLength;
            this->segmentLengthMask = this->segmentLength - 1;
            this->segmentCount = (this->arrayLength + this->segmentLength - 1) / this->segmentLength;
            this->segmentCount = this->segmentCount <= arity - 1 ? 1 : this->segmentCount - (arity - 1);
            this->arrayLength = (this->segmentCount + arity - 1) * this->segmentLength;
            this->segmentCountLength = this->segmentCount * this->segmentLength;
            fingerprints = new FingerprintType[arrayLength]();
            std::fill_n(fingerprints, arrayLength, 0);
        }

        ~XorBinaryFuseFilter() {
            delete[] fingerprints;
            delete hasher;
        }

        Status AddAll(const std::vector<ItemType>& data, const uint64_t start, const uint64_t end) {
            return AddAll(data.data(), start, end);
        }


        Status AddAll(const ItemType* data, const uint64_t start, const uint64_t end) {


            uint64_t* reverseOrder = new uint64_t[size + 1];
            uint8_t* reverseH = new uint8_t[size];
            uint64_t reverseOrderPos;


            hashIndex = 0;

            uint64_t h0123[7];
            uint64_t hi0123[7];
            hi0123[0] = 0;
            hi0123[1] = 1;
            hi0123[2] = 2;
            hi0123[3] = 3;
            hi0123[4] = 0;
            hi0123[5] = 1;
            hi0123[6] = 2;
            int MAX_ATTEMPS = 1;
            int current_attempt = 0;
            double sizeFactor = calculateSizeFactor(arity, size);
            while (true) {
                uint16_t* t2count = new uint16_t[arrayLength];
                uint64_t* t2hash = new uint64_t[arrayLength];
                uint64_t* alone = new uint64_t[arrayLength];

                current_attempt++;
                reverseOrderPos = 0;
                memset(t2count, 0, sizeof(uint16_t) * arrayLength);
                memset(t2hash, 0, sizeof(uint64_t) * arrayLength);

                memset(reverseOrder, 0, sizeof(uint64_t) * size);
                reverseOrder[size] = 1;

                uint64_t blockBits = 1;
                while ((uint64_t(1) << blockBits) < segmentCount) {
                    blockBits++;
                }
                uint64_t block = uint64_t(1) << blockBits;
                uint64_t* startPos = new uint64_t[block];
                for (uint64_t i = 0; i < uint64_t(1) << blockBits; i++) {
                    startPos[i] = i * size / block;
                }
                for (uint64_t i = start; i < end; i++) {
                    ItemType k = data[i];
                    uint64_t hash = (*hasher)(k + Seed);
                    uint64_t segment_index = hash >> (64 - blockBits);
                    while (reverseOrder[startPos[segment_index]] != 0) {
                        segment_index++;
                        segment_index &= (uint64_t(1) << blockBits) - 1;
                    }
                    reverseOrder[startPos[segment_index]] = hash;
                    startPos[segment_index]++;
                }
                uint8_t countMask = 0;
                for (uint64_t i = 0; i < size; i++) {
                    uint64_t hash = reverseOrder[i];
                    for (int hi = 0; hi < 4; hi++) {
                        uint64_t index = getHashFromHash(hash, hi);
                        t2count[index] += 4;
                        t2count[index] ^= hi;
                        t2hash[index] ^= hash;
                        countMask |= t2count[index];
                    }
                }
                delete[] startPos;
                /*if (countMask >= 0x80) {
                    memset(fingerprints, ~0, arrayLength * sizeof(FingerprintType));
                    return Ok;
                }*/
                reverseOrderPos = 0;
                uint64_t alonePos = 0;
                for (uint64_t i = 0; i < arrayLength; i++) {
                    alone[alonePos] = i;
                    int inc = (t2count[i] >> 2) == 1 ? 1 : 0;
                    alonePos += inc;
                }

                while (alonePos > 0) {
                    alonePos--;
                    uint64_t index = alone[alonePos];
                    if ((t2count[index] >> 2) == 1) {
                        uint64_t hash = t2hash[index];
                        int found = t2count[index] & 3;

                        reverseH[reverseOrderPos] = found;
                        reverseOrder[reverseOrderPos] = hash;

                        h0123[1] = getHashFromHash(hash, 1);
                        h0123[2] = getHashFromHash(hash, 2);
                        h0123[3] = getHashFromHash(hash, 3);
                        h0123[4] = getHashFromHash(hash, 0);
                        h0123[5] = h0123[1];
                        h0123[6] = h0123[2];

                        uint64_t index3 = h0123[found + 1];
                        alone[alonePos] = index3;
                        alonePos += ((t2count[index3] >> 2) == 2 ? 1 : 0);
                        t2count[index3] -= 4;
                        t2count[index3] ^= hi0123[found + 1];
                        t2hash[index3] ^= hash;

                        index3 = h0123[found + 2];
                        alone[alonePos] = index3;
                        alonePos += ((t2count[index3] >> 2) == 2 ? 1 : 0);
                        t2count[index3] -= 4;
                        t2count[index3] ^= hi0123[found + 2];
                        t2hash[index3] ^= hash;

                        index3 = h0123[found + 3];
                        alone[alonePos] = index3;
                        alonePos += ((t2count[index3] >> 2) == 2 ? 1 : 0);
                        t2count[index3] -= 4;
                        t2count[index3] ^= hi0123[found + 3];
                        t2hash[index3] ^= hash;

                        reverseOrderPos++;
                    }
                }

                if (reverseOrderPos < size) {
                    for (uint64_t idx = 0; idx < arrayLength; idx++) {
                        if ((t2count[idx] >> 2) > 1) {
                            alone[alonePos++] = idx;
                            break;

                        }

                    }

                    while (alonePos > 0) {
                        alonePos--;
                        uint64_t index = alone[alonePos];
                        if ((t2count[index] >> 2) == 1) {
                            uint64_t hash = t2hash[index];
                            int found = t2count[index] & 3;

                            reverseH[reverseOrderPos] = found;
                            reverseOrder[reverseOrderPos] = hash;

                            h0123[1] = getHashFromHash(hash, 1);
                            h0123[2] = getHashFromHash(hash, 2);
                            h0123[3] = getHashFromHash(hash, 3);
                            h0123[4] = getHashFromHash(hash, 0);
                            h0123[5] = h0123[1];
                            h0123[6] = h0123[2];

                            uint64_t index3 = h0123[found + 1];
                            alone[alonePos] = index3;
                            alonePos += ((t2count[index3] >> 2) == 2 ? 1 : 0);
                            t2count[index3] -= 4;
                            t2count[index3] ^= hi0123[found + 1];
                            t2hash[index3] ^= hash;

                            index3 = h0123[found + 2];
                            alone[alonePos] = index3;
                            alonePos += ((t2count[index3] >> 2) == 2 ? 1 : 0);
                            t2count[index3] -= 4;
                            t2count[index3] ^= hi0123[found + 2];
                            t2hash[index3] ^= hash;

                            index3 = h0123[found + 3];
                            alone[alonePos] = index3;
                            alonePos += ((t2count[index3] >> 2) == 2 ? 1 : 0);
                            t2count[index3] -= 4;
                            t2count[index3] ^= hi0123[found + 3];
                            t2hash[index3] ^= hash;

                            reverseOrderPos++;
                        }
                    }

                }
                if (reverseOrderPos == size) {
                    break;
                }
                printf("[!] Error building XOR filter... %llu != %llu . Trying build new one, attemp = %i [!]\n", reverseOrderPos, size, current_attempt);

                
                sizeFactor *= 1.10;      // +10%
                //arrayLength = size * sizeFactor;
                uint64_t capacity = size * sizeFactor;
                uint64_t segmentCount_local = (capacity + segmentLength - 1) / segmentLength - (arity - 1);
                arrayLength = (segmentCount_local + arity - 1) * segmentLength;
                segmentLengthMask = segmentLength - 1;
                segmentCount = (arrayLength + segmentLength - 1) / segmentLength;
                segmentCount = segmentCount <= arity - 1 ? 1 : segmentCount - (arity - 1);
                arrayLength = (segmentCount + arity - 1) * segmentLength;
                segmentCountLength = segmentCount * segmentLength;
                delete[] fingerprints;
                fingerprints = new FingerprintType[arrayLength]();
                delete[] alone;
                delete[] t2count;
                delete[] t2hash;

            }


            for (uint64_t i = reverseOrderPos; i-- > 0;) {
                uint64_t hash = reverseOrder[i];
                int found = reverseH[i];
                FingerprintType xor2 = fingerprint(hash);
                h0123[0] = getHashFromHash(hash, 0);
                h0123[1] = getHashFromHash(hash, 1);
                h0123[2] = getHashFromHash(hash, 2);
                h0123[3] = getHashFromHash(hash, 3);
                h0123[4] = h0123[0];
                h0123[5] = h0123[1];
                h0123[6] = h0123[2];
                fingerprints[h0123[found]] = xor2 ^ fingerprints[h0123[found + 1]] ^ fingerprints[h0123[found + 2]] ^ fingerprints[h0123[found + 3]];
            }
            delete[] reverseOrder;
            delete[] reverseH;

            return Ok;
        }

        bool Contain(const ItemType& item) const {
            uint64_t hash = (*hasher)(item + Seed);
            FingerprintType f = fingerprint(hash);
            for (int hi = 0; hi < 4; hi++) {
                uint64_t h = getHashFromHash(hash, hi);
                f ^= fingerprints[h];
            }
            return f == 0 ? true : false;
        }

        std::string Info() const {
            std::stringstream ss;
            ss << "4-wise XorBinaryFuseFilter Status:\n"
                << "\t\tKeys stored: " << Size() << "\n";
            return ss.str();
        }

        uint64_t Size() const { return size; }

        uint64_t SizeInBytes() const { return arrayLength * sizeof(FingerprintType); }

        // Function to save the filter to a file
        bool SaveToFile(const std::string& filename) const {
            std::ofstream out(filename, std::ios::binary);
            if (!out.is_open()) {
                return false;
            }
            out.write(reinterpret_cast<const char*>(&size), sizeof(size));
            out.write(reinterpret_cast<const char*>(&arrayLength), sizeof(arrayLength));
            out.write(reinterpret_cast<const char*>(&segmentCount), sizeof(segmentCount));
            out.write(reinterpret_cast<const char*>(&segmentCountLength), sizeof(segmentCountLength));
            out.write(reinterpret_cast<const char*>(&segmentLength), sizeof(segmentLength));
            out.write(reinterpret_cast<const char*>(&segmentLengthMask), sizeof(segmentLengthMask));
            out.write(reinterpret_cast<const char*>(fingerprints), sizeof(FingerprintType) * arrayLength);
            out.close();
            return true;
        }

        // Function to load the filter from a file
        bool LoadFromFile(const std::string& filename) {
            std::ifstream in(filename, std::ios::binary);
            if (!in.is_open()) {
                return false;
            }
            in.read(reinterpret_cast<char*>(&size), sizeof(size));
            in.read(reinterpret_cast<char*>(&arrayLength), sizeof(arrayLength));
            in.read(reinterpret_cast<char*>(&segmentCount), sizeof(segmentCount));
            in.read(reinterpret_cast<char*>(&segmentCountLength), sizeof(segmentCountLength));
            in.read(reinterpret_cast<char*>(&segmentLength), sizeof(segmentLength));
            in.read(reinterpret_cast<char*>(&segmentLengthMask), sizeof(segmentLengthMask));
            delete[] fingerprints;
            fingerprints = new FingerprintType[arrayLength];
            in.read(reinterpret_cast<char*>(fingerprints), sizeof(FingerprintType) * arrayLength);
            in.close();
            return true;
        }
    };

} // namespace xorbinaryfusefilter_lowmem4wise

#endif // FOURWISE_XOR_BINARY_FUSE_FILTER_XOR_FILTER_LOWMEM_H_
