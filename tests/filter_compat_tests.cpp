/*
 * hex_to_xor
 * Author: Mikhail Khoroshavin aka "XopMC"
 * Compatibility checks for the public helpers and the persisted filter format.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "hex_key_utils.h"
#include "xor_filter.h"

namespace {

// Small assertion helper so the test executable can fail with readable messages.
void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Loads keys from the fixture using the same decode path as the main builder.
std::vector<uint64_t> read_fixture_keys(const std::string& path, bool compressed_mode) {
    std::ifstream input(path);
    require(input.is_open(), "failed to open fixture file: " + path);

    std::vector<uint64_t> keys;
    std::string line;
    while (std::getline(input, line)) {
        uint64_t decoded[2] = {};
        const size_t count = hex_to_xor::decode_keys_from_hex_line(
            line.data(),
            line.size(),
            compressed_mode,
            decoded);
        keys.insert(keys.end(), decoded, decoded + count);
    }
    return keys;
}

// Guards the compatibility-critical input normalization and decode rules.
void test_decode_helpers() {
    {
        const std::string line = "00112233445566778899aabbccddeeff00112233";
        uint64_t decoded[2] = {};
        const size_t count = hex_to_xor::decode_keys_from_hex_line(line.data(), line.size(), false, decoded);
        require(count == 2, "expected two keys for uncompressed fixture");
        require(decoded[0] == 0x33eeddcc22aa9988ULL, "unexpected uncompressed high word");
        require(decoded[1] == 0x1166554400221100ULL, "unexpected uncompressed low word");
    }

    {
        const std::string line = "0x89abcdef0123456789abcdef0123456789abcdef";
        uint64_t decoded[2] = {};
        const size_t count = hex_to_xor::decode_keys_from_hex_line(line.data(), line.size(), false, decoded);
        require(count == 2, "expected two keys for 0x-prefixed fixture");
        require(decoded[0] == 0x67452301cdcdab89ULL, "unexpected prefixed high word");
        require(decoded[1] == 0x2345230189cdab89ULL, "unexpected prefixed low word");
    }

    {
        const std::string line = "ffffffffffffffffffffffffffffffffffffffff";
        uint64_t decoded[2] = {};
        const size_t count = hex_to_xor::decode_keys_from_hex_line(line.data(), line.size(), true, decoded);
        require(count == 1, "expected one key for compressed fixture");
        require(decoded[0] == 0x5f1c3870b1f0cd81ULL, "unexpected compressed key");
    }

    {
        const std::string line = "0000000000000000000000000000000000000000";
        uint64_t decoded[2] = {};
        const size_t count = hex_to_xor::decode_keys_from_hex_line(line.data(), line.size(), true, decoded);
        require(count == 0, "all-zero line should be skipped");
    }
}

// Verifies that the portable high-half multiplication helper matches known values.
void test_mul_hi_u64() {
    require(
        hex_to_xor::mul_hi_u64(0x0123456789abcdefULL, 0xfedcba9876543210ULL) == 0x0121fa00ad77d742ULL,
        "mul_hi_u64 case #1 failed");
    require(
        hex_to_xor::mul_hi_u64(0xffffffffffffffffULL, 0xffffffffffffffffULL) == 0xfffffffffffffffeULL,
        "mul_hi_u64 case #2 failed");
    require(
        hex_to_xor::mul_hi_u64(0x1234567890abcdefULL, 0x0fedcba987654321ULL) == 0x0121fa00acd77d74ULL,
        "mul_hi_u64 case #3 failed");
}

template <typename FingerprintType>
// Confirms that every key from the fixture is found in the saved golden filter.
void verify_filter_contains_all(const std::string& path, const std::vector<uint64_t>& keys) {
    xorbinaryfusefilter_lowmem4wise::XorBinaryFuseFilter<uint64_t, FingerprintType> filter;
    require(filter.LoadFromFile(path), "failed to load filter: " + path);
    for (uint64_t key : keys) {
        require(filter.Contain(key), "filter miss for key in: " + path);
    }
}

}  // namespace

int main() {
    try {
        test_decode_helpers();
        test_mul_hi_u64();

        const std::string fixture = "tests/fixtures/keys_small.txt";
        const std::vector<uint64_t> uncompressed_keys = read_fixture_keys(fixture, false);
        const std::vector<uint64_t> compressed_keys = read_fixture_keys(fixture, true);

        verify_filter_contains_all<uint32_t>("tests/golden/keys_small_0.xor_u", uncompressed_keys);
        verify_filter_contains_all<uint32_t>("tests/golden/keys_small_0.xor_c", compressed_keys);
        verify_filter_contains_all<uint16_t>("tests/golden/keys_small_0.xor_uc", compressed_keys);
        verify_filter_contains_all<uint8_t>("tests/golden/keys_small_0.xor_hc", compressed_keys);

        std::cout << "filter_compat_tests: ok\n";
        return 0;
    }
    catch (const std::exception& exception) {
        std::cerr << "filter_compat_tests: " << exception.what() << '\n';
        return 1;
    }
}
