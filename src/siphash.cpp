#include "kallisto/siphash.hpp"
#include <cstring>

namespace kallisto {

SipHash::SipHash(uint64_t first_part, uint64_t second_part) :
	first_part(first_part), second_part(second_part) {}

uint64_t SipHash::hash(const std::string& input) const {
	return hash(input, first_part, second_part);
}

uint64_t SipHash::hash(
	const std::string& input, 
	uint64_t first_part, 
	uint64_t second_part) {
	
	// Khởi tạo trạng thái nội bộ với các hằng số "nothing-up-my-sleeve"
	// đã có sẵn trong tài liệu thuật toán SipHash. Tham khảo tại:
	// https://cr.yp.to/siphash/siphash-20120918.pdf
	// Mục đích: phá vỡ tính đối xứng ban đầu.
	uint64_t v0 = 0x736f6d6570736575ULL ^ first_part; // "somepseu"
	uint64_t v1 = 0x646f72616e646f6dULL ^ second_part; // "dorandom"
	uint64_t v2 = 0x6c7967656e657261ULL ^ first_part; // "lygenera"
	uint64_t v3 = 0x7465646279746573ULL ^ second_part; // "tedbytes"

	const uint8_t* m = reinterpret_cast<const uint8_t*>(input.data());
	size_t len = input.length();
	const uint8_t* end = m + (len & ~7);
	int left = len & 7;
	uint64_t b = static_cast<uint64_t>(len) << 56;

	// 2. Compression Loop (Vòng lặp nén)
	// Cắt input thành từng block 8 bytes (64-bit) để xử lý.
	// Với mỗi block 64-bit 'mi':
	// - XOR 'mi' vào v3 (Nạp dữ liệu vào trạng thái)
	// - Chạy 2 vòng sipround (Xáo trộn)
	// - XOR 'mi' vào v0 (Khóa dữ liệu lại)
	for (; m < end; m += 8) {
		uint64_t mi;
		std::memcpy(&mi, m, 8);
		v3 ^= mi;
		for (int i = 0; i < 2; ++i) sipround(v0, v1, v2, v3);
		v0 ^= mi;
	}

	// Nếu chuỗi không chia hết cho 8 thì ta chỉ việc dùng switch-case để nhặt nốt những byte bị chia dư ra cuối cùng.
	// Đặc biệt, độ dài của chuỗi được gán vào byte cao nhất (dòng 22) để đảm bảo chuỗi abc và abc\0 sẽ cho ra mã băm khác hẳn nhau.
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
	// Sau khi băm xong dữ liệu, thêm một hằng số 0xff vào v_2.
	// Cho sipround chạy liên tục 4 lần để các bit được trộn lẫn.
	// Cuối cùng, gom 4 biến v_0, v_1, v_2, v_3,
	// XOR lại với nhau để ra số 64-bit cuối cùng.

	b |= t;
	v3 ^= b;
	for (int i = 0; i < 2; ++i) sipround(v0, v1, v2, v3);
	v0 ^= b;

	v2 ^= 0xff;
	for (int i = 0; i < 4; ++i) sipround(v0, v1, v2, v3);

	return v0 ^ v1 ^ v2 ^ v3;
}
} // namespace kallisto
