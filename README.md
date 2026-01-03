# HIGH-PERFORMANCE SECRET MANAGEMENT SYSTEM: IMPLEMENT A CUCKOO HASHING & B-TREE DATA STRUCTURE USING SIPHASH TO PREVENT HASHING COLLISIONS FROM DOS ATTACKS

# INTRODUCTION




# REQUIREMENTS

## Benchmark Targets

Với một server C++ tối ưu, các mốc hiệu năng cần được tối ưu tới các mốc sau:

### RPS (Requests Per Second): > 50,000 req/s.

Cuckoo Hashing với O(1) là worst-case, với các request tra cứu Secret đơn giản, CPU chỉ mất vài micro giây để tìm thấy dữ liệu. Nếu con số này dưới 10k, cần nghi ngờ về việc phần mềm có bị lỗi I/O hay không.

### Latency (Độ trễ): p99 < 1ms (Sub-millisecond).

Cache Locality (Day 3). Việc sắp xếp các bucket của bảng băm nằm liên tục trong bộ nhớ giúp CPU không bị "cache miss", dẫn đến độ trễ cực thấp.

### Locust CCU (Concurrent Users): 500 - 1,000 CCU.

Con số này chứng minh khả năng quản lý Call Stacks và tài nguyên hệ thống của bạn tốt, không bị tràn bộ nhớ hay deadlock khi nhiều Agent (Kaellir) gọi tới cùng lúc.

---

# THEORY


## 1. SipHash

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

Chức năng: Quản lý cấu trúc cây thư mục (Path-based secrets), thực hiện các truy vấn theo tiền tố (prefix search).

Tại sao dùng: Một secret management system không chỉ lưu trong RAM mà phải lưu xuống đĩa cứng (persistent storage). B-Tree tối ưu số lần đọc/ghi (I/O) và tìm path trong B-Tree tốn O(log N), rất nhanh để chặn các request rác (ví dụ: user hỏi path `/admin/`... nhưng trong hệ thống chưa từng tạo path này, B-Tree sẽ ngăn chặn logic tìm kiếm sai path trước cả khi hệ thống phải khởi động SipHash, tính toán băm hash và so sánh với các entry trong Cuckoo Table).

# APPLICATIONS

## Architecture (Mô hình Kallisto/Kaellir).

### Storage Engine

Ta sẽ sử dụng Binary Packing (giống Raft). Mục tiêu của ta là High Performance (Cuckoo Hash O(1)). Rất vô lý nếu xử lý dữ liệu trên RAM rất nhanh nhưng việc ứng dụng phải thao tác ghi đĩa siêu chậm do phải tạo hàng nghìn folder và gây inode overhead. Việc `load_snapshot` từ 1 file binary vào RAM sẽ nhanh hơn rất nhiều so với việc scan folder, do đó ta chọn Binary File như trong Raft.

## Implementation 

(Giải thích code Cuckoo, SipHash, B-Tree - *Copy code vào giải thích*).

## Cuckoo Table Code Explanation

Ta chọn Cuckoo Hashing với kích thước 16384 slots (phục vụ test bài toán insert và retrieve 10 nghìn lượt) để đảm bảo hiệu suất cao nhất.

```cpp
KallistoServer::KallistoServer() {
    // TODO: implement ENV to change the size of initial Cuckoo Table.
    // We plan to benchmark 10,000 items. 
    // Cuckoo Hashing typically degrades if the load factor is above 50% (leads to cycles/infinite loops).
    // Capacity of 2 tables with size 16384 is 32,768 slots.
    // Load Factor = 10,000 / 32,768 ≈ 30% (Very Safe).
    storage = std::make_unique<CuckooTable>(16384);
    path_index = std::make_unique<BTreeIndex>(5);
    persistence = std::make_unique<StorageEngine>();

    // Recover state from disk
    auto secrets = persistence->load_snapshot();
    if (!secrets.empty()) {
        rebuild_indices(secrets);
    }
}
```

### Core Logic (Simplified)
Logic "Kick-out" (Cuckoo Displacement) từ file `src/cuckoo_table.cpp` giúp đạt O(1) được phát triển như sau

```cpp
bool CuckooTable::insert(const std::string& key, const SecretEntry& entry) {
    // 1. Check if key exists (Update logic)
    // Nếu key đã tồn tại trong bucket 1 hoặc 2, update value và return.
    size_t h1 = hash_1(key);
    if (table_1[h1].occupied && table_1[h1].key == key) {
        table_1[h1].entry = entry;
        return true;
    }
    // Check hash_2 tương tự
    size_t h2 = hash_2(key);
    if (table_2[h2].occupied && table_2[h2].key == key) {
        table_2[h2].entry = entry;
        return true;
    }

    // 2. Insert with Displacement (Fighting for slots)
    std::string current_key = key;
    SecretEntry current_entry = entry;

    for (int i = 0; i < max_displacements; ++i) {
        // [PHASE 1] Try to insert into Table 1
        size_t h1 = hash_1(current_key);
        if (!table_1[h1].occupied) {
            table_1[h1] = {true, current_key, current_entry};
            return true;
        }
        
        // [KICK] If Table 1 is full, kick out the old entry
        std::swap(current_key, table_1[h1].key);
        std::swap(current_entry, table_1[h1].entry);

        // [PHASE 2] Try to insert into Table 2
        size_t h2 = hash_2(current_key);
        if (!table_2[h2].occupied) {
            table_2[h2] = {true, current_key, current_entry};
            return true;
        }

        // [KICK] If Table 2 is full, kick out old entry and repeat process
        std::swap(current_key, table_2[h2].key);
        std::swap(current_entry, table_2[h2].entry);
    }

    // If looped too many times (reach max_displacements) without finding a slot -> Resize
    return false; 
}
```

*Why is this code fast?* Vì toàn bộ thao tác chỉ là truy cập mảng `table_1` và `table_2` của Cuckoo Table trên RAM (Cache L1/L2). Khác với Chaining Hashmap (dùng danh sách liên kết khi va chạm), Cuckoo Hash lưu dữ liệu phẳng (Flat Array) và CPU Prefetcher ưu tiên truy cập mảng này để dễ dàng prefetch cả một mảng giá trị liền kề nhau từ RAM vào CPU Cache. Lookup (hàm `get`) chỉ kiểm tra đúng 2 vị trí `h1` và `h2` nên big O của nó là `O(1) + O(1) = O(1)`. Không bao giờ phải duyệt danh sách dài nên độ trễ đạt ổn định < 1ms.


## Workflow

Khi chương trình (main.cpp) chạy, quy trình thử nghiệm sẽ diễn ra như sau:

### 1. Startup

Khi khởi tạo server (KallistoServer được khởi tạo):

Nó chuẩn bị 2 cấu trúc dữ liệu:

- `B-Tree Index`: Sinh ra danh mục các đường dẫn (ví dụ: /prod/payment, /dev/db) hiện tại.

- `Cuckoo Table`: Đây là nơi lưu trữ bí mật thực sự. Nó tạo sẵn số lượng Buckets cố định là `1024 buckets` để chờ điền dữ liệu.

### 2. Khi cất giấu bí mật (Lệnh PUT)

Người dùng (hoặc code) ra yêu cầu: "Lưu trữ mật khẩu secret123 vào đường dẫn /prod/db với key là `'password'` và value là `'secret123'`" Đây là những gì Kallisto làm bên trong:

Kiểm tra Mục lục (B-Tree): Kallisto gọi function `put_secret`, mà bên trong function này có hàm `insert_path` kiểm tra xem đường dẫn `/prod/db` đã tồn tại hay chưa. Nếu chưa có thì nó ghi thêm dòng `/prod/db` vào B-tree index.

Tạo `SecretEntry`: Nó đóng gói thông tin key, value, thời gian tạo vào một struct `SecretEntry`.

Cất vào kho (Cuckoo Hashing):
Nó dùng thuật toán SipHash để tính toán xem `SecretEntry` nên đặt vào ô số mấy trong "`Cuckoo Table`". Nếu ô đó trống, nó sẽ đặt vào và kết thúc công việc ngay lập tức. Nếu ô đó đã có `SecretEntry` khác, nó sẽ "đá" (kick) entry cũ sang ô khác để lấy chỗ cho `SecretEntry` mới. `SecretEntry` cũ sẽ thực hiện cơ chế này cho đến khi tất cả `SecretEntry` đều có chỗ. (Đây là điểm đặc biệt của Cuckoo Hashing).

### 3. Khi lấy bí mật (Lệnh GET)

Người dùng hỏi: "Cho tôi xin mật khẩu password trong ngăn /prod/db".

Qua cổng bảo vệ (B-Tree Validation):
Việc đầu tiên: Kallisto check ngay cuốn "Mục lục".
Nếu trong Mục lục không hề có dòng /prod/db -> Từ chối phục vụ ngay lập tức. (Đây là tính năng bảo mật để tránh kẻ gian mò mẫm đường dẫn lung tung).
Lục kho (Cuckoo Lookup):
Nếu Mục lục ok, nó mới dùng SipHash tính vị trí.
Vì là Cuckoo Hash, nó chỉ cần check đúng 2 vị trí duy nhất.
Vị trí 1 có không? -> Có thì trả về.
Không có thì check Vị trí 2 -> Có thì trả về.
Cả 2 đều không? -> Kết luận: Không tìm thấy. (Tốc độ cực nhanh $O(1)$).
Tóm tắt dưới dạng biểu đồ
mermaid
sequenceDiagram
    participant User
    participant Server as Kallisto
    participant BTree as B-Tree (Path Index)
    participant Cuckoo as Cuckoo (Secret Cache)
    Note over Server: 1. Startup (Empty)
    User->>Server: PUT("/prod/db", "pass", "123")
    Server->>BTree: Insert Path "/prod/db"
    Note right of BTree: Insert Path "/prod/db" into B-Tree
    Server->>Cuckoo: Insert Secret
    Note right of Cuckoo: Tính SipHash -> Tìm ô trống -> Ghi
    User->>Server: GET("/dev/hack", "root")
    Server->>BTree: Validate "/dev/hack"
    BTree-->>Server: FALSE (Path not found)
    Server-->>User: ERROR (Path not found)
    User->>Server: GET("/prod/db", "pass")
    Server->>BTree: Validate "/prod/db"
    BTree-->>Server: TRUE (Path found)
    Server->>Cuckoo: Lookup Key
    Cuckoo-->>Server: Found "123"
    Server-->>User: "123"

# ANALYSIS

## Time Complexity: 

### For SipHash 

It should be O(1). 

Hence, time Complexity of SipHash is O(1).

### For Cuckoo Hashing up and indexing

It should be O(1). 

Hence, time Complexity of Cuckoo Hashing up and indexing is O(1).

### For B-Tree

It should be O(log n). 

Hence, time Complexity of B-Tree is O(log n).

## Space Complexity: 

### For SipHash 

It should be O(1).

Hence, space Complexity of SipHash is O(1).

### For Cuckoo Hashing up and indexing

It should be O(1). 

Hence, space Complexity of Cuckoo Hashing up and indexing is O(1).

### For B-Tree

It should be O(log n). 

Hence, space Complexity of B-Tree is O(log n).
Đo lường thực tế để vẽ biểu đồ so sánh lý thuyết Big-O với hiệu năng thực tế.

# CONCLUSION
