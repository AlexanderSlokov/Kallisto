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
	uint64_t state0 = 0x736f6d6570736575ULL ^ first_part; // "somepseu"
	uint64_t state1 = 0x646f72616e646f6dULL ^ second_part; // "dorandom"
	uint64_t state2 = 0x6c7967656e657261ULL ^ first_part; // "lygenera"
	uint64_t state3 = 0x7465646279746573ULL ^ second_part; // "tedbytes"

	const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(input.data());
	size_t input_len = input.length();
	const uint8_t* full_blocks_end = data_ptr + (input_len & ~7);
	int remainder_size = input_len & 7;
	uint64_t final_block = static_cast<uint64_t>(input_len) << 56;

	// 2. Compression Loop (Vòng lặp nén)
	// Cắt input thành từng block 8 bytes (64-bit) để xử lý.
	// Với mỗi block 64-bit 'message_word':
	// - XOR 'message_word' vào state3 (Nạp dữ liệu vào trạng thái)
	// - Chạy 2 vòng sipround (Xáo trộn)
	// - XOR 'message_word' vào state0 (Khóa dữ liệu lại)
	for (; data_ptr < full_blocks_end; data_ptr += 8) {
		uint64_t message_word;
		std::memcpy(&message_word, data_ptr, 8);
		state3 ^= message_word;
		for (int i = 0; i < 2; ++i) sipround(state0, state1, state2, state3);
		state0 ^= message_word;
	}

	// Nếu chuỗi không chia hết cho 8 thì ta chỉ việc dùng switch-case để nhặt nốt những byte bị chia dư ra cuối cùng.
	// Đặc biệt, độ dài của chuỗi được gán vào byte cao nhất để đảm bảo chuỗi abc và abc\0 sẽ cho ra mã băm khác hẳn nhau.
	uint64_t remainder_word = 0;
	switch (remainder_size) {
		case 7: remainder_word |= static_cast<uint64_t>(data_ptr[6]) << 48; [[fallthrough]];
		case 6: remainder_word |= static_cast<uint64_t>(data_ptr[5]) << 40; [[fallthrough]];
		case 5: remainder_word |= static_cast<uint64_t>(data_ptr[4]) << 32; [[fallthrough]];
		case 4: remainder_word |= static_cast<uint64_t>(data_ptr[3]) << 24; [[fallthrough]];
		case 3: remainder_word |= static_cast<uint64_t>(data_ptr[2]) << 16; [[fallthrough]];
		case 2: remainder_word |= static_cast<uint64_t>(data_ptr[1]) << 8; [[fallthrough]];
		case 1: remainder_word |= static_cast<uint64_t>(data_ptr[0]); break;
		case 0: break;
	}
	// Sau khi băm xong dữ liệu, thêm một hằng số 0xff vào state2.
	// Cho sipround chạy liên tục 4 lần để các bit được trộn lẫn.
	// Cuối cùng, gom 4 biến state0, state1, state2, state3,
	// XOR lại với nhau để ra số 64-bit cuối cùng.

	final_block |= remainder_word;
	state3 ^= final_block;
	for (int i = 0; i < 2; ++i) sipround(state0, state1, state2, state3);
	state0 ^= final_block;

	state2 ^= 0xff;
	for (int i = 0; i < 4; ++i) sipround(state0, state1, state2, state3);

	return state0 ^ state1 ^ state2 ^ state3;
}
} // namespace kallisto
