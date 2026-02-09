# Kallisto Server Implementation Plan

## Mục tiêu

Biến Kallisto từ CLI tool thành **Production-Grade Secret Server** với khả năng:
- Xử lý concurrent network requests
- Vault-compatible API
- High-performance (target: 1M+ ops/sec reads)

---

## User Review Required

> [!IMPORTANT]
> **API Strategy Decision**: Chọn 1 trong 2 phương án dưới đây

### Option A: Dual Protocol (Recommended) ⭐

```
┌─────────────────────────────────────────────────────────────────┐
│                      KALLISTO SERVER                             │
│                                                                  │
│  ┌──────────────┐     ┌──────────────┐     ┌────────────────┐   │
│  │   HTTP/JSON  │     │    gRPC     │     │   WorkerPool   │   │
│  │   :8200      │     │   :8201     │     │    (N cores)   │   │
│  └──────┬───────┘     └──────┬───────┘     └────────┬───────┘   │
│         │                     │                      │           │
│         └──────────┬──────────┘                      │           │
│                    ▼                                 ▼           │
│         ┌──────────────────┐        ┌────────────────────────┐  │
│         │  Request Router  │───────►│  ShardedCuckooTable   │  │
│         └──────────────────┘        │     (64 partitions)    │  │
│                                     └────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

| Port | Protocol | Purpose |
|------|----------|---------|
| 8200 | HTTP/JSON | Vault-compatible API (`/v1/secret/*`) |
| 8201 | gRPC | High-performance internal API |

**Pros:**
- ✅ Drop-in replacement cho Vault clients (Terraform, CLI, SDKs)
- ✅ gRPC cho internal microservices (10-20x faster than HTTP)
- ✅ Flexibility: migrate gradually

**Cons:**
- ❌ Complexity: 2 protocol handlers
- ❌ More code to maintain

---

### Option B: gRPC Only + Gateway

```
┌───────────────────────────────────────────────────────────────┐
│                                                               │
│  External   ┌───────────────┐     ┌─────────────────────────┐│
│  HTTP ─────►│  grpc-gateway │────►│   KALLISTO (gRPC only) ││
│  Clients    │   (Go proxy)  │     │        :8201            ││
│             └───────────────┘     └─────────────────────────┘│
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

**Pros:**
- ✅ Simple: 1 protocol
- ✅ Auto-generate REST from proto files

**Cons:**
- ❌ Extra hop (latency penalty)
- ❌ Need separate grpc-gateway service
- ❌ Not true Vault API compatibility

---

> [!CAUTION]
> **Recommendation**: Option A (Dual Protocol) vì Vault ecosystem rất lớn (Terraform, Consul, Nomad). Breaking compatibility = breaking adoption.

---

## Proposed Changes

### Component 1: Protocol Buffer Definitions

#### [NEW] [kallisto.proto](file:///e:/GitHub/kallisto/proto/kallisto.proto)

```protobuf
syntax = "proto3";
package kallisto;

service SecretService {
  rpc Get(GetRequest) returns (GetResponse);
  rpc Put(PutRequest) returns (PutResponse);
  rpc Delete(DeleteRequest) returns (DeleteResponse);
  rpc List(ListRequest) returns (ListResponse);
}

message GetRequest {
  string path = 1;  // e.g., "secret/data/myapp/db_password"
}

message GetResponse {
  bytes value = 1;
  map<string, string> metadata = 2;
}
// ... more messages
```

---

### Component 2: gRPC Server Implementation

#### [NEW] [grpc_server.hpp](file:///e:/GitHub/kallisto/include/kallisto/server/grpc_server.hpp)

Core gRPC server với async completion queue pattern:

```cpp
class GrpcServer {
public:
    void start(uint16_t port, WorkerPool& workers);
    void shutdown();
private:
    std::unique_ptr<grpc::Server> server_;
    std::vector<std::unique_ptr<grpc::ServerCompletionQueue>> cqs_;
};
```

#### [NEW] [grpc_server.cpp](file:///e:/GitHub/kallisto/src/server/grpc_server.cpp)

- 1 CompletionQueue per Worker (avoids lock contention)
- Async request handling via `dispatcher_->post()`

---

### Component 3: HTTP/JSON Server (Vault-compatible)

#### [NEW] [http_server.hpp](file:///e:/GitHub/kallisto/include/kallisto/server/http_server.hpp)

Lightweight HTTP server using epoll directly:

```cpp
class HttpServer {
public:
    void start(uint16_t port, WorkerPool& workers);
    void shutdown();
private:
    void handleVaultApi(HttpRequest& req, HttpResponse& resp);
    // Vault API compatibility:
    // GET  /v1/secret/data/:path  -> lookup
    // POST /v1/secret/data/:path  -> insert
    // DELETE /v1/secret/data/:path -> remove
};
```

#### [NEW] [http_server.cpp](file:///e:/GitHub/kallisto/src/server/http_server.cpp)

---

### Component 4: Main Server Entry Point

#### [NEW] [kallisto_server.cpp](file:///e:/GitHub/kallisto/src/kallisto_server.cpp)

```cpp
int main(int argc, char** argv) {
    // 1. Parse config (ports, worker count, etc.)
    Config config = parseArgs(argc, argv);
    
    // 2. Initialize ThreadLocal Storage
    auto tls = tls::createThreadLocalInstance();
    
    // 3. Create WorkerPool (N workers, each with its own epoll)
    WorkerPool pool(config.num_workers);
    
    // 4. Create shared storage
    auto storage = std::make_shared<ShardedCuckooTable>(config.capacity);
    
    // 5. Start servers
    HttpServer http_server;
    GrpcServer grpc_server;
    
    http_server.start(8200, pool, storage);
    grpc_server.start(8201, pool, storage);
    
    // 6. Wait for shutdown signal
    waitForSignal();
    
    // 7. Graceful shutdown
    http_server.shutdown();
    grpc_server.shutdown();
    pool.stop();
}
```

---

### Component 5: CMake Integration

#### [MODIFY] [CMakeLists.txt](file:///e:/GitHub/kallisto/CMakeLists.txt)

Add dependencies:
- gRPC + Protobuf
- Optional: RapidJSON (for HTTP/JSON parsing)

---

## Verification Plan

### Automated Tests

#### 1. Unit Test: gRPC Server

```bash
# File: tests/test_grpc_server.cpp
# Command to run:
cd e:\GitHub\kallisto\build && ctest -R test_grpc -V
```

Test cases:
- `TestGrpcPut` - Insert secret via gRPC
- `TestGrpcGet` - Retrieve secret via gRPC
- `TestGrpcDelete` - Remove secret via gRPC
- `TestGrpcConcurrent` - 1000 concurrent requests

#### 2. Integration Test: Vault API Compatibility

```bash
# File: tests/test_vault_api.cpp
# Command to run:
cd e:\GitHub\kallisto\build && ctest -R test_vault -V
```

Test cases:
- `TestVaultKvV2Get` - `GET /v1/secret/data/:path`
- `TestVaultKvV2Put` - `POST /v1/secret/data/:path`
- `TestVaultKvV2Delete` - `DELETE /v1/secret/data/:path`

#### 3. Load Test: Throughput Benchmark

```bash
# File: tests/bench_server.cpp
# Command to run:
cd e:\GitHub\kallisto\build && ./bench_server --threads=4 --duration=10s
```

---

### Manual Verification

> [!NOTE]
> Sau khi implement, cần bạn confirm các bước manual test sau:

1. **Start server**:
   ```bash
   ./kallisto_server --http-port=8200 --grpc-port=8201 --workers=4
   ```

2. **Test Vault compatibility với curl**:
   ```bash
   # PUT secret
   curl -X POST http://localhost:8200/v1/secret/data/myapp -d '{"data":{"password":"secret123"}}'
   
   # GET secret
   curl http://localhost:8200/v1/secret/data/myapp
   ```

3. **Test gRPC với grpcurl** (cần cài grpcurl):
   ```bash
   grpcurl -plaintext -d '{"path":"myapp"}' localhost:8201 kallisto.SecretService/Get
   ```

---

## Implementation Order

| Phase | Component | Estimated Effort |
|-------|-----------|------------------|
| 1 | Proto definitions + CMake gRPC setup | 2-3 giờ |
| 2 | gRPC Server (async) | 4-6 giờ |
| 3 | HTTP Server (Vault API) | 4-6 giờ |
| 4 | Main entry point + config | 2-3 giờ |
| 5 | Tests + Benchmarks | 3-4 giờ |

**Total estimated: 15-22 giờ**

---

## Questions for User

1. **API Strategy**: Chọn Option A (Dual) hay Option B (gRPC only)?
2. **Additional features for Phase 1**:
   - Authentication (Token-based)?
   - TLS/mTLS?
   - Metrics endpoint (`/metrics` for Prometheus)?
3. **RocksDB integration**: Làm luôn trong phase này hay để phase sau?
