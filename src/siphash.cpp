#include "kallisto/siphash.hpp"
#include <cstring>

namespace kallisto {

SipHash::SipHash(uint64_t k0, uint64_t k1, 
		 uint64_t s0, uint64_t s1, uint64_t s2, uint64_t s3) {
	v0_init = s0 ^ k0;
	v1_init = s1 ^ k1;
	v2_init = s2 ^ k0;
	v3_init = s3 ^ k1;
}

uint64_t SipHash::hash(const std::string& input) const {
	uint64_t v0 = v0_init;
	uint64_t v1 = v1_init;
	uint64_t v2 = v2_init;
	uint64_t v3 = v3_init;

	const uint8_t* m = reinterpret_cast<const uint8_t*>(input.data());
	size_t len = input.length();
	const uint8_t* end = m + (len & ~7);
	int left = len & 7;
	uint64_t b = static_cast<uint64_t>(len) << 56;

	for (; m < end; m += 8) {
		uint64_t mi;
		std::memcpy(&mi, m, 8);
		v3 ^= mi;
		for (int i = 0; i < 2; ++i)
			sipround(v0, v1, v2, v3);
		v0 ^= mi;
	}

	uint64_t t = 0;
	switch (left) {
	case 7:
		t |= static_cast<uint64_t>(m[6]) << 48;
		[[fallthrough]];
	case 6:
		t |= static_cast<uint64_t>(m[5]) << 40;
		[[fallthrough]];
	case 5:
		t |= static_cast<uint64_t>(m[4]) << 32;
		[[fallthrough]];
	case 4:
		t |= static_cast<uint64_t>(m[3]) << 24;
		[[fallthrough]];
	case 3:
		t |= static_cast<uint64_t>(m[2]) << 16;
		[[fallthrough]];
	case 2:
		t |= static_cast<uint64_t>(m[1]) << 8;
		[[fallthrough]];
	case 1:
		t |= static_cast<uint64_t>(m[0]);
		break;
	case 0:
		break;
	}

	b |= t;
	v3 ^= b;
	for (int i = 0; i < 2; ++i)
		sipround(v0, v1, v2, v3);
	v0 ^= b;

	v2 ^= 0xff;
	for (int i = 0; i < 4; ++i)
		sipround(v0, v1, v2, v3);

	return v0 ^ v1 ^ v2 ^ v3;
}

} // namespace kallisto
