#pragma once

#include <cstdint>
#include <string>

namespace kallisto {

/**
 * SipHash-2-4 with customizable seeds for maximum security.
 * As a sysadmin, you control both the secret key and the internal 
 * magic constants (seeds) used to initialize the hash state.
 */
class SipHash {
public:
	/**
	 * Initializes SipHash with a 128-bit key and four 64-bit seeds.
	 * @param k0 First 64 bits of the secret key.
	 * @param k1 Second 64 bits of the secret key.
	 * @param s0 Seed for v0 initialization.
	 * @param s1 Seed for v1 initialization.
	 * @param s2 Seed for v2 initialization.
	 * @param s3 Seed for v3 initialization.
	 */
	SipHash(uint64_t k0, uint64_t k1, 
		uint64_t s0 = 0x736f6d6570736575ULL,
		uint64_t s1 = 0x646f72616e646f6dULL,
		uint64_t s2 = 0x6c7967656e657261ULL,
		uint64_t s3 = 0x7465646279746573ULL);

	/**
	 * Computes the 64-bit SipHash-2-4 result for the given string.
	 * @param input The string to hash.
	 * @return 64-bit hash value.
	 */
	uint64_t hash(const std::string& input) const;

private:
	uint64_t v0_init, v1_init, v2_init, v3_init;

	// SipRound logic (ARX)
	static inline uint64_t rotl(uint64_t x, int b) {
		return (x << b) | (x >> (64 - b));
	}

	static inline void sipround(uint64_t& v0, uint64_t& v1, 
				    uint64_t& v2, uint64_t& v3) {
		v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32);
		v2 += v3; v3 = rotl(v3, 16); v3 ^= v2;
		v0 += v3; v3 = rotl(v3, 21); v3 ^= v0;
		v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32);
	}
};

} // namespace kallisto
