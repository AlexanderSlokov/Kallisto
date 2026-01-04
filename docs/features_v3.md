# Kallisto Features (Prototype v3)

Tổng hợp tính năng của hệ thống tính đến phiên bản Prototype 3 (04/01/2026).

## 1. Core Engine (Nhân Xử Lý)
- **Cuckoo Hashing Storage**:
  - Sử dụng 2 bảng băm (Tables) với 2 hàm hash khác nhau.
  - Cơ chế "Kick-out" (Đá khóa) giải quyết va chạm (Collision Resolution).
  - Đảm bảo độ phức tạp truy xuất (Lookup) là **O(1)** trong Worst-case.
  - Load Factor tối ưu: ~30% (Test với 16,384 slots / 10,000 items).

- **SipHash-2-4 Security**:
  - Hàm băm mật mã học (Cryptographic Hash Function).
  - Sử dụng 128-bit Secret Key để chống tấn công Hash Flooding (DoS).
  - Đạt chuẩn bảo mật tương đương Linux Kernel hash.

## 2. Structural Validation (Kiểm Soát Cấu Trúc)
- **B-Tree Path Indexing**:
  - Quản lý danh sách đường dẫn (Paths) theo cấu trúc cây (không phẳng như Redis).
  - **Fail-Fast**: Từ chối request ngay lập tức nếu đường dẫn không tồn tại (Validation O(log N)).
  - Hỗ trợ High Concurrency read tốt hơn việc scan key phẳng.

## 3. Persistence Layer (Lưu Trữ Bền Vững)
- **Custom Binary Packing**:
  - File định dạng `/data/kallisto/kallisto.db` (Magic Header `KALL`, Versioning).
  - Tối ưu hóa kích thước file (nhỏ hơn JSON/Text).
  - **Snapshotting**: Load/Save toàn bộ trạng thái RAM xuống đĩa 1 lần.

- **Dual Sync Strategies (Mới ở v3)**:
  1.  **STRICT Mode (Safe)**: Gọi `fsync` ngay lập tức sau mỗi lệnh Write. Đảm bảo ACID, zero data loss. Tốc độ ~1,500 RPS.
  2.  **BATCH Mode (Fast)**: Chỉ ghi RAM, sync xuống đĩa khi đạt ngưỡng (10,000 ops) hoặc lệnh thủ công. Tốc độ RAM thuần túy ~17,000+ RPS.

## 4. User Interface (Giao Diện)
- **Interactive CLI**:
  - Giao diện dòng lệnh tương tác (REPL).
  - Logging được làm đẹp (Info/Warn/Debug) tách biệt với luồng dữ liệu.
- **Commands**:
  - `PUT /path key val`: Ghi secret.
  - `GET /path key`: Đọc secret.
  - `DEL /path key`: Xóa secret.
  - `MODE <STRICT|BATCH>`: Chuyển đổi chế độ Sync nóng (Runtime).
  - `SAVE`: Ghi đĩa thủ công (Force Flush).
  - `BENCH <count>`: Chạy bài test hiệu năng tích hợp.

## 5. System Quality
- **Unit Testing**: Bộ test bao phủ logic SipHash và Cuckoo Table.
- **Clean Architecture**: Tách biệt rõ ràng các tầng: API -> Logic (Server) -> Indices (BTree/Cuckoo) -> Persistence (Storage).
- **No External Dependencies**: Code thuần C++ (STL), không phụ thuộc thư viện thứ 3 nặng nề.
