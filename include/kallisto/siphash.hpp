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

	static inline void sipround(uint64_t& state0, uint64_t& state1, 
				    uint64_t& state2, uint64_t& state3) {
		state0 += state1; state1 = rotl(state1, 13); state1 ^= state0; state0 = rotl(state0, 32);
		state2 += state3; state3 = rotl(state3, 16); state3 ^= state2;
		state0 += state3; state3 = rotl(state3, 21); state3 ^= state0;
		state2 += state1; state1 = rotl(state1, 17); state1 ^= state2; state2 = rotl(state2, 32);
	}
};

} // namespace kallisto
