# Kallisto Project TODO & History

**Note: High-level use-case documentation has been offloaded to README.md.**

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

### 2. High-Performance Enhancements & Scalability
- [ ] **gRPC Server Optimization (Phase 1.3)**:
  - Rewrite `GrpcHandler` to use dedicated threads/CompletionQueues instead of the current 1ms epoll timer polling.
  - Implement a Request Router (round-robin/hash) to properly distribute incoming gRPC streams to the `WorkerPool`.
- [ ] **Blocked Cuckoo Table**:
  - Optimize memory/perf by migrating from "1 slot per bucket" to "4 slots per bucket". 
  - *Struct details:* `Bucket { uint16_t tags[4]; void* pointers[4]; }` (Exactly 64-bytes = 1 CPU Cache Line). Predictable 95% load factor without loop kicks.
- [ ] **Raft Consensus (Replication)**:
  - Integrate eBay's `NuRaft`. Master-Follower model (Quorum of 3). 
  - *Mechanism:* RocksDB as `log_store`. State machine = applied to Cuckoo Table post-quorum consensus. Write performance will drop, but Read stays Millions ops/sec.

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