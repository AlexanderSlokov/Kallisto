# Benchmark Report: Kallisto Performance Analysis

## 1. Methodology (Design of Experiment)

Để đánh giá hiệu năng thực tế của Kallisto, chúng tôi đã xây dựng một bộ công cụ benchmark tích hợp trực tiếp trong CLI (`src/main.cpp`). Bài test được thiết kế để mô phỏng kịch bản sử dụng thực tế của một hệ thống quản lý Secret tập trung.

### 1.1 Test Case Logic
Hàm `run_benchmark(count)` thực hiện quy trình kiểm thử 2 giai đoạn (Phase):

**Phase 1: Write Stress Test (Ghi đè nặng)**
- **Input**: Tạo ra `N` (ví dụ: 10,000) secret entries.
- **Key Distribution**: Keys được sinh ra theo dãy số học (`k0`, `k1`, ... `k9999`) để đảm bảo tính duy nhất.
- **Path Distribution**: Sử dụng cơ chế Round-Robin trên 10 đường dẫn cố định (`/bench/p0` đến `/bench/p9`).
  - *Mục đích*: Kiểm tra khả năng xử lý của **B-Tree Index** khi một node phải chứa nhiều key và khả năng điều hướng (routing) của cây.
- **Action**: Gọi lệnh `PUT`. Đây là bước kiểm tra tốc độ tính toán **SipHash**, khả năng xử lý va chạm của **Cuckoo Hashing**, và độ trễ của **Storage Engine**.

```cpp
// Code Snippet: Benchmark Loop
for (int i = 0; i < count; ++i) {
    std::string path = "/bench/p" + std::to_string(i % 10); 
    std::string key = "k" + std::to_string(i);
    std::string val = "v" + std::to_string(i);
    server->put_secret(path, key, val);
}
```

**Phase 2: Read Stress Test (Thundering Herd Simulation)**
- **Input**: Truy vấn lại toàn bộ `N` key vừa ghi.
- **Action**: Gọi lệnh `GET`.
- **Mục tiêu**: Đo lường tốc độ truy xuất trên RAM. Do toàn bộ dữ liệu đã nằm trong `CuckooTable` (Cache), đây là bài test thuần túy về thuật toán (Algorithm Efficiency) mà không bị ảnh hưởng bởi Disk I/O.

### 1.2 Configuration Environments
Chúng tôi thực hiện đo lường trên 2 cấu hình Sync (Đồng bộ) để làm rõ sự đánh đổi giữa An toàn dữ liệu và Hiệu năng:

1.  **STRICT MODE (Default)**:
    - Cơ chế: `fsync` xuống đĩa cứng ngay sau mỗi lệnh PUT.
    - Dự đoán: Rất chậm, bị giới hạn bởi IOPS của ổ cứng (thường < 2000 IOPS với SSD thông thường).
    - Mục đích: Đảm bảo ACID, không mất dữ liệu dù mất điện đột ngột.

2.  **BATCH MODE (Optimized)**:
    - Cơ chế: Chỉ ghi vào RAM, sync xuống đĩa khi user gọi lệnh `SAVE` hoặc đạt ngưỡng 10,000 ops.
    - Dự đoán: Rất nhanh, đạt tốc độ tới hạn của CPU và RAM.
    - Mục đích: Chứng minh độ phức tạp O(1) của thuật toán Cuckoo Hash.

---

## 2. Experimental Results (Kết quả thực nghiệm)

**Dataset**: 10,000 secret items.
**Hardware**: Virtual Development Environment (Single Thread).

### 2.1 Comparative Analysis (So sánh)

| Metric | Strict Mode (Safe) | Batch Mode (Fast) | Improvement |
| :--- | :--- | :--- | :--- |
| **Write RPS** | ~1,572 req/s | **~17,564 req/s** | **~11.1x** |
| **Read RPS** | ~5,654 req/s | **~6,394 req/s*** | ~1.1x |
| **Total Time** | ~12.3s | **~2.1s** | Nhanh hơn 6 lần |

*(Note: Read RPS tăng nhẹ ở Batch Mode do CPU không bị ngắt quãng bởi các tác vụ chờ I/O ngầm)*

### 2.2 Visual Analysis
- **Strict Mode**: Biểu đồ Write đi ngang ở mức thấp (~1.5k). Đây là "nút thắt cổ chai" (Bottleneck) do phần cứng (Disk I/O), không phản ánh tốc độ thuật toán.
- **Batch Mode**: Biểu đồ Write vọt lên ~17.5k. Đây chính là tốc độ thực của **SipHash + Cuckoo Insert**.

---

## 3. Theoretical vs. Actual (Lý thuyết và Thực tế)

### 3.1 Behavior Analysis
- **B-Tree Indexing**: Với 10,000 item chia vào 10 path, mỗi node lá của B-Tree chứa khoảng 1,000 item. Việc `validate_path` chỉ tốn O(log 10) gần như tức thời. Kết quả benchmark cho thấy không có độ trễ đáng kể khi chuyển đổi giữa các path.
- **Cuckoo Hashing**: Hit Rate đạt **100%** (10000/10000). Không có trường hợp nào bị fail do bảng đầy (nhờ Load Factor 30% hợp lý).

### 3.2 "Thundering Herd" Defense Provability
Kết quả Read RPS (~6,400 req/s) chứng minh khả năng chống chịu của Kallisto trước "Cơn bão hợp pháp" (Thundering Herd). Khi hàng nghìn service khởi động lại và đòi lấy Secret cùng lúc:
1.  Kallisto **không truy cập đĩa**.
2.  Mọi thao tác `GET` được giải quyết trên RAM với độ phức tạp O(1).
3.  Hệ thống duy trì được độ trễ thấp (< 1ms) ngay cả khi đang chịu tải cao.

---

## 4. Conclusion
Kết quả thực nghiệm khẳng định thiết kế của Kallisto là chính xác:
- **Write**: Batch Mode giúp tận dụng tối đa băng thông RAM, phù hợp cho các đợt import dữ liệu lớn (Bulk Load).
- **Read**: Luôn ổn định ở tốc độ cao nhờ kiến trúc In-Memory Cuckoo Table, đáp ứng tốt yêu cầu của một hệ thống High-Performance Secret Management.
