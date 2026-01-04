# 📅 Kallisto Project - MVP Battle Plan (12 Days)

## 🏗️ GIAI ĐOẠN 1: CORE DEVELOPMENT (27/12 - 30/12)
*Mục tiêu: Xây dựng xong "cỗ máy" lưu trữ.*

- [x] **Ngày 1 (27/12): Architecture & Skeleton** (Hôm nay)
    - [x] Setup cấu trúc Project (CMake, folder `src`, `include`, `tests`).
    - [x] Định nghĩa Interface `KallistoServer`, `CuckooTable`, `BTreeIndex`.
    - [x] Thiết kế struct `SecretEntry`.
- [x] **Ngày 2 (28/12): Trụ cột 1 - SipHash (Security)**
    - [x] Implement thuật toán SipHash (chống Hash Flooding).
    - [x] Viết Unit Test cơ bản kiểm tra tính nhất quán của Hash (Đã có trong `tests/test_main.cpp`).
- [/] **Ngày 3 & 4 (29/12 - 30/12): Trụ cột 2 - Cuckoo Hashing (Performance)**
    - [x] Implement logic `insert()` với cơ chế "kicking" (đá key).
    - [x] Implement `lookup()` và `delete()` với độ phức tạp $O(1)$.
    - [SUSPENDED] Implement `rehash()` để tăng kích thước bảng khi đầy (Scope MVP: Return false khi đầy).
    - [x] **Review:** Tự tay code lại hàm `insert` 3 lần để thuộc logic cho buổi vấn đáp.

---

## 🌐 GIAI ĐOẠN 2: INTEGRATION & APPS (31/12 - 01/01)
*Mục tiêu: Kết nối các thành phần và làm cho nó "sống".*

- [x] **Ngày 5 (31/12): Trụ cột 3 - B-Tree Lite (Paths)**
    - [x] Xây dựng cấu trúc cây để quản lý thư mục (ví dụ: `/prod/db/`).
    - [x] Tích hợp B-Tree làm lớp validate đường dẫn trước khi tra cứu key.
- [ ] **Ngày 5.5 (01/01): Giai đoạn 2.5 - Double-Defense Persistence**
    - [x] **Primary:** Setup `/data/kallisto` làm storage gốc trên disk (`storage_engine.cpp`).
    - [x] **Optimization:** Implement Batch Sync Mode (Avoid `fsync` bottleneck on every write).
    - [SUSPENDED] **Secondary:** Implement Async Dispatcher để đẩy data sang Postgres "Bomb Shelter". (Skipped for MVP).
- [ ] **Ngày 6 (01/01): API Layer & Kaellir Agent**
    - [x] Viết API đơn giản cho Server (CLI Interactive Mode trong `main.cpp`).
    - [SUSPENDED] Code Agent `Kaellir` để giả lập client gửi request (Tích hợp lệnh `BENCH` vào CLI).

---

## 📈 GIAI ĐOẠN 3: DATA & WRITING (02/01 - 04/01)
*Mục tiêu: Biến code thành con số và nội dung báo cáo.*

- [x] **Ngày 7 (02/01): Benchmark (Tiền đề báo cáo)**
    - [x] Chạy benchmark đo RPS và Latency.
    - [x] So sánh với `std::map` để vẽ biểu đồ chênh lệch hiệu năng.
    - [x] Chụp lại tất cả các biểu đồ để đưa vào báo cáo (Xem `benchmark.md`).
- [ ] **Ngày 8 & 9 (03/01 - 04/01): Sprint Writing (Báo cáo 20 trang)**
    - [ ] Viết chương Lý thuyết (SipHash, Cuckoo, B-Tree).
    - [ ] Viết chương Triển khai (Code snippets + giải thích).
    - [ ] Viết chương Phân tích kết quả (Dùng dữ liệu Ngày 7).

---

## ⚔️ GIAI ĐOẠN 4: REFINEMENT & DEFENSE (05/01 - 07/01)
*Mục tiêu: Đạt trạng thái sẵn sàng chiến đấu.*

- [ ] **Ngày 10 (05/01): Refactor & Clean Code**
    - [ ] Kiểm tra memory leak, tối ưu `smart pointers`.
    - [ ] Viết comment giải thích (phòng trường hợp thầy đọc code trực tiếp).
- [ ] **Ngày 11 (06/01): Mock Defense & Video Demo**
    - [ ] Quay video demo giới thiệu tính năng MVP "Path-Based Retrieval".
    - [ ] Tự trả lời các câu hỏi về Big-O, Collision handling.
- [ ] **Ngày 12 (07/01): FINAL DEFENSE! 🚀**
    - [ ] Check lại laptop, sạc, file PDF báo cáo.

---

> [!TIP]
> **Chiến thuật "Code-to-Theory":** Mỗi khi code xong một phần (ví dụ Cuckoo Hash), hãy note lại ngay 3 ý chính tại sao nó nhanh. Việc này giúp bạn vừa code vừa ôn tập lý thuyết luôn, không đợi đến ngày cuối.
> **Performance Tip:** Khi demo benchmark, hãy chuyển sang `MODE BATCH` để đạt RPS cao nhất (> 50k), chứng minh thuật toán Cuckoo Hash nhanh thế nào khi không bị đĩa cứng kìm hãm.
