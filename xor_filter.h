/*
 * hex_to_xor
 * Author: Mikhail Khoroshavin aka "XopMC"
 * Low-memory 4-wise XOR binary fuse filter implementation used by the builder.
 */

#ifndef FOURWISE_XOR_BINARY_FUSE_FILTER_XOR_FILTER_LOWMEM_H_
#define FOURWISE_XOR_BINARY_FUSE_FILTER_XOR_FILTER_LOWMEM_H_

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <sys/types.h>
#include <vector>

#include "hex_key_utils.h"

#define XOR_MAX_SIZE UINT32_MAX - 3
#define XOR_MIN_SIZE INT32_MAX - 3

class SimpleMixSplit {
public:
    // Stable 64-bit mixer used by the filter for key placement and fingerprints.
    inline static uint64_t murmur64(uint64_t h) {
        h ^= h >> 33;
        h *= UINT64_C(0xff51afd7ed558ccd);
        h ^= h >> 33;
        h *= UINT64_C(0xc4ceb9fe1a85ec53);
        h ^= h >> 33;
        return h;
    }

    inline uint64_t operator()(uint64_t key) const {
        return murmur64(key);
    }
};


// Chooses the segment length used by the 4-wise binary fuse layout.
inline uint64_t calculateSegmentLength(uint64_t arity, uint64_t size) {
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

// Returns the load factor that gives the builder enough slack to find a peel order.
inline double calculateSizeFactor(uint64_t arity, uint64_t size) {
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


        // Compresses the 64-bit hash down to the fingerprint width stored in the filter.
        static inline FingerprintType fingerprint(uint64_t hash)
        {
            return hash ^ (hash >> 32);
        }

        // Deterministic seed generation keeps builds reproducible across runs.
        static inline uint64_t rng_splitmix64(uint64_t* seed)
        {
            uint64_t z = (*seed += UINT64_C(0x9E3779B97F4A7C15));
            z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
            z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
            return z ^ (z >> 31);
        }

        // Stores a 2-bit edge index in a compact byte buffer.
        static inline void setPacked2(uint8_t* packed, uint64_t index, uint8_t value) {
            const uint64_t byteIndex = index >> 2;
            const uint64_t shift = (index & 3ULL) * 2ULL;
            const uint8_t mask = static_cast<uint8_t>(0x3u << shift);
            packed[byteIndex] = static_cast<uint8_t>((packed[byteIndex] & ~mask) | ((value & 0x3u) << shift));
        }

        // Reads a packed 2-bit edge index from the compact reverse-order buffer.
        static inline uint8_t getPacked2(const uint8_t* packed, uint64_t index) {
            const uint64_t shift = (index & 3ULL) * 2ULL;
            return static_cast<uint8_t>((packed[index >> 2] >> shift) & 0x3u);
        }

        // Maps a mixed key hash to one of the four filter positions used by the key.
        inline uint64_t getHashFromHash(uint64_t hash, int index) const {
            uint64_t h = hex_to_xor::mul_hi_u64(hash, segmentCountLength);
            h += index * segmentLength;
            const uint64_t hh = hash & ((1ULL << 32) - 1);
            if (index < 3) {
                h ^= (hh >> (36 - 18 * index)) & segmentLengthMask;
            }
            return h;
        }

        explicit XorBinaryFuseFilter(const uint64_t sizes) {
            uint64_t rng_counter = 0x726b2b9d438b9d4d;
            Seed = rng_splitmix64(&rng_counter);
            uint64_t size = sizes;
            if (size < 2) {
                size = 2;
            }
            hasher = new HashFamily();
            this->size = size;
            this->segmentLength = calculateSegmentLength(arity, size);
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
        }
        explicit XorBinaryFuseFilter() {
            uint64_t rng_counter = 0x726b2b9d438b9d4d;
            Seed = rng_splitmix64(&rng_counter);
            uint64_t size = 1;
            hasher = new HashFamily();
            this->size = size;
            this->segmentLength = calculateSegmentLength(arity, size);
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
        }

        XorBinaryFuseFilter(const XorBinaryFuseFilter&) = delete;
        XorBinaryFuseFilter& operator=(const XorBinaryFuseFilter&) = delete;
        XorBinaryFuseFilter(XorBinaryFuseFilter&&) = delete;
        XorBinaryFuseFilter& operator=(XorBinaryFuseFilter&&) = delete;

        ~XorBinaryFuseFilter() {
            delete[] fingerprints;
            delete hasher;
        }

        Status AddAll(const std::vector<ItemType>& data, const uint64_t start, const uint64_t end) {
            return AddAll(data.data(), start, end);
        }

        // Builds the filter in memory by repeatedly trying to peel the 4-wise graph.
        Status AddAll(const ItemType* data, const uint64_t start, const uint64_t end) {
            std::vector<uint64_t> reverseOrder(size + 1, 0);
            std::vector<uint8_t> reverseHPacked((size + 3) / 4, 0);
            uint64_t reverseOrderPos = 0;
            hashIndex = 0;

            uint64_t h0123[7];
            const uint8_t hi0123[7] = { 0, 1, 2, 3, 0, 1, 2 };
            int current_attempt = 0;
            double sizeFactor = calculateSizeFactor(arity, size);
            while (true) {
                std::vector<uint8_t> t2count(arrayLength, 0);
                std::vector<uint64_t> t2hash(arrayLength, 0);
                std::vector<uint64_t> alone(arrayLength, 0);
                bool countOverflow = false;

                current_attempt++;
                reverseOrderPos = 0;
                std::fill(reverseOrder.begin(), reverseOrder.end(), 0);
                reverseOrder[size] = 1;
                std::fill(reverseHPacked.begin(), reverseHPacked.end(), 0);

                uint64_t blockBits = 1;
                while ((uint64_t(1) << blockBits) < segmentCount) {
                    blockBits++;
                }
                const uint64_t block = uint64_t(1) << blockBits;
                std::vector<uint64_t> startPos(block, 0);
                for (uint64_t i = 0; i < block; i++) {
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
                for (uint64_t i = 0; i < size && !countOverflow; i++) {
                    const uint64_t hash = reverseOrder[i];
                    for (int hi = 0; hi < 4; hi++) {
                        const uint64_t index = getHashFromHash(hash, hi);
                        if (t2count[index] > std::numeric_limits<uint8_t>::max() - 4) {
                            countOverflow = true;
                            break;
                        }
                        t2count[index] = static_cast<uint8_t>(t2count[index] + 4);
                        t2count[index] ^= static_cast<uint8_t>(hi);
                        t2hash[index] ^= hash;
                    }
                }

                if (!countOverflow) {
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

                            setPacked2(reverseHPacked.data(), reverseOrderPos, static_cast<uint8_t>(found));
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

                                setPacked2(reverseHPacked.data(), reverseOrderPos, static_cast<uint8_t>(found));
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
                }

                if (!countOverflow && reverseOrderPos == size) {
                    break;
                }
                std::cerr << "[!] Error building XOR filter... " << reverseOrderPos
                    << " != " << size << ". Trying build new one, attempt = " << current_attempt << " [!]\n";

                // Grow the backing array gradually until the graph becomes peelable.
                sizeFactor *= 1.10;
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
            }

            for (uint64_t i = reverseOrderPos; i-- > 0;) {
                uint64_t hash = reverseOrder[i];
                int found = getPacked2(reverseHPacked.data(), i);
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

            return Ok;
        }

        // Checks whether the item is present using the four stored fingerprints.
        bool Contain(const ItemType& item) const {
            uint64_t hash = (*hasher)(item + Seed);
            FingerprintType f = fingerprint(hash);
            for (int hi = 0; hi < 4; hi++) {
                uint64_t h = getHashFromHash(hash, hi);
                f ^= fingerprints[h];
            }
            return f == 0;
        }

        // Returns a small human-readable summary that is handy in local debugging.
        std::string Info() const {
            std::stringstream ss;
            ss << "4-wise XorBinaryFuseFilter Status:\n"
                << "\t\tKeys stored: " << Size() << "\n";
            return ss.str();
        }

        uint64_t Size() const { return size; }

        uint64_t SizeInBytes() const { return arrayLength * sizeof(FingerprintType); }

        // Saves the filter using the legacy binary layout so older files remain readable.
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

        // Loads a filter written by SaveToFile without changing the on-disk format.
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
