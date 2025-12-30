# kallisto
Kallisto is a Vault-compatible storage management, wrote by C++

# Các giải thuật được sử dụng

## 1. Universal Hashing & SipHash

Chức năng: Dùng để băm các khóa (Key/Secret Name)

Tại sao dùng: Một secret management system phải đối mặt với nguy cơ bị tấn công từ chối dịch vụ (DoS). Nếu dùng hàm băm thông thường, kẻ tấn công có thể tạo ra hàng loạt key gây trùng lặp (Hash Flooding) làm treo hệ thống.

Ứng dụng: Triển khai SipHash với một "secret key" `KALLISTO_SIPHASH_SECRET_KEY` để đảm bảo các bảng băm miễn nhiễm với các cuộc tấn công hash flooding.

### Nhưng tại sao lại phải là SipHash?

SipHash sử dụng kiến trúc đảo bit (Add-Rotate-XOR) để tạo ra các bit nhiễu (noise bit) trong quá trình băm mà không tiêu tốn quá nhiều tài nguyên tính toán của CPU (việc xoay bit chỉ diễn ra trong một clock cycle nên rất nhanh và hiệu quả trong một chu kỳ của CPU).

## 2. Cuckoo Hashing

Chức năng: Lưu trữ các secret trong RAM để truy xuất tức thì. Bắt chước theo Hashicorp Vault: xin trước luôn luôn vùng nhớ RAM để không cho các thread khác truy cập. 

Tại sao dùng: Cuckoo Hashing đảm bảo việc tìm kiếm luôn là O(1) trong trường hợp xấu nhất (Worst-case).

Ứng dụng: Làm "Secret Cache". Khi ứng dụng cần lấy mật khẩu, Kallisto sẽ trả về kết quả ngay lập tức mà không có độ trễ biến thiên, vì Cuckoo Hash không bao giờ bị tình trạng "dây chuyền" quá dài khi xung đột xảy ra.

## 3. B-Trees & Disk-Optimized Storage

Chức năng: Quản lý cấu trúc cây thư mục (Path-based secrets) ý như các API của Hashicorp Vault.

Tại sao dùng: Một secret management system không chỉ lưu trong RAM mà phải lưu xuống đĩa cứng (persistent storage). B-Tree tối ưu số lần đọc/ghi (I/O).

Ứng dụng: Dùng B-Tree để lưu trữ toàn bộ cây phân cấp các secret, thực hiện các truy vấn theo tiền tố (prefix search) cực nhanh, ví dụ: "Lấy tất cả secret trong thư mục /prod".

---

# Mục tiêu benchmark

## 1. Chỉ số "Code Xịn" (Benchmark Targets)

Với một server C++ tối ưu, bạn nên hướng tới các mốc sau:

- RPS (Requests Per Second): > 50,000 req/s.   
Tại sao: Vì bạn dùng Cuckoo Hashing (O(1) worst-case). Với các request tra cứu Secret đơn giản, CPU chỉ mất vài micro giây để tìm thấy dữ liệu. Nếu con số này dưới 10k, thầy sẽ đặt câu hỏi về việc bạn có đang gặp lỗi I/O hay không.

- Latency (Độ trễ): p99 < 1ms (Sub-millisecond).
Tại sao: Cache Locality (Day 3). Việc sắp xếp các bucket của bảng băm nằm liên tục trong bộ nhớ giúp CPU không bị "cache miss", dẫn đến độ trễ cực thấp.

- Locust CCU (Concurrent Users): 500 - 1,000 CCU.
Tại sao: Con số này chứng minh khả năng quản lý Call Stacks và tài nguyên hệ thống của bạn tốt, không bị tràn bộ nhớ hay deadlock khi nhiều Agent (Kaellir) gọi tới cùng lúc.

---

# 2. Mô hình server Kallisto & Kaellir agent
Trong báo cáo 20 trang, bạn nên mô tả mô hình này như một giải pháp Sidecar Injection thực thụ:
Thành phần
Vai trò DSA
Nhiệm vụ cụ thể
Kallisto (Server)
Cuckoo Hash 8& B-Tree 9
Lưu trữ secrets. Trả về credential cực nhanh nhờ tra cứu hằng số O(1).
Kaellir (Agent)
Mocking Client
Giả lập sidecar gửi yêu cầu tra cứu key (ví dụ: GET /secret/db-pass).
Locust (Tester)
Asymptotic Analysis 11
Đo lường thực tế để vẽ biểu đồ so sánh lý thuyết Big-O với hiệu năng thực tế.

---

# Lịch trình cho dự án **Kallisto**.

---

## GIAI ĐOẠN 1: KHỞI TẠO & CODE CORE (HẠ TẦNG)

*Mục tiêu: Xây dựng xong MVP của hệ thống (Cuckoo Hash & B-Tree cơ bản).*

**Ngày 1: 27/12 (Hôm nay) - Architecture & Skeleton**

* **Nhiệm vụ:**
* Setup Git repo, `CMakeLists.txt` (hoặc `Makefile`).
* Tạo file header định nghĩa Class: `KallistoServer`, `CuckooTable`, `BTreeIndex`.
* Định nghĩa các `struct` dữ liệu: `SecretEntry` (gồm key, value, ttl).


* **Output:** Project compile được (dù chưa chạy logic gì). File `design.md` ghi lại flow dữ liệu.

**Ngày 2: 28/12 - Trụ cột 1: SipHash & Basic Hashing**

* **Lý thuyết:** Ôn lại Universal Hashing (Day 6).
* **Code:**
* Implement hàm `SipHash` (có thể copy reference implementation nhưng phải chuyển sang C++ style).
* Viết Unit Test nhỏ để đảm bảo hàm băm ra kết quả nhất quán.


* **Output:** Hàm `hash(key, seed)` chạy ngon lành.

**Ngày 3 & 4: 29/12 - 30/12 - Trụ cột 2: Cuckoo Hashing Logic (QUAN TRỌNG NHẤT)**

* **Lý thuyết:** Ôn cơ chế "Kicking" (đá key cũ sang bảng khác) và Cycle detection (phát hiện vòng lặp).
* **Code:**
* Implement `insert()`, `lookup()`, `delete()`.
* Dùng `std::vector` để làm bucket.


* **Luyện Live Code:** *Đây là phần thầy dễ bắt code lại nhất.* Hãy code đi code lại hàm `insert` ít nhất 3 lần cho thuộc logic "đá qua đá lại".
* **Output:** Một `CuckooMap` lưu được secret và tìm kiếm trong .

---

### GIAI ĐOẠN 2: MỞ RỘNG & TÍCH HỢP (ỨNG DỤNG)

*Mục tiêu: Biến các hàm rời rạc thành một Server có thể gọi được.*

**Ngày 5: 31/12 (Tết Dương lịch) - Trụ cột 3: B-Tree "Lite"**

* **Chiến thuật:** Đừng làm B-Tree full tính năng của Database. Hãy làm **B-Tree lưu Path**.
* **Code:**
* Node cấu trúc: Chứa danh sách keys và con trỏ con (dùng `std::unique_ptr`).
* Implement `search(path)` và `insert(path)`.
* *Mẹo:* Nếu thấy logic Split node quá khó, hãy implement một cây tìm kiếm cân bằng đơn giản trước, rồi tối ưu sau nếu còn giờ.


* **Output:** Có thể lưu secret theo đường dẫn `/prod/db/pass`.

**Ngày 6: 01/01/2026 - API Layer & Agent "Kaellir"**

* **Nhiệm vụ:**
* Viết một lớp Wrapper đơn giản để nhận input từ CLI hoặc Socket giả lập.
* Code `Kaellir` (Client): Một vòng lặp gửi request liên tục vào `Kallisto`.


* **Output:** Client gửi "GET key", Server trả về "Value".

---

### GIAI ĐOẠN 3: BENCHMARK & VIẾT BÁO CÁO

*Mục tiêu: Tạo ra các con số "biết nói" và lấp đầy 20 trang báo cáo.*

**Ngày 7: 02/01 - Benchmark (Locust/Script)**

* **Nhiệm vụ:**
* Chạy script `Kaellir` để spam 100,000 requests.
* Đo thời gian phản hồi (Latency).
* So sánh: Chạy thử với `std::map` (C++ default) để thấy sự khác biệt của Cuckoo Hash.


* **Output:** Các biểu đồ so sánh RPS, Latency (Screenshot ngay để đưa vào báo cáo).

**Ngày 8 & 9: 03/01 - 04/01 - Viết Báo Cáo (Sprint Writing)**

* **Cấu trúc 20 trang (như đã bàn):**
1. Introduction (Bài toán quản lý Secret & rủi ro Hash Flood).
2. Architecture (Mô hình Kallisto/Kaellir).
3. Theory & Implementation (Giải thích code Cuckoo, SipHash, B-Tree - *Copy code vào giải thích*).
4. Performance Analysis (Phân tích Big-O và show biểu đồ Benchmark hôm trước).
5. Conclusion.


* **Output:** File PDF nháp đầu tiên.

---

### GIAI ĐOẠN 4: DEFENSE DRILL (VỀ ĐÍCH)

*Mục tiêu: Chuẩn bị tâm lý và kỹ năng để đối mặt với Thầy.*

**Ngày 10: 05/01 - Review Code & Refactor**

* **Nhiệm vụ:**
* Đọc lại toàn bộ code. Chỗ nào dùng pointer trần (`*`) thì đổi sang `std::unique_ptr` hoặc `shared_ptr`.


* Thêm comment vào code (Thầy sẽ đọc code trên máy bạn).
* Kiểm tra memory leak (nếu biết dùng Valgrind thì tốt, không thì review bằng mắt).



**Ngày 11: 06/01 - Tổng duyệt (Mock Defense)**

* **Nhiệm vụ:**
* Tự đặt câu hỏi: "Tại sao độ phức tạp là O(1)?", "Nếu bảng đầy thì sao?".
* **Thử thách:** Mở một file trắng, tự code lại hàm `CuckooHash::Insert` trong 10 phút. Nếu làm được, bạn đã thắng 90%.
* Quay video demo (theo yêu cầu nộp bài).



**Ngày 12: 07/01 - LÊN THỚT (LIVE)**

* Ngủ đủ giấc. Mang theo laptop đã sạc đầy. Tự tin với kiến thức về Hash Flood.

---

### HÀNH ĐỘNG NGAY HÔM NAY (27/12)

Bạn chỉ có **3 tiếng** tối nay. Hãy làm đúng 2 việc này để chốt "Software Design":

1. **Define Interface (`kallisto.h`):** Viết ra tên các hàm bạn sẽ code. Đừng code ruột vội.
2. **Chốt cấu trúc dữ liệu:** Xác định xem `Bucket` của Cuckoo Hash sẽ trông như thế nào (dùng `struct` hay `class`, lưu `pair<string, string>` hay gì).

**Tôi có nên generate luôn cái file header `kallisto.h` mẫu (Skeleton) cho bạn để bạn bắt đầu tối nay không?** Nó sẽ tiết kiệm cho bạn 1 tiếng google cú pháp đấy.
—
Chào "Partner", tôi hiểu ý bạn. Chúng ta sẽ làm đúng tinh thần Agile/Scrum: Ship cái MVP (Minimum Viable Product) tinh gọn nhất, nhưng "đắt" nhất.
Tính năng quan trọng nhất (The "Money Shot" Feature) mà Kallisto phải demo được để "khoe" trọn vẹn 3 thuật toán trong một luồng xử lý duy nhất chính là:
🌟 MVP Feature: "Path-Based Secret Retrieval"
(Truy xuất Secret theo phân cấp Đường dẫn)
Đây là tính năng mô phỏng hành vi cốt lõi của Vault: Client hỏi xin một secret nằm trong một thư mục cụ thể, và Server trả về giá trị tức thì.

Tại sao tính năng này "cân" được cả 3 thuật toán?
Hãy tưởng tượng luồng đi của một request từ Kaellir (Agent) gửi tới Kallisto (Server):
GET /prod/payment/db_pass
Hệ thống sẽ xử lý theo quy trình 3 bước sau (Mapping chính xác vào 3 yêu cầu của thầy):
1. Bước 1: Validate Path (Sân chơi của B-Tree)
Hành động: Hệ thống phải kiểm tra xem thư mục /prod/payment/ có tồn tại không.
Thuật toán: B-Tree (Day 11).
Demo Point: Thay vì duyệt mảng tuyến tính ($O(N)$), bạn dùng B-Tree để "nhảy" qua các node đường dẫn.
Tìm prod $\rightarrow$ trỏ xuống node con.
Tìm payment $\rightarrow$ trỏ xuống node con.
Thấy hợp lệ!
Research Link: Disk-Optimized Storage (dù ta chạy RAM nhưng giả lập cấu trúc này).
2. Bước 2: Secure Key Hashing (Sân chơi của SipHash)
Hành động: Khi đã vào đúng "thư mục", hệ thống lấy key db_pass để chuẩn bị tìm kiếm.
Thuật toán: Universal Hashing / SipHash (Day 6).
Demo Point: Bạn không dùng std::hash. Bạn gọi hàm SipHash(key="db_pass", seed=SECRET_KEY).
Research Link: Security & Hash Flooding Resistance (Chống tấn công từ chối dịch vụ).
3. Bước 3: Instant Lookup (Sân chơi của Cuckoo Hashing)
Hành động: Dùng mã băm vừa tạo để lấy giá trị mật khẩu thật.
Thuật toán: Cuckoo Hashing (Day 7).
Demo Point:
Kiểm tra vị trí Hash1: Nếu có $\rightarrow$ Trả về ngay ($O(1)$).
Nếu không, kiểm tra vị trí Hash2: Nếu có $\rightarrow$ Trả về ngay ($O(1)$).
Không bao giờ phải duyệt danh sách liên kết dài lê thê.
Research Link: Worst-case $O(1)$ Access Time.

Kịch bản Demo (Script) để ăn điểm tuyệt đối
Khi bạn quay video demo hoặc Live Defense, bạn chỉ cần show đúng 1 log này chạy trên màn hình console là thầy hiểu bạn đã làm xong bài:
Plaintext
[KAELLIR] Request: GET /prod/payment/db_pass
---------------------------------------------------
1. [B-TREE] Searching path...
   -> Found Node 'prod'
   -> Found Node 'payment' (Leaf Node)
   -> Path Validated. Time: 0.002ms

2. [SIPHASH] Hashing key 'db_pass'...
   -> Seed: 0xCAFEBABE
   -> Hash Result: 0x93F2... (Secure against flooding)

3. [CUCKOO] Looking up in Bucket Table...
   -> Check T1[0x93F2]: EMPTY
   -> Check T2[0x4A1B]: HIT! Value found.
   -> Access Time: O(1) guaranteed.
---------------------------------------------------
[RESPONSE] 200 OK: "SuperSecretPassword123"

Kết luận
MVP của chúng ta chỉ cần tập trung làm cho hàm GetSecret(path, key) chạy mượt mà theo đúng luồng trên.
Không cần tính năng Update/Delete phức tạp.
Không cần phân quyền User.
Không cần mã hóa đường truyền (HTTPS).
Bạn thấy tính năng MVP này đủ "gọn" để code trong 3-4 buổi tối chưa? Nếu OK, bước tiếp theo tôi sẽ đưa bạn thiết kế Class Diagram cực nhỏ gọn cho đúng cái MVP này.

