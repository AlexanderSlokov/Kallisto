# Kallisto Project

## Công dụng (phần này sẽ được ship vào README.md khi sẵn sàng)

### BATCH mode: "Identity & Secret Proxy" hoặc "High-speed Transit Engine" (làm việc trên RAM là chính)

#### 1. Dynamic Session Keys (Khóa phiên động)

Đây là ứng dụng phổ biến nhất. Các khóa dùng để mã hóa cookie, session người dùng hoặc các phiên làm việc giữa các microservices. Nếu server sập và RAM mất trắng, người dùng chỉ việc đăng nhập lại.

Lợi ích: Tốc độ kiểm tra session cực nhanh giúp gánh được lượng traffic khổng lồ mà không làm nghẽn DB chính.

#### 2. Response Wrapping Tokens (Vault style)

Trong Vault có một tính năng gọi là "Response Wrapping". Nó tạo ra một token tạm thời chỉ dùng ĐÚNG 1 LẦN và có thời gian sống (TTL) cực ngắn (vài giây đến vài phút). Nếu mất token này trước khi khách hàng kịp lấy, họ chỉ cần yêu cầu cấp lại cái mới từ Master Vault.

Lợi ích: Bảo vệ bí mật gốc trong khi vẫn giao tiếp cực nhanh.

#### 3. Dynamic Database Credentials (Lease-based)

Dạng bí mật mà `Kallisto` tự tạo ra cho ứng dụng để truy cập DB (với quyền hạn bị giới hạn). Nếu RAM bị reset, các ứng dụng khách sẽ nhận lỗi "401 Unauthorized". Theo thiết kế của các hệ thống Cloud-native, ứng dụng sẽ tự động gọi lại API để xin cấp "Identity" mới.

Lợi ích: Bạn có thể xoay vòng (rotate) mật khẩu DB liên tục (mỗi 5 phút) mà không lo bị nghẽn Disk I/O.

#### 4. Transit Encryption as a Service

Đây không phải là lưu trữ bí mật, mà là dùng server của bạn như một "Máy tính toán mã hóa". Bạn gửi dữ liệu thô qua API, server dùng khóa trong RAM để mã hóa và trả về kết quả. Khóa mã hóa chính (Master Key) có thể được nạp từ một nguồn an toàn (như HSM hoặc Master Vault) khi server khởi động. Nếu server sập, bạn chỉ cần nạp lại khóa vào RAM.

Lợi ích: Xử lý mã hóa dữ liệu nhạy cảm (số thẻ tín dụng, căn cước...) ở tốc độ triệu ops/sec mà không bao giờ ghi lộ dữ liệu xuống Disk.

#### 5. ACL Cache (Quyền truy cập)

Các bảng phân quyền (Policy) cực kỳ phức tạp. Những thứ này thực tế được lưu ở một DB bền vững nào đó. `Kallisto` đóng vai trò là một "Hot-Cache" cực mạnh.

Lợi ích: Việc duyệt B-tree trên RAM để quyết định "Ai được làm gì" ở tốc độ triệu lần/giây giúp hệ thống của bạn không bao giờ bị "đứng hình" khi có bão traffic.

#### 6. Cách triển khai kiến trúc

`Kallisto` đứng trước `Vault` Master Node và các replica của nó. Khi có request, nếu `Cuckoo Table` không có (Miss), nó mới "lết" sang Master Vault để lấy về rồi nhét vào RAM. Sau đó, mọi request tiếp theo sẽ được hưởng tốc độ gấp nhiều lần Vault.

# 🚀 FUTURE ROADMAP

Phần này dành cho "Later Works" sau đồ án, tập trung vào các kỹ thuật Software Architecture nâng cao để biến Kallisto thành một Production-Grade System.

---

## ✅ Phase 1.1: Threading Infrastructure - COMPLETE (2025-01-18)

Đã triển khai thành công mô hình multi-threading lấy cảm hứng từ Envoy:

- `Dispatcher` - epoll-based event loop với timerfd/eventfd
- `Worker` - Thread wrapper với request counting
- `WorkerPool` - Quản lý N workers (auto-detect CPU cores)
- `Thread-Local Storage` - Zero-lock per-thread data access

**Files mới:**
- `include/kallisto/event/dispatcher.hpp`
- `include/kallisto/event/worker.hpp`
- `include/kallisto/thread_local/thread_local.hpp`
- `src/event/dispatcher_impl.cpp`
- `src/event/worker_impl.cpp`
- `src/thread_local/thread_local_impl.cpp`
- `tests/test_threading.cpp`
- `tests/bench_multithread.cpp`

**Test results:** `make test-threading` - 12/12 passed ✅

---

## ✅ Phase 1.2: Sharded CuckooTable - COMPLETE (2025-01-18)

### Vấn đề Ban Đầu

Lock contention trên global `shared_mutex` gây performance degradation:
- Multi-thread (3w) chỉ đạt 143k RPS (0.5x so với single-thread)

### Giải Pháp: 64-Partition Sharding

Chia CuckooTable thành 64 partitions độc lập, mỗi partition có lock riêng.

**Files mới:**
- `include/kallisto/sharded_cuckoo_table.hpp`
- `src/sharded_cuckoo_table.cpp`

### Kết Quả Benchmark

| Pattern | RPS | Description |
|---------|-----|-------------|
| **MIXED 95/5** | **1.17M** | 95% reads, 5% writes |
| **BURSTY** | **600k** | Deployment bursts |
| **ZIPF** | 28k | Hot keys (expected) |

**Improvement: 4-6x so với non-sharded!**

**Test commands:**
- `make benchmark-multithread` - Comprehensive suite
- `make benchmark-p99` - Latency test (p99 = 3.24 μs ✅)
- `make test-atomic` - Thread-safety stress test

---

## 🔜 Phase 1.3: gRPC Server + WorkerPool Integration

### Trạng Thái: READY TO IMPLEMENT

Threading infrastructure đã **sẵn sàng** cho multi-threaded server:

| Component | Status | Notes |
|-----------|--------|-------|
| `Dispatcher` (epoll) | ✅ Ready | Event loop per worker |
| `Worker` | ✅ Ready | Thread wrapper |
| `WorkerPool` | ✅ Ready | N worker management |
| `ThreadLocal Storage` | ✅ Ready | Zero-lock TLS |
| `ShardedCuckooTable` | ✅ Integrated | Thread-safe storage |

### Kiến Trúc Mục Tiêu (Envoy-Style)

```
gRPC Server ──► WorkerPool(N) ──┬─► Worker 0 ──► ShardedCuckooTable
                                ├─► Worker 1 ──►   (64 partitions)
                                └─► Worker N ──►
```

### Cần Implement

1. **gRPC Server** - Accept connections, parse requests
2. **Request Router** - Distribute to workers (round-robin hoặc hash-based)
3. **WorkerPool Integration** - Mỗi worker xử lý requests độc lập

### Tại Sao Chờ gRPC?

Envoy pattern: WorkerPool chỉ có ý nghĩa khi có **concurrent network requests**.
Hiện tại Kallisto là CLI → single-threaded đủ.
Khi có gRPC → WorkerPool sẽ được tích hợp tự nhiên.

---

## 1. Security Layer

### Encryption-at-Rest (Mã hóa lưu trữ)

**Vấn đề**: dữ liệu hiện tại lưu plaintext. Kể cả nó có trên RAM và dễ bay hơi thì vẫn là plaintext.

**Giải pháp**: Tích hợp `AES-256-GCM` ( cân nhắc `lazySSL` của Google) để encrypt value trước khi lưu trữ đi bất kỳ đâu. Chỉ giữ Master Key trên RAM.

**Mục tiêu**: Key Management Life-cycle (Rotation, Unseal).

### Secure Memory Allocator (Bảo vệ RAM)

**Vấn đề**: Memory Dump hoặc Swap file có thể làm lộ secret.

**Giải pháp**: Implement custom allocator sử dụng `mlock()` (cấm swap) và `explicit_bzero` (xóa trắng RAM ngay khi free) của `Hashicorp Vault`.

**Bài học**: OS Memory Management & Low-level Systems Programming.

### Access Control List (Phân quyền)

*Vấn đề*: Ai có quyền truy cập CLI cũng đọc được mọi thứ.

*Giải pháp*: Thêm cơ chế Authentication (Token-based) và Authorization (Path-based Policy như Vault). Cái này thì sử dụng cấu trúc B-tree sẽ rất thích hợp vì dữ liệu vốn là dạng kế thừa mở rộng theo mô hình cây phân quyền.

*Bài học*: RBAC Design Patterns.

## 2. Scalability & Reliability

### Cải tiến Cuckoo Table thành Blocked Cuckoo

Với kiến thức hệ thống hiện đại và sức mạnh của C++, hoàn toàn có thể khiến Cuckoo Table trở nên tiết kiệm mà vẫn giữ được tốc độ cao. Bí mật nằm ở việc thay đổi cấu trúc từ "1 slot mỗi bucket" sang "nhiều slot mỗi bucket" (thường là 4):

1. **Giảm tối đa hiện tượng "loop kick":** Cuckoo truyền thống (1 slot/bucket) khi nạp đầy đến khoảng 50%, xác suất bị "đá" nhau vòng quanh tăng vọt rồi lại phải resize bảng. Đó là lý do vì sao bạn cần RAM gấp đôi dữ liệu. Với cấu trúc `Blocked Cuckoo` (4 slots/bucket), nhờ có 4 sự lựa chọn trong cùng một chỗ, xác suất tìm được ít nhất 1 chỗ trống tăng lên cực lớn. Khoa học đã chứng minh: Với 4 slots, bạn có thể nạp đầy đến 95% dung lượng bảng trước khi gặp vấn đề về "đá" nhau.

2. **"Buff" thêm sức mạnh từ CPU Cache:** Một Cache Line của CPU thường là 64 bytes. Nếu bạn thiết kế một Bucket gồm 4 slots (mỗi slot gồm 4 bytes Tag + 12 bytes Pointer = 16 bytes), thì cả cái Bucket đó nặng đúng 64 bytes. Khi CPU nạp 1 bucket vào để kiểm tra slot đầu tiên, nó sẽ nạp luôn cả 3 slot còn lại vào Cache cùng một lúc (vì tụi nó nằm sát nhau). Việc check 4 slots lúc này nhanh gần như check 1 slot, nhưng hiệu quả sử dụng RAM thì tăng gấp đôi.

3. **"Bắt ngược" điều kiện đói RAM:** Cuckoo Table (4-slot) Hiệu năng sử dụng RAM đạt >90%. => Thực tế, Cuckoo Table đang TIẾT KIỆM RAM hơn cả những thư viện chuẩn. Để quản lý 500MB dữ liệu Vault trên RAM: Thiết kế struct `Bucket { uint16_t tags[4]; void* pointers[4]; }` và cấp phát một mảng các Bucket sao cho tổng số lượng slots bằng khoảng 110% số lượng secret dự kiến. 

Cuối cùng là cài đặt hàm kick tối đa bao nhiêu lần thì tối ưu? Cái này phải nghiên cứu.


### Cải tiến hiệu suất + storage engine thực thụ chuyên dụng hiệu suất cao

*Vấn đề*: Strict Mode quá chậm, Batch Mode rủi ro mất data.
*Giải pháp*: kết hợp RocksDB với cái Cuckoo Table và kiến trúc Envoy sẽ tạo ra một hệ thống có tên gọi chuyên môn là "In-memory First, Disk-backed Database".

Tại sao sự kết hợp này lại "vô đối"?

1. Phân vai rõ ràng (Role Split)
Cuckoo Table (RAM): Đóng vai trò là "MemTable" hoặc "Hot-Cache". 100% các lệnh Read sẽ đâm vào đây đầu tiên. Với tốc độ O(1), bạn vẫn giữ được con số triệu ops/sec cho những dữ liệu đang "nóng".
RocksDB (Disk): Đóng vai trò là "Persistence Engine". Nhiệm vụ của nó là đảm bảo dữ liệu không bao giờ bị mất sau khi server được trả về 200 OK.
2. Quy trình xử lý một lệnh Ghi (Write Path)
Để không làm giảm hiệu năng, bạn sẽ làm như sau:

Request tới: Server C++ nhận Secret mới.
Ghi vào RocksDB: Bạn gọi hàm db->Put(). RocksDB cực kỳ thông minh, nó sẽ không ghi ngay xuống đĩa đâu. Nó ném vào một cái WAL (Write Ahead Log) trên đĩa (ghi nối đuôi rất nhanh) và một cái MemTable nội bộ của nó.
Cập nhật Cuckoo Table: Sau khi RocksDB xác nhận đã ghi xong (vào log), bạn cập nhật bản copy vào Cuckoo Table của mình.
Trả về Client: Xong! Toàn bộ quá trình này vẫn cực nhanh vì không có lệnh "tìm kiếm" nào trên đĩa cả.
3. Tại sao không dùng luôn bộ nhớ đệm của RocksDB?
Nhiều người sẽ hỏi: "RocksDB có Block Cache rồi, cần gì Cuckoo Table của mình?". Câu trả lời của một lập trình viên C++ sành sỏi sẽ là:

Overhead: Block Cache của RocksDB vẫn phải thông qua nhiều tầng quản lý phức tạp.
O(1) thực thụ: Cuckoo Table của bạn (với 4 slots/bucket) check RAM trực tiếp, nhanh hơn bất kỳ bộ đệm tổng quát nào của RocksDB. Bạn đang tối ưu đến tận cùng chu kỳ CPU.
4. Những "món quà" RocksDB tặng bạn:
Khi ghép thêm RocksDB, bạn bỗng nhiên có thêm một loạt tính năng "khủng" mà không cần tự code:

Compaction: Tự động dọn dẹp các secret cũ, bị xóa hoặc bị ghi đè để giải phóng ổ cứng.
Snapshot/Backup: Khả năng sao lưu dữ liệu mà không cần dừng server.
LSM-Tree: Cách tổ chức dữ liệu giúp việc ghi vào ổ SSD bền hơn và nhanh hơn nhiều so với các DB truyền thống.
Một lưu ý cực quan trọng:
RocksDB là một thư viện C++ (embeddable library), không phải là một server chạy riêng (như Redis hay MySQL). Khi bạn build, RocksDB sẽ nằm ngay bên trong cái file binary server của bạn. Điều này giúp loại bỏ hoàn toàn độ trễ mạng (Network Latency) khi server C++ của bạn muốn lưu dữ liệu.

Bạn thấy sự đánh đổi giữa việc "Ghi nhanh nhưng có rủi ro mất vài ms dữ liệu nếusập nguồn đột ngột" (Async Write) và "Ghi chậm hơn nhưng chắc chắn từng byte" (Sync Write) cái nào phù hợp với con đường bạn đang đi hơn? Thông thường với Secret, người ta sẽ chọn Sync Write để an toàn tuyệt đối.

Đúng vậy! Đây chính là khái niệm quan trọng nhất khiến RocksDB trở nên cực kỳ bá đạo trong các hệ thống C++ hiệu năng cao.

RocksDB không phải là một "Server" giống như Redis hay MySQL hay PostgreSQL. Nó là một Library (Thư viện).

1. "Đính kèm" (Embeddable) nghĩa là gì?
Hãy tưởng tượng:

Với Redis/MySQL: Bạn phải cài đặt một phần mềm riêng, mở một Port (như 6379 hay 3306). Code của bạn phải qua Network (TCP/IP) hoặc Unix Socket để gửi lệnh tới nó.
Với RocksDB: Bạn #include "rocksdb/db.h", sau đó khi biên dịch (compile), toàn bộ mã nguồn của RocksDB sẽ được "nhập" vào làm một phần của file thực thi (.exe hoặc .bin) của bạn.
2. Tại sao điều này lại "khủng khiếp" cho Project của bạn?
Khi bạn dùng RocksDB bên trong Server C++ của mình:

Zero Network Latency: Không có độ trễ mạng. Khi bạn lưu một Secret, lệnh gọi hàm db->Put() thực chất là một lệnh CPU trực tiếp, không phải đi qua card mạng, không phải đóng gói gói tin TCP.
Shared Memory: RocksDB và cái Cuckoo Table của bạn sống chung trong một căn nhà (cùng một tiến trình/process). Chúng dùng chung RAM, chung CPU. Việc chuyển dữ liệu giữa RAM của bạn vào đĩa qua RocksDB nhanh đến mức tối đa mà phần cứng cho phép.
Không cần quản lý Server riêng: Bạn không bao giờ phải lo lắng chuyện "Server Database bị sập" trong khi App vẫn chạy. Nếu App chạy thì DB chạy, App tắt thì DB tắt. Dữ liệu được lưu trữ an toàn trong một thư mục trên ổ cứng mà bạn chỉ định.
3. Những "ông lớn" nào cũng dùng kiểu này?
Cách làm này thực ra cực kỳ phổ biến trong giới công nghệ "hạng nặng":

TiKV (Storage của TiDB): Dùng RocksDB làm cái lõi lưu trữ xuống đĩa.
CockroachDB: Những phiên bản đầu tiên dùng RocksDB cực kỳ thành công.
Meta (Facebook): Chính là người tạo ra RocksDB để dùng cho các dịch vụ lưu trữ khổng lồ của họ.
4. Nó giúp gì cho cái "Cuckoo Table" của bạn?
Bạn cứ tưởng tượng cái Cuckoo Table là phần "não bộ" xử lý nhanh trên RAM, còn RocksDB là cái "cuốn sổ tay" ghi chép trên đĩa cực bền.

Mỗi khi Server khởi động, nó chỉ cần bảo RocksDB: "Này, đọc hết data trong thư mục này ra cho tôi".
Sau đó nó nạp sạch vào Cuckoo Table.
Từ giây phút đó, mọi việc đọc (Read) đều diễn ra trên RAM với tốc độ 1.5 triệu ops/sec mà bạn mong muốn.
Tóm lại: Việc RocksDB là một thư viện đính kèm chính là "mảnh ghép hoàn hảo" cho một server C++ tự viết. Nó giúp bạn giữ được sự đơn giản, tính độc lập (không phụ thuộc vào server bên ngoài) và hiệu năng "xé gió".

Bạn có thấy khái niệm "nhét cả database vào trong code của mình" này thú vị không? Nó chính là bí mật để các hệ thống "khủng" giữ được tốc độ mà các DB truyền thống không bao giờ theo kịp đấy!

Chính xác! Về mặt kỹ thuật, việc nhúng RocksDB giúp bạn giải quyết xong 90% "cơn đau đầu" về Storage Engine (ghi xuống đĩa, an toàn dữ liệu, phục hồi sau khi sập). 10% còn lại chỉ là việc bạn "đi dây" (wire up) giữa nó và cái Cuckoo Table của bạn thôi.

Tuy nhiên, để bạn không bị "lạc quan quá mức", hãy xem cụ thể việc "nhúng vào là xong" thực tế sẽ trông như thế nào:

1. Những phần RocksDB làm hộ bạn (Fix xong):
Ghi log an toàn (WAL): RocksDB đảm bảo mỗi khi bạn gọi Put, dữ liệu được ghi xuống đĩa cực nhanh và an toàn.
Quản lý File: Nó tự chia data thành các file nhỏ (.sst), tự nén (Compression), tự dọn dẹp (Compaction). Bạn không cần quan tâm folder chứa data có bao nhiêu file hay cấu trúc thế nào.
Phục hồi (Recovery): Khi server khởi động lại, RocksDB tự quét các file log để biết trạng thái cuối cùng của dữ liệu.
2. "Đi dây" - Chỗ bạn vẫn phải code:
Bạn cần viết một lớp logic để kết nối hai thế giới:

Lúc ghi (Write):
cpp
// 1. Ghi vào RocksDB (Persistent)
rocksdb_status = db->Put(write_options, "key", "encrypted_secret");
// 2. Nếu RocksDB ok, thì mới nhét vào Cuckoo Table (RAM)
if (rocksdb_status.ok()) {
    my_cuckoo_table.insert("key", "pointer_to_data");
}
Lúc khởi động (Startup): Bạn phải viết một vòng lặp (Iterator) để RocksDB đổ hết dữ liệu cũ từ đĩa lên đầu cho cái Cuckoo Table nạp lại.
3. Một lưu ý nhỏ về "Đối trọng của Vault"
RocksDB giải quyết cực tốt bài toán trên một máy đơn lẻ (Single Node).

Nếu bạn chỉ định chạy 1 con server C++ siêu mạnh: Thế là đủ.
Nếu bạn muốn chạy 2-3 con server để dự phòng (High Availability): Lúc này bạn cần thêm một giao thức để các RocksDB trên các máy khác nhau đồng bộ với nhau (thường là dùng Raft - giống như cái cách mà Vault hay Consul làm).
Tóm lại:
Với một project cá nhân hoặc một hệ thống nội bộ cần hiệu năng kinh hoàng, việc kết hợp C++ + Cuckoo Table + RocksDB là bạn đã có một cái "Storage Engine" xịn ngang ngửa (hoặc hơn) bất kỳ giải pháp thương mại nào.

Đến đây, bạn đã thấy bức tranh tổng thể chưa?

Mạng (Networking): Dùng kiến trúc Envoy (Dispatcher/Event Loop).
RAM (Fast Path): Dùng Cuckoo Table 4-slots + B-Tree ACLs.
Bảo mật: SipHash + AES-256-GCM (Hardware).
Đĩa (Persistence): RocksDB.
Bạn đã cảm thấy "ngứa tay" muốn bắt đầu viết code để nhúng thử RocksDB vào chưa? Tôi có thể hướng dẫn bạn cách setup CMakeLists.txt để link RocksDB vào project C++ của bạn đấy!

### Network Interface (gRPC/HTTP API)

*Vấn đề*: Hiện tại chỉ dùng CLI cục bộ (Unix Pipe).

*Giải pháp*: Mạng (Networking): Dùng kiến trúc Envoy (Dispatcher/Event Loop)

*Bài học*: API Design, Distributed Systems Communication.

### Replication (Raft Consensus):

*Vấn đề*: Single Point of Failure. Server đơn mà chết là hệ thống dừng.

*Giải pháp*: Dùng thuật toán Raft để bầu Leader tối thiểu quorum 3 node. 

`NuRaft` (do eBay phát triển) được chọn vì nó:

1. Cực kỳ nhẹ, chỉ tập trung vào logic Raft và rất dễ nhúng vào các project C++ hiện đại.

2. NuRaft được thiết kế theo kiểu "Bring Your Own Storage" (Tự mang bộ lưu trữ của bạn tới). Nó chỉ lo phần "cãi nhau" giữa các node để bầu Leader và đồng bộ Log. Còn việc lưu Log ở đâu thì nó... nhờ bạn. Cho RocksDB làm nơi lưu trữ cho NuRaft là xong! 

Khi lắp thêm NuRaft,`Kallisto` sẽ có cấu trúc như sau:

*Layer 1: NuRaft (Consensus)*: Đảm bảo 3-5 máy server của bạn luôn đồng nhất về dữ liệu. Khi có một lệnh Write mới, NuRaft sẽ "hỏi ý kiến" các máy khác.
*Layer 2: RocksDB (Storage)*: Dùng để lưu Raft Log (các bước thay đổi dữ liệu)và State Machine Data (dữ liệu secret cuối cùng).
*Layer 3: Cuckoo Table (Performance)*: Là cái "State Machine" trên RAM. Sau khi NuRaft báo đã đạt được Quorum (đa số đồng ý), bạn mới cập nhật vào Cuckoo Table.

3. Cách bạn "lắp" NuRaft vào code:
- `state_machine`: Bạn nhét logic B-tree và Cuckoo Table của bạn vào đây. Khi NuRaft nói "Lệnh này đã được đồng thuận", nó sẽ gọi hàm commit trong state machine của bạn.
- `log_store`: Bạn dùng RocksDB để lưu các bản ghi log này xuống đĩa.

4. Khi có HA (Raft), hiệu năng Write sẽ giảm xuống (vì phải chờ mạng giữa các máy và chờ Quorum), thường sẽ còn khoảng `50,000 - 150,000 ops/sec`. Nhưng hiệu năng Read thì vẫn đạt mức triệu ops/sec vì bạn đọc trực tiếp từ Cuckoo Table trên RAM của máy Leader (hoặc các máy Follower nếu bạn chấp nhận Read-after-write latency).
---

## ✅ Phase 2: High-Performance Server & Networking (Feb 2026) - COMPLETE

### 1. Kiến Trúc Thread-Per-Core (Envoy Style)
- **Unified Event Loop**: Tích hợp gRPC CompletionQueue vào trong `Dispatcher` (epoll) dùng `timerfd` (1ms polling).
- **SO_REUSEPORT**: 4 worker threads bind cùng port 8200/8201, kernel tự load balance. Không có acceptor thread bottleneck.
- **HTTP/1.1 & Vault KV v2 API**: Tự viết parser (zero-copy), sử dụng `simdjson` để parse JSON cực nhanh.
- **Graceful Shutdown**: Xử lý tín hiệu SIGINT/SIGTERM, drain connection an toàn.

### 2. Critical Bug Fixes (Stability)
- **Dispatcher Use-After-Free**: Fix lỗi crash kinh điển khi `unordered_map` rehash callback trong lúc đang iterate event loop.
  - Giải pháp: **Deferred Mutation** (Pending Add/Remove queues) + `std::unique_ptr<Connection>`.
- **Concurrency Crash**: Fix lỗi race condition ở `ShardedCuckooTable` khi write load cao.

### 3. Server Benchmark Results (15/02/2026)
Environment: AMD Ryzen 5 3550H (4 vCPUs allocated), 8GB RAM (CodeSpaces)
*Lưu ý: Chạy trên CPU Laptop cũ (Zen+, 2019) trong Container.*

| Endpoint | Workload | RPS (c=50) | Stability |
|----------|----------|------------|-----------|
| **GET** (Read) | JSON lookup | **67,987** | ✅ 100% |
| **PUT** (Write) | JSON parse + Hash | **46,465** | ✅ 100% |
| **MIXED** | 95% R / 5% W | **46,213** | ✅ 100% |

**Đánh giá**: **EXCELLENT**.
- Đạt ~17k RPS/core xử lý full HTTP/JSON stack.
- Latency trung bình < 1ms (p99 ~7-9ms do container scheduling jitter).
- Không có memory leak (ASAN verified).