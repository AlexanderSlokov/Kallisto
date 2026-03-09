# Kế hoạch Tối ưu hóa Bảng Cuckoo

## Mô tả Mục tiêu

Nâng cấp hiệu năng và hiệu quả bộ nhớ của CuckooTable bằng cách chuyển sang Blocked Cuckoo Hash (8 slots mỗi bucket) sử dụng Tham chiếu Index 32-bit (Nén). Thay đổi này nhằm mục đích:

- Khớp hoàn hảo với Cache Line 64-byte: 8 slots * (4B Tag + 4B Index) = 64 Bytes.
- Tối đa hóa Load Factor: Đạt tới 99% dung lượng sử dụng nhờ có 8 sự lựa chọn.
- Hiệu quả Bộ nhớ Cực đoan: Sử dụng một vùng nhớ liên tục (arena) và index 32-bit (hỗ trợ ~4 tỷ phần tử).

## Yêu cầu Người dùng Xem xét

### IMPORTANT

> Kiến trúc này giới thiệu một Pool quản lý bộ nhớ tùy chỉnh. Memory Reservation: initial_capacity cần được thiết lập đúng ngay khi khởi tạo để tránh việc std::vector phải cấp phát lại bộ nhớ (rất tốn kém). Giới hạn: Tối đa ~4 tỷ phần tử cho mỗi bảng (do giới hạn của index 32-bit).

## Thay đổi Đề xuất

### include/kallisto/cuckoo_table.hpp

```cpp
[MODIFY] 
cuckoo_table.hpp
Định nghĩa struct 
Bucket
:

struct alignas(64) Bucket {
    struct Slot {
        uint32_t tag;   // Dấu vân tay (Fingerprint)
        uint32_t index; // Index trỏ vào vector lưu trữ (0xFFFFFFFF = rỗng)
    } slots[8];
};
```

### Quản lý Bộ nhớ (Storage Man	agement)

```cpp
std::vector<SecretEntry> storage; // Vùng nhớ cấp phát trước (Slab)
std::vector<uint32_t> free_list; // Stack (LIFO) để tái sử dụng index, tăng tối đa cache locality.
uint32_t next_free_index; // Mốc đánh dấu phần tử chưa dùng nếu free_list rỗng.
```

### Cập nhật Constructor

```cpp
CuckooTable(size_t size = 1024, size_t initial_capacity = 1024);
```

```cpp
src/cuckoo_table.cpp
[MODIFY] 
cuckoo_table.cpp
```

Constructor: Gọi storage.reserve(initial_capacity) ngay lập tức để tránh invalidation con trỏ và copy overhead.
Insert Logic: Sinh Tag: Lấy các bit cao (high-bits) của SipHash 64-bit (hoặc hash thứ cấp) để làm tag, đảm bảo độ hỗn loạn (entropy) tối đa, độc lập với Bucket Index.
Free List: Lấy từ free_list (LIFO) nếu có, nếu không thì dùng next_free_index.
Bucket Insert: Quét 8 slots trong bucket. Nếu tìm thấy slot trống, ghi index vào.
Kick (Đá): Tráo đổi cặp (Tag, Index) trong các bucket. KHÔNG cần di chuyển dữ liệu gốc (dữ liệu vẫn nằm yên tại index đó). Việc này cực nhanh vì chỉ tráo các số nguyên.
Lookup Logic: SIMD Hint: Viết vòng lặp đơn giản for (int i = 0; i < 8; ++i) để trình biên dịch tự động vector hóa (auto-vectorization).
Thêm comment: // TODO: AVX2 Optimization using _mm256_cmpeq_epi32
Remove Logic:

Đánh dấu slot là rỗng (0xFFFFFFFF).
Đẩy index vừa xóa vào free_list (LIFO) để tái sử dụng ngay.

## Kế hoạch Kiểm thử

### Kiểm thử Tự động

Tạo/Cập nhật file kiểm thử để xác minh logic 8-slot.

Xác minh:

Insert N phần tử (với N > 95% sức chứa).
Kiểm tra Lookup trả về đúng dữ liệu.
Kiểm tra Remove hoạt động đúng (slot trống, index vào free list).
Kiểm tra cơ chế "Kick" (đẩy bảng lên load factor cao).