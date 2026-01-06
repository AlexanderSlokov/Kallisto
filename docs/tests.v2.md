# 1. Chỉ số Độ trễ (Latency p99)

Chỉ số này dùng để chứng minh tính ổn định hiệu năng của hệ thống và là bằng chứng trực tiếp cho lợi ích của **Cuckoo Hashing** (O(1) worst-case lookup).

| Mục tiêu           | Chi tiết Thực nghiệm                                                                                                       | Dữ liệu Cần Thu Thập                                                                 |
| :----------------- | :------------------------------------------------------------------------------------------------------------------------- | :----------------------------------------------------------------------------------- |
| **Yêu cầu**        | **p99 \< 1ms**                                                                                                             | So sánh p99 của Kallisto với Chaining Hashmap truyền thống.                          |
| **Thiết lập Test** | **1. Tiền tải dữ liệu:** Chèn 10,000 secret hợp lệ vào Cuckoo Table.                                                       | Thời gian tra cứu (Latency) cho **mỗi request** (đơn vị: micro giây hoặc nano giây). |
|                    | **2. Kịch bản:** Chạy 10,000 yêu cầu **GET (tra cứu)** ngẫu nhiên.                                                         | **Tổng thời gian chạy** (Total runtime) của 10,000 requests.                         |
|                    | **3. Đo lường:** Ghi lại thời gian hoàn thành (end-to-end latency) của từng yêu cầu.                                       |                                                                                      |
| **Phân tích**      | Sau khi thu thập 10,000 mẫu thời gian: Sắp xếp các mẫu theo thứ tự tăng dần và tìm giá trị ở vị trí 99% (99th percentile). | **Giá trị p99 (phân vị 99) của độ trễ** (phải nhỏ hơn 1ms).                          |

-----

### 2. Bằng chứng Bảo mật (SipHash) - Chống Tấn công Hash Flooding

Bài test này dùng để chứng minh rằng việc sử dụng **SipHash (PRF) thay thế cho hàm băm yếu** sẽ ngăn chặn sự suy giảm hiệu năng DoS do va chạm (collision).

| Mục tiêu                                             | Chi tiết Thực nghiệm                                                                                                                                    | Dữ liệu Cần Thu Thập                                                           |
| :--------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------ | :----------------------------------------------------------------------------- |
| **Yêu cầu**                                          | **Phải ngăn chặn Hash Flooding Attacks.**                                                                                                               | So sánh **Độ trễ trung bình (Avg. Latency)** và **Độ trễ p99** của 2 kịch bản. |
| **Kịch bản 1: Baseline (SipHash – An toàn)**         | **1. Cấu hình:** Sử dụng **SipHash** như trong Kallisto.                                                                                                |                                                                                |
|                                                      | **2. Thực hiện:** Chèn ngẫu nhiên 10,000 keys và đo thời gian.                                                                                          | **Latency Chèn/Tra cứu (Bình thường)**.                                        |
| **Kịch bản 2: Tấn công Giả lập (Hash Function Yếu)** | **1. Cấu hình:** **Thay thế SipHash bằng một hàm băm non-cryptographic yếu** (ví dụ: một hàm băm đơn giản chỉ xét 8-16 byte đầu tiên của key, hoặc dùng |                                                                                |

``` 
std::hash 
```

chuẩn). | **Latency Chèn/Tra cứu (Tấn công)**. |
| | **2. Thực hiện:** Tạo 10,000 keys **được thiết kế để gây va chạm** (ví dụ: các keys chỉ khác nhau ở các byte mà hàm băm yếu bỏ qua) và chèn chúng. | |
| **Phân tích** | **SipHash phải cho kết quả Latency ổn định** trong kịch bản 2, trong khi Hash Function yếu sẽ cho kết quả Latency tăng vọt (đặc biệt là p99). | Tốc độ chèn/tra cứu **ổn định** của Kallisto khi dùng SipHash là bằng chứng chống DoS. |

-----

### 3. Hiệu quả B-Tree - Tốc độ Validate Path

Bài test này chứng minh **B-Tree** đóng vai trò "cổng chặn" hiệu quả, loại bỏ các yêu cầu không hợp lệ nhanh chóng.

| Mục tiêu           | Chi tiết Thực nghiệm                                                                                                                                                              | Dữ liệu Cần Thu Thập                                          |
| :----------------- | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------ |
| **Yêu cầu**        | **Validate Path** với $O(\\log N)$ phải hiệu quả và nhanh chóng.                                                                                                                  | Số requests bị chặn (Block RPS) và Latency cho thao tác chặn. |
| **Thiết lập Test** | **1. Tiền tải dữ liệu:** Chèn một số lượng lớn paths hợp lệ (ví dụ: 10,000) vào B-Tree Index.                                                                                     | Thời gian hoàn thành (end-to-end latency) cho 10,000 yêu cầu. |
|                    | **2. Kịch bản:** Chạy 10,000 yêu cầu **Validate Path** với các paths **không hợp lệ** (không tồn tại trong B-Tree).                                                               |                                                               |
| **Phân tích**      | **Mục tiêu là chứng minh B-Tree có thể chặn 10,000 requests không hợp lệ với Latency rất thấp**, trước khi chúng kịp kích hoạt các quy trình SipHash và Cuckoo Table tốn kém hơn. | **Latency p99** của thao tác                                  |

``` 
Flow Validate Path 
```

(phải cực kỳ thấp, dưới 100 micro giây nếu có thể) để chứng minh hiệu quả của **"gate"** B-Tree. |

