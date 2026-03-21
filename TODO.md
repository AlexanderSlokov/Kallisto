# Kallisto Project TODO & History

## 🚀 ROADMAP & PENDING TASKS

### 1. Security Layer & Testing Infrastructure
- [ ] **[URGENT] Coverage & Integration Tests**: 
  - *Tier 1 (Networking/API - 0%):* `src/server/grpc_handler.cpp` (mock gRPC Client), `src/server/http_handler.cpp` (mock REST/JSON Client), `src/kallisto_server.cpp` (init logic).
  - *Tier 2 (Persistence - Crash Recovery):* `src/rocksdb_storage.cpp` (WAL recovery stress).
  - *Strategy:* Use Integration Tests (spin up `KallistoServer` on a loopback port) rather than isolated unit tests to quickly bump coverage.
- [ ] **Testing Framework Migration**:
  - Migrate legacy macro tests in `tests/` to Google Test (GTest) & Google Mock (GMock) via `vcpkg`.
- [ ] **Encryption-at-Rest**:
  - Implement AES-256-GCM (consider Google's `lazySSL`) to encrypt values before RocksDB sync. Maintain only Master Key in RAM. Implement Key Rotation & Unseal lifecycle.
- [ ] **Secure Memory Allocator**:
  - Build custom allocator using `mlock()` (disable OS swap) and `explicit_bzero` (zero out RAM on free) mirroring Vault's security model.
- [ ] **Access Control List (ACL)**:
  - Token-based Auth & Path-based Policy RBAC leveraging the B-Tree hierarchical structure.

- [ ] **Cơ chế xoay vòng secret và lease-renew secret theo policy**:

- [ ] **Cấp phát secret động có TTL ngắn theo policy**:

- [ ] **Cơ chế tự động xoá secret hết hạn**:

- [ ] **Chống timing attack**: Hạn chế thời gian xử lý request, không để thời gian xử lý request phụ thuộc vào nội dung request. Hashicorps Vault đã phát hiện ra rằng request xác thực sai trả kết quả nhanh hơn request xác thực đúng. Do đó hacker có thể dò ra token bằng cách gửi request liên tục và đo thời gian trả về.

### 2. High-Performance Enhancements & Scalability
- [ ] **gRPC Server Optimization (Phase 1.3)**:
  - Rewrite `GrpcHandler` to use dedicated threads/CompletionQueues instead of the current 1ms epoll timer polling.
  - Implement a Request Router (round-robin/hash) to properly distribute incoming gRPC streams to the `WorkerPool`.
- [ ] **Raft Consensus (Replication)**:
  - Integrate eBay's `NuRaft`. Master-Follower model (Quorum of 3). 
  - Mechanism: Dùng RocksDB làm `log_store`. State machine = áp dụng vào Cuckoo Table sau khi quorum consensus. Write performance sẽ drop, nhưng Read vẫn giữ được hiệu năng ops/sec.

### 3. Các lỗi nghiêm trọng đã phát hiện:

#### 21-03-2026

- [x] Phát hiện toàn bộ logic điều khiển của CLI chỉ tác động đến chế độ chạy trong terminal (trên file main.cpp), không hề tác động đến chế độ chạy server (trên file kallisto_server.cpp).

- [x] CLI khởi tạo và bao bọc toàn bộ bằng class `KallistoServer` (src/kallisto.cpp). Server hoàn toàn bỏ sọt class `KallistoServer`. Trong kallisto_server.cpp đang tự khởi tạo lại các biến shared_ptr<ShardedCuckooTable> và shared_ptr<RocksDBStorage>, sau đó ném cho HttpHandler và GrpcHandler chỉ đơn thuần gọi `persistence_->put()`, phó mặc hoàn toàn cho cấu hình async mặc định của RocksDB tự bơi (Nó vĩnh viễn kẹt ở Batch Mode trần trụi nhất).

- [x] CLI có biến đếm `unsaved_ops_count`, cấu hình `SyncMode::IMMEDIATE / BATCH` và tự động kích hoạt `check_and_sync()` để ép đĩa Flush. Server không hề biết những thứ đó tồn tại. HttpHandler và GrpcHandler chỉ đơn thuần gọi `persistence_->put()`, phó mặc hoàn toàn cho cấu hình async mặc định của RocksDB. Nó vĩnh viễn kẹt ở Batch Mode.

- [x] **Bỏ quên dữ liệu TTL (Time-To-Live)**: Khi nhập lệnh PUT trên CLI, `entry.ttl` được hardcode gán bằng 3600 (1 tiếng) (src/kallisto.cpp:71). Khi bắn request `POST /v1/secret/data/...` trên Server, `HttpHandler` và `GrpcHandler` chỉ gán key, value, created_at nhưng lại cố tình bỏ quên gán TTL, dẫn đến `entry.ttl` bị dính rác mặc định (uninitialized memory hoặc 0).

- [x] **Lặp code (Code Duplication) ở B-Tree Firewall**: Các thao tác cực kỳ cốt lõi như Ghi chú đường dẫn vào B-Tree (Step 0) hoặc Check Cache Miss đẩy xuống RocksDB (Step 2)... thay vì nằm trong một Repository pattern chung, thì nó lại copy-paste y hệt dán vào khắp 3 file (`kallisto.cpp`, `http_handler.cpp` và `grpc_handler.cpp`).

---

## 📜 IMPLEMENTATION HISTORY (COMPLETED)

*The following sections contain context and patterns already deployed in the codebase.*

### Phase 1.1: Threading Infrastructure (Envoy-Style)
- **Status:** COMPLETE
- **Architecture:** `Dispatcher` (epoll event loop with timerfd/eventfd) -> `WorkerPool` -> `Worker` -> Per-thread `Thread-Local Storage` (zero-lock).
- **Core Files:** `dispatcher.hpp/cpp`, `worker.hpp/cpp`, `thread_local_impl.cpp`.

### Phase 1.2: Sharded CuckooTable
- **Status:** COMPLETE
- **Architecture:** Solved global `shared_mutex` lock contention. Partitioned CuckooTable into 64 isolated shards (locks).
- **Result:** ~1.17M RPS on MIXED workloads (4-6x improvement over un-sharded).
- **Core Files:** `sharded_cuckoo_table.hpp/cpp`.

### Phase 2: High-Performance Server & Networking Layer
- **Status:** COMPLETE
- **Architecture:** Thread-per-Core model. `SO_REUSEPORT` kernel load balancing across identical bound worker ports. Built-in zero-copy HTTP/1.1 Vault KV v2 parser (`simdjson`).
- **Stability Fixes:** `Dispatcher` Use-After-Free (solved via deferred mutations - Pending Add/Remove queues).
- **Security Fix:** Re-enabled B-Tree Path indexing logic during startup/rebuild from RocksDB iterators to prevent DB-bypass DoS vulnerability.

### Phase 3: RocksDB Persistence Dual-Write
- **Status:** COMPLETE
- **Architecture:** Hybrid Storage Engine. `ShardedCuckooTable` as O(1) Hot-Cache, `RocksDB` as persistent Write-Ahead Log (WAL).
- **Data Flow:** PUT asynchronously writes to RocksDB -> Update CuckooTable. GET hits Cuckoo directly (sub-microsecond), cache-miss defaults to reading RocksDB.
- **Core Files:** `rocksdb_storage.hpp/cpp`.

### Phase 4: Clean up code và The Big Hunt

- [x] **Văng pthread lock Invalid argument (Core dumped) trên lệnh CLI yêu cầu EXIT**: Vấn đề nằm ở thứ tự khởi tạo và hủy các biến static/global trong C++: Trong main.cpp khai báo con trỏ server ở biến Global `std::unique_ptr<kallisto::KallistoServer> server;` `Class Logger::getInstance()` lại dùng function-local static variable. Khi gõ EXIT, chương trình chạy lệnh exit(0). Lệnh này kích hoạt tự động việc hủy (Destruct) các biến static/global theo chiều ngược lại so với lúc chúng sinh ra. Do đó, Logger (chứa `std::mutex`) bị hủy trước con trỏ server. Vài micro-giây sau khi Logger biến mất, ~KallistoServer() mới chạy và kéo theo `~RocksDBStorage()`. Hàm này lại cố gọi LOG_INFO("[ROCKSDB] Database closed.") để print ra màn hình. Khi đó nó cố lock một cái Mutex đã biến mất dẫn đến Invalid argument.

Khắc phục: Gọi hàm `server.reset();` ngay phía trên `exit(0)`. Điều này ép server đóng một cách an toàn và giải phóng RocksDB trước khi cơ chế dọn dẹp static của C++ quét tới hàm Logger.

### Phase 4: "The One True Core" & UDS Admin CLI
- **Status:** COMPLETE
- **Architecture:** Eliminated Split-Brain architecture. Introduced `KallistoEngine` Repository encapsulating all storage layers (B-Tree, Cuckoo, RocksDB, TTL Management). Handlers are now purely unopinionated I/O routers.
- **Security:** Removed legacy REPL. Implemented thin UDS Admin CLI securely bound to `/var/run/kallisto.sock` using OS-level `0600` permissions.
- **Testing:** Comprehensive Test-Driven Development (TDD) resulting in 100% test pass rate with coverage profiling.
- **Core Files:** `kallisto_engine.hpp/cpp`, `uds_admin_handler.hpp/cpp`, `main.cpp`.