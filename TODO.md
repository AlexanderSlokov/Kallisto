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

1. [x] **Benchmark gRPC Server**: Basic implementation complete (~3.5k RPS due to polling).
    - [ ] **Optimization**: Rewrite `GrpcHandler` to use dedicated threads/CQs instead of 1ms Timer polling.
2. **Request Router** - Distribute to workers (round-robin hoặc hash-based)
3. **WorkerPool Integration** - Mỗi worker xử lý requests độc lập

### Tại Sao Chờ gRPC?

Envoy pattern: WorkerPool chỉ có ý nghĩa khi có **concurrent network requests**.
Hiện tại Kallisto là CLI → single-threaded đủ.
Khi có gRPC → WorkerPool sẽ được tích hợp tự nhiên.

---

## 1. Security Layer & Testing Infrastructure

### Migrate Unit Testing to Google Test (GTest) & Google Mock

**Vấn đề**: Framework test hiện tại là một file macro C++ tối giản tự viết. Mặc dù nhẹ nhưng thiếu tính năng mở rộng khi dự án lớn dần, không support mocking cho network/gRPC để cô lập bài test.

**Giải pháp**: Tích hợp Google Test (gtest) và Google Mock (gmock) thông qua `vcpkg`. Chuyển đổi các bài test hiện tại trong thư mục `tests/` sang dạng `TEST(...)` hoặc `TEST_F(...)` chuẩn của GTest. 

**Mục tiêu**: Nâng chuẩn Enterprise-grade giống Envoy Proxy, cho phép viết test rành mạch hơn và setup các Fixture tests cho server.

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


## ✅ Phase 3: RocksDB Persistence Integration (March 2026) - COMPLETE

**Vấn đề**: Strict Mode sử dụng file binary thô sơ quá chậm (`fsync` liên tục), Batch Mode thì mất dữ liệu khi crash.
**Giải pháp**: Chuyển đổi sang mô hình "In-memory First, Disk-backed Database" bằng cách nhúng thẳng thư viện RocksDB.

### Kết quả triển khai:
1. **Phân vai rõ ràng**: 
   - `ShardedCuckooTable` đóng vai trò Hot-Cache trên RAM (O(1) cho Read).
   - `RocksDB` đóng vai trò Persistence Engine (Ghi Write-Ahead Log cực nhanh xuống đĩa).
2. **Dual-write Path**: HTTP PUT -> Ghi RocksDB WAL (Async) -> Cập nhật CuckooTable -> Return 200 OK.
3. **Cache-miss Fallback**: Đọc CuckooTable bị miss -> Xuống RocksDB -> Nạp ngược lên CuckooTable.
4. **Performance**: 
   - Duy trì ~127,000 RPS cho GET (hot path in-memory).
   - Đạt ~29,000 RPS cho PUT (đã bao gồm overhead ghi đĩa của hệ điều hành).

**Files thay đổi**:
- `include/kallisto/rocksdb_storage.hpp` 
- `src/rocksdb_storage.cpp`
- Tích hợp trực tiếp vào `http_handler.cpp`, `grpc_handler.cpp`, và `kallisto.cpp`.

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

- [x] **[URGENT] B-Tree Index Bypass**
  - **Severity**: High (DoS vulnerability + Data Accessibility issue)
  - **Description**: The recent RocksDB integration bypassed the `BTreeIndex` path validator in Server Mode. Also, CLI mode fails to rebuild the B-Tree on startup from RocksDB, causing valid paths to be incorrectly rejected as "not found" after a restart.
  - **Action**: Modified `RocksDBStorage` to add an `iterate_all` method. Updated `kallisto.cpp` and `kallisto_server.cpp` to populate the `BTreeIndex` on startup. Updated `HttpHandler` and `GrpcHandler` to check B-Tree before consulting caches.

### 3. Server Benchmark Results (Latest: 08/03/2026)
Environment: AMD Ryzen 5 3550H (4 vCPUs allocated), 8GB RAM (CodeSpaces)
*Lưu ý: Đã tích hợp B-Tree Indexing (Shared Mutex) và RocksDB Persistence.*

| Endpoint | Workload | RPS (c=200) | Stability | Latency (p99) |
|----------|----------|-------------|-----------|---------------|
| **GET** (Read) | B-tree + Cuckoo | **107,359** | ✅ 100% | 11.07 ms |
| **PUT** (Write) | B-tree + RocksDB | **30,087** | ✅ 100% | 29.68 ms |
| **MIXED** | 95% R / 5% W | **86,767** | ✅ 99.9% (159 Timeouts) | 15.97 ms |

**Đánh giá**: **EXCELLENT**.
- Tốc độ **GET** tăng nhảy vọt lên **107k RPS** (so với 67k đợt trước) nhờ bỏ contention và chỉ cần shared_lock ở B-Tree.
- Tốc độ **PUT** chững lại ở **30k RPS** (từ 46k) do chi phí của việc lấy Write-Lock tại B-Tree (unique_lock) + Ghi I/O vào RocksDB Disk.
- Ở những Mix Workload 95% Read thực tế, Kallisto đạt gần **87k RPS**, bỏ xa các hệ thống như Vault.
- Latency cực thấp, p99 duy trì ở mức < 30ms dù tải nặng 200 CCU. Không còn lỗi Segmentation Fault hay Data Race Cuckoo/BTree.