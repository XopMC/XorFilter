/*
 * hex_to_xor
 * Author: Mikhail Khoroshavin aka "XopMC"
 * Shared helpers for decoding hex input and keeping hash math compatible
 * with existing XOR filter files.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "intrin_local.h"

namespace hex_to_xor {

// Lookup table used by the legacy hex parser. We keep the same nibble mapping
// so that newly built filters stay compatible with older ones.
inline constexpr unsigned char kUnhexTable[80] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
inline constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

// Converts a 40-character hex string into the 20-byte hash buffer expected by
// the original builder logic.
inline void unhex40_20(const char* src, uint8_t* out20) {
    for (int i = 0; i < 20; ++i) {
        const unsigned char hi = kUnhexTable[static_cast<unsigned char>(src[i * 2]) & 0x4f];
        const unsigned char lo = kUnhexTable[static_cast<unsigned char>(src[i * 2 + 1]) & 0x4f];
        out20[i] = static_cast<uint8_t>((hi & 0xf0) | (lo & 0x0f));
    }
}

// Compressed filters store a single 64-bit key derived from the whole 20-byte
// input, so we keep the original FNV-1a pass unchanged.
inline uint64_t fnv1a_64_20(const uint8_t* input20) {
    uint64_t hash = kFnvOffsetBasis;
    for (int i = 0; i < 20; ++i) {
        hash ^= input20[i];
        hash *= kFnvPrime;
    }
    return hash;
}

// Reads a little-endian 64-bit word without depending on pointer punning.
inline uint64_t load_le_u64(const uint8_t* input) {
    uint64_t value = 0;
    std::memcpy(&value, input, sizeof(value));
    return value;
}

// Returns the high 64 bits of a 64x64 multiplication on every supported
// toolchain.
inline uint64_t mul_hi_u64(uint64_t lhs, uint64_t rhs) {
#if defined(__SIZEOF_INT128__) && !defined(_MSC_VER)
    const unsigned __int128 wide =
        static_cast<unsigned __int128>(lhs) * static_cast<unsigned __int128>(rhs);
    return static_cast<uint64_t>(wide >> 64);
#else
    uint64_t hi = 0;
    static_cast<void>(_umul128(lhs, rhs, &hi));
    return hi;
#endif
}

// Accepts the historical input variants used by the tool: plain 40-char hex,
// "0x" prefixed values, and uncompressed public key prefixes.
inline bool normalize_hex_input(const char*& line, size_t& length) {
    if (length >= 2 && line[0] == '0' && line[1] == 'x') {
        line += 2;
        length -= 2;
    }

    if (length >= 66 && line[0] == '0' && (line[1] == '2' || line[1] == '3' || line[1] == '4')) {
        line += 2;
        length -= 2;
    }

    return length >= 40;
}

// Decodes one input line into the exact key layout expected by the current
// on-disk format. Uncompressed mode yields two keys, compressed mode yields one.
inline size_t decode_keys_from_hex_line(const char* line, size_t length, bool compressed, uint64_t out_keys[2]) {
    if (!normalize_hex_input(line, length)) {
        return 0;
    }

    uint8_t hash20[20];
    unhex40_20(line, hash20);

    if (compressed) {
        if ((load_le_u64(hash20) | load_le_u64(hash20 + 8)) == 0) {
            return 0;
        }

        out_keys[0] = fnv1a_64_20(hash20);
        return 1;
    }

    hash20[3] &= hash20[16];
    hash20[7] &= hash20[17];
    hash20[11] &= hash20[18];
    hash20[15] &= hash20[19];

    const uint64_t lo = load_le_u64(hash20);
    const uint64_t hi = load_le_u64(hash20 + 8);
    if ((hi | lo) == 0) {
        return 0;
    }

    out_keys[0] = hi;
    out_keys[1] = lo;
    return 2;
}

}  // namespace hex_to_xor
