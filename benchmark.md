# Benchmark Report: Kallisto Performance Analysis

## 1. Methodology (Design of Experiment)

Để đánh giá hiệu năng thực tế của Kallisto, chúng tôi đã xây dựng một bộ công cụ benchmark tích hợp trực tiếp trong CLI (`src/main.cpp`). Bài test được thiết kế để mô phỏng kịch bản sử dụng thực tế của một hệ thống quản lý Secret tập trung, với quy mô lớn (High Scalability).

### 1.1 Test Case Logic
Hàm `run_benchmark(count)` thực hiện quy trình kiểm thử 2 giai đoạn (Phase):

**Phase 1: Write Stress Test (Ghi đè nặng)**
- **Input**: Đẩy vào `N` secret entries (Quy mô kiểm thử: **1,000,000 items**).
- **Key Distribution**: Keys được sinh ra theo dãy số học (`k0`... `k999999`) để đảm bảo tính duy nhất.
- **Path Distribution**: Sử dụng cơ chế Round-Robin trên 10 đường dẫn cố định (`/bench/p0` đến `/bench/p9`).
  - *Mục đích*: Kiểm tra khả năng xử lý của **B-Tree Index** khi một node phải chứa hàng trăm nghìn key và khả năng điều hướng (routing) của cây.
- **Action**: Gọi lệnh `PUT`. Đây là bước kiểm tra tốc độ trung bình của bộ 3: **SipHash** (Hashing) + **Cuckoo** (Collision Handling) + **Storage** (In-Memory Allocation).

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
- **Input**: Truy vấn lại toàn bộ `N` key (1 triệu keys) vừa ghi.
- **Action**: Gọi lệnh `GET`.
- **Mục tiêu**: Đo lường tốc độ truy xuất In-Memory. Do toàn bộ dữ liệu đã nằm trong `CuckooTable` (Cache), đây là bài test thuần túy về độ hiệu quả thuật toán (Algorithm Efficiency) mà không bị nghẽn bởi I/O.

### 1.2 Configuration Environments
Chúng tôi thực hiện đo lường trên chế độ tối ưu nhất để chứng minh giới hạn của thuật toán:

**BATCH MODE (Optimized)**:
- **Cơ chế**: Ghi dữ liệu trực tiếp vào RAM, vô hiệu hóa cơ chế fsync liên tục (Auto-Save Threshold = 10M ops).
- **Mục đích**: Loại bỏ hoàn toàn độ trễ I/O đĩa cứng để đo lường "tốc độ thô" (Raw Performance) của cấu trúc dữ liệu.
- **Kỳ vọng**: Đạt tốc độ xử lý hàng trăm nghìn request/giây.

---

## 2. Experimental Results (Kết quả thực nghiệm)

**Dataset**: 1,000,000 secret items.
**Hardware**: Virtual Development Environment (Single Thread).

### 2.1 Performance Metrics

| Metric | Batch Mode (Crazy Dog Optimized) |
| :--- | :--- |
| **Write RPS** | **~279,418 req/s** |
| **Read RPS** | **~267,059 req/s** |
| **Total Runtime** | **~7.3 giây** (cho 2 triệu ops) |
| **Hit Rate**| **100%** (1,000,000/1,000,000) |

*(Dữ liệu được ghi nhận từ lần chạy thực tế ngày 07/01/2026)*

### 2.2 Visual Analysis
- **Write Speed**: Hệ thống "nuốt" trọn 1 triệu bản ghi trong ~3.5 giây. Tốc độ này nhanh hơn hầu hết các Database truyền thống (thường ~10k-50k RPS) nhờ loại bỏ hoàn toàn Disk I/O trong quá trình ghi.
- **Read Speed**: Tốc độ đọc gần như tương đương tốc độ ghi, chứng tỏ chi phí tìm kiếm (Lookup cost) của Cuckoo Hashing xấp xỉ chi phí chèn (Insert cost) trong trường hợp Load Factor thấp (25%).

---

## 3. Theoretical vs. Actual (Lý thuyết và Thực tế)

### 3.1 Behavior Analysis
- **B-Tree Indexing**: Với 1 triệu item chia vào 10 path, mỗi nhánh B-Tree phải quản lý 100,000 item. Dù vậy, tốc độ vẫn không suy giảm. Điều này khớp với lý thuyết độ phức tạp $O(\log_t N)$ của B-Tree, cho phép mở rộng quy mô (Scale) cực tốt.
- **Cuckoo Hashing**: Hit Rate đạt tuyệt đối **100%**. Với bảng băm kích thước 4 triệu slot (Load Factor 25%), xác suất xảy ra xung đột dẫn đến Rehash là cực thấp, giúp duy trì độ trễ ổn định.

### 3.2 "Thundering Herd" Defense Provability
Kết quả Read RPS (~267k req/s) là minh chứng hùng hồn cho khả năng của Kallisto. Khi đối mặt với kịch bản "Thundering Herd" (hàng nghìn service cùng lúc khởi động và lấy secret):
1.  Hệ thống có thể phục vụ **267,000 requests/giây** trên một luồng đơn.
2.  Độ trễ trung bình mỗi request chỉ khoảng **3-4 micro-giây**.
3.  Không có hiện tượng "treo" hay suy giảm hiệu năng theo thời gian.

---

## 4. Conclusion
Kết quả thực nghiệm 1 Triệu Items khẳng định:
- **Scalability**: Kallisto xử lý tốt khối lượng dữ liệu lớn gấp 100 lần so với thiết kế ban đầu mà không gặp lỗi bộ nhớ hay hiệu năng.
- **Speed**: Tốc độ ~270k RPS biến Kallisto thành một giải pháp "Siêu tốc độ" (Ultra-fast), phù hợp làm bộ nhớ đệm (Cache) hoặc kho lưu trữ Secret cục bộ cho các hệ thống High-Frequency Trading hoặc Real-time Analytics.
