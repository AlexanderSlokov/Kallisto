#pragma once

#include <cstdint>
#include <string>

namespace kallisto {

/**
 * SipHash-2-4 need a 128-bit key (seed) to be used efficiently.
 * But why 128 bit? Can I increase it to 256 bit?
 */
class SipHash {
public:
	/**
	 * Initializes SipHash with a 128-bit key (seed).
	 * @param first_part First 64 bits of the key.
	 * @param second_part Second 64 bits of the key.
	 */
	SipHash(uint64_t first_part, uint64_t second_part);

	/**
	 * Computes the 64-bit SipHash-2-4 result for the given string.
	 * @param input The string to hash.
	 * @return 64-bit hash value.
	 */
	uint64_t hash(const std::string& input) const;

	/**
	 * Helper to get hash with custom key.
	 */
	static uint64_t hash(const std::string& input, uint64_t first_part, 
			     uint64_t second_part);

private:
	uint64_t first_part, second_part;

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
