# 📅 Kallisto Project - MVP Battle Plan (12 Days)

Dựa trên yêu cầu của bạn, đây là bảng `TODO.md` chi tiết để theo dõi tiến độ mỗi ngày. Mục tiêu là hoàn thành MVP "Path-Based Secret Retrieval" và có bộ số liệu "đẹp" cho báo cáo.

---

## 🏗️ GIAI ĐOẠN 1: CORE DEVELOPMENT (27/12 - 30/12)
*Mục tiêu: Xây dựng xong "cỗ máy" lưu trữ.*

- [x] **Ngày 1 (27/12): Architecture & Skeleton** (Hôm nay)
    - [x] Setup cấu trúc Project (CMake, folder `src`, `include`, `tests`).
    - [x] Định nghĩa Interface `KallistoServer`, `CuckooTable`, `BTreeIndex`.
    - [x] Thiết kế struct `SecretEntry`.
- [ ] **Ngày 2 (28/12): Trụ cột 1 - SipHash (Security)**
    - [ ] Implement thuật toán SipHash (chống Hash Flooding).
    - [ ] Viết Unit Test cơ bản kiểm tra tính nhất quán của Hash.
- [ ] **Ngày 3 & 4 (29/12 - 30/12): Trụ cột 2 - Cuckoo Hashing (Performance)**
    - [ ] Implement logic `insert()` với cơ chế "kicking" (đá key).
    - [ ] Implement `lookup()` và `delete()` với độ phức tạp $O(1)$.
    - [ ] **Review:** Tự tay code lại hàm `insert` 3 lần để thuộc logic cho buổi vấn đáp.

---

## 🌐 GIAI ĐOẠN 2: INTEGRATION & APPS (31/12 - 01/01)
*Mục tiêu: Kết nối các thành phần và làm cho nó "sống".*

- [ ] **Ngày 5 (31/12): Trụ cột 3 - B-Tree Lite (Paths)**
    - [ ] Xây dựng cấu trúc cây để quản lý thư mục (ví dụ: `/prod/db/`).
    - [ ] Tích hợp B-Tree làm lớp validate đường dẫn trước khi tra cứu key.
- [ ] **Ngày 6 (01/01): API Layer & Kaellir Agent**
    - [ ] Viết API đơn giản cho Server (nhận command line/socket).
    - [ ] Code Agent `Kaellir` để giả lập client gửi request.

---

## 📈 GIAI ĐOẠN 3: DATA & WRITING (02/01 - 04/01)
*Mục tiêu: Biến code thành con số và nội dung báo cáo.*

- [ ] **Ngày 7 (02/01): Benchmark (Tiền đề báo cáo)**
    - [ ] Chạy benchmark đo RPS và Latency.
    - [ ] So sánh với `std::map` để vẽ biểu đồ chênh lệch hiệu năng.
    - [ ] Chụp lại tất cả các biểu đồ để đưa vào báo cáo.
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
