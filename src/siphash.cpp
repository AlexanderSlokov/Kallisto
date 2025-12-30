#include "kallisto/siphash.hpp"
#include <cstring>

namespace kallisto {

SipHash::SipHash(uint64_t first_part, uint64_t second_part) : first_part(first_part), second_part(second_part) {}

uint64_t SipHash::hash(const std::string& input) const {
    return hash(input, first_part, second_part);
}

uint64_t SipHash::hash(const std::string& input, uint64_t first_part, uint64_t second_part) {
    // Four rounds of sipround.
    // But we can do more, make a second secret key 
    // a generate function to create this 4 SipRound,
    // so that each instance of Kallisto Server can have its own unique seeds.
    // prevent hash collision between different instances.
    uint64_t v0 = 0x736f6d6570736575ULL ^ first_part;
    uint64_t v1 = 0x646f72616e646f6dULL ^ second_part;
    uint64_t v2 = 0x6c7967656e657261ULL ^ first_part;
    uint64_t v3 = 0x7465646279746573ULL ^ second_part;

    // XOR nó vào v_3.
    // Quay sipround 2 lần (đó là lý do gọi là SipHash-2-4).
    // XOR nó vào v_0 để "khóa" dữ liệu lại.
    const uint8_t* m = reinterpret_cast<const uint8_t*>(input.data());
    size_t len = input.length();
    const uint8_t* end = m + (len & ~7);
    int left = len & 7;
    uint64_t b = static_cast<uint64_t>(len) << 56;

    for (; m < end; m += 8) {
        uint64_t mi;
        std::memcpy(&mi, m, 8);
        v3 ^= mi;
        for (int i = 0; i < 2; ++i) sipround(v0, v1, v2, v3);
        v0 ^= mi;
    }

    uint64_t t = 0;
    switch (left) {
        case 7: t |= static_cast<uint64_t>(m[6]) << 48; [[fallthrough]];
        case 6: t |= static_cast<uint64_t>(m[5]) << 40; [[fallthrough]];
        case 5: t |= static_cast<uint64_t>(m[4]) << 32; [[fallthrough]];
        case 4: t |= static_cast<uint64_t>(m[3]) << 24; [[fallthrough]];
        case 3: t |= static_cast<uint64_t>(m[2]) << 16; [[fallthrough]];
        case 2: t |= static_cast<uint64_t>(m[1]) << 8; [[fallthrough]];
        case 1: t |= static_cast<uint64_t>(m[0]); break;
        case 0: break;
    }
    // Sau khi xay xong dữ liệu, "bồi" thêm một hằng số 0xff vào v_2.
    // Sau đó cho sipround chạy liên tục 4 lần để các bit được trộn lẫn.
    // Cuối cùng, gom 4 biến v_0, v_1, v_2, v_3 XOR lại với nhau để ra con số 64-bit cuối cùng.

    b |= t;
    v3 ^= b;
    for (int i = 0; i < 2; ++i) sipround(v0, v1, v2, v3);
    v0 ^= b;

    v2 ^= 0xff;
    for (int i = 0; i < 4; ++i) sipround(v0, v1, v2, v3);

    return v0 ^ v1 ^ v2 ^ v3;
}
} // namespace kallisto
