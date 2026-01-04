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

- [ ] **Ngày 10 (05/01): Presentation Prep**
    - [ ] Làm Slide Powerpoint (10 slides).
    - [ ] Demo script (quay video màn hình terminal).
    - [ ] Q&A Rehearsal (Chuẩn bị trả lời thầy cô).
- [ ] **Ngày 11 (06/01): Mock Defense & Video Demo**
    - [ ] Quay video demo giới thiệu tính năng MVP "Path-Based Retrieval".
    - [ ] Tự trả lời các câu hỏi về Big-O, Collision handling.
- [ ] **Ngày 12 (07/01): FINAL DEFENSE! 🚀**
    - [ ] Check lại laptop, sạc, file PDF báo cáo.

---

> [!TIP]
> **Chiến thuật "Code-to-Theory":** Mỗi khi code xong một phần (ví dụ Cuckoo Hash), hãy note lại ngay 3 ý chính tại sao nó nhanh. Việc này giúp bạn vừa code vừa ôn tập lý thuyết luôn, không đợi đến ngày cuối.
> **Performance Tip:** Khi demo benchmark, hãy chuyển sang `MODE BATCH` để đạt RPS cao nhất (> 50k), chứng minh thuật toán Cuckoo Hash nhanh thế nào khi không bị đĩa cứng kìm hãm.

---

## 🚀 FUTURE ROADMAP (System Design & Architecture Learning)

Phần này dành cho "Later Works" (sau đồ án), tập trung vào các kỹ thuật Software Architecture nâng cao để biến Kallisto thành một Production-Grade System.

### 1. Security Layer (Defense in Depth)
- [ ] **Encryption-at-Rest** (Mã hóa lưu trữ):
  - *Vấn đề*: File `kallisto.db` hiện tại lưu plaintext. Mất ổ cứng là mất hết.
  - *Giải pháp*: Tích hợp **AES-256-GCM**. Encrypt value trước khi ghi xuống đĩa. Chỉ giữ Master Key trên RAM.
  - *Bài học*: Key Management Life-cycle (Rotation, Unseal).

- [ ] **Secure Memory Allocator** (Bảo vệ RAM):
  - *Vấn đề*: Memory Dump hoặc Swap file có thể làm lộ secret.
  - *Giải pháp*: Implement custom allocator sử dụng `mlock()` (cấm swap) và `explicit_bzero` (xóa trắng RAM ngay khi free).
  - *Bài học*: OS Memory Management & Low-level Systems Programming.

- [ ] **Access Control List (ACL)** (Phân quyền):
  - *Vấn đề*: Ai có quyền truy cập CLI cũng đọc được mọi thứ.
  - *Giải pháp*: Thêm cơ chế Authentication (Token-based) và Authorization (Path-based Policy như Vault).
  - *Bài học*: RBAC Design Patterns.

### 2. Scalability & Reliability (Mở rộng & Tin cậy)
- [ ] **WAL (Write-Ahead Logging)**:
  - *Vấn đề*: Strict Mode quá chậm, Batch Mode rủi ro mất data.
  - *Giải pháp*: Ghi vào Append-Only Log file (tuần tự, log rotation) trước khi ghi vào RAM. Nếu crash, replay lại LOG.
  - *Bài học*: Cơ chế cốt lõi của mọi Database (Postgres, Redis AOF).

- [ ] **Network Interface (gRPC/HTTP API)**:
  - *Vấn đề*: Hiện tại chỉ dùng CLI cục bộ (Unix Pipe).
  - *Giải pháp*: Viết một lớp Wrapper (Adapter Pattern) expose API qua HTTP/2 (gRPC) để các service khác gọi vào.
  - *Bài học*: API Design, Distributed Systems Communication.

- [ ] **Replication (Raft Consensus)**:
  - *Vấn đề*: Single Point of Failure. Server chết là hệ thống dừng.
  - *Giải pháp*: Dựng Cluster 3 node, dùng thuật toán Raft để bầu Leader.
  - *Bài học*: Distributed Consensus (Đỉnh cao của System Design).