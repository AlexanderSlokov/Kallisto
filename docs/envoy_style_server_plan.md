# Kallisto Server: Envoy-Style SO_REUSEPORT Architecture

Biến Kallisto từ CLI tool thành **Production-Grade Secret Server** với kiến trúc Envoy-style: mỗi Worker tự accept connections, zero context switch, kernel load-balancing qua SO_REUSEPORT.

## User Review Required

> [!IMPORTANT]  
> **Estimated effort: 30-35 giờ** - Đây là đường khó hơn nhưng cho performance tốt nhất (1M+ RPS).

> [!WARNING]
> **gRPC + epoll integration phức tạp**: Cần bridge gRPC CompletionQueue với Dispatcher qua eventfd. Có thể gặp edge cases.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        KERNEL (SO_REUSEPORT)                        │
│   Connections distributed directly to Worker accept() calls        │
└───────────┬──────────────────┬──────────────────┬──────────────────┘
            │                  │                  │
            ▼                  ▼                  ▼
┌───────────────────┐ ┌───────────────────┐ ┌───────────────────┐
│     Worker 0      │ │     Worker 1      │ │     Worker N      │
│ ┌───────────────┐ │ │ ┌───────────────┐ │ │ ┌───────────────┐ │
│ │  Dispatcher   │ │ │ │  Dispatcher   │ │ │ │  Dispatcher   │ │
│ │   (epoll)     │ │ │ │   (epoll)     │ │ │ │   (epoll)     │ │
│ └───────┬───────┘ │ │ └───────┬───────┘ │ │ └───────┬───────┘ │
│         │         │ │         │         │ │         │         │
│ ┌───────▼───────┐ │ │ ┌───────▼───────┐ │ │ ┌───────▼───────┐ │
│ │ HTTP Handler  │ │ │ │ HTTP Handler  │ │ │ │ HTTP Handler  │ │
│ │ gRPC Handler  │ │ │ │ gRPC Handler  │ │ │ │ gRPC Handler  │ │
│ └───────┬───────┘ │ │ └───────┬───────┘ │ │ └───────┬───────┘ │
│         │         │ │         │         │ │         │         │
│ ┌───────▼───────┐ │ │ ┌───────▼───────┐ │ │ ┌───────▼───────┐ │
│ │ ShardedCuckoo │ │ │ │ ShardedCuckoo │ │ │ │ ShardedCuckoo │ │
│ │   (shared)    │ │ │ │   (shared)    │ │ │ │   (shared)    │ │
│ └───────────────┘ │ │ └───────────────┘ │ │ └───────────────┘ │
└───────────────────┘ └───────────────────┘ └───────────────────┘
```

**Key differences from Opus's plan:**
- ❌ No central "Request Router"
- ✅ Each Worker calls `accept()` directly with SO_REUSEPORT
- ✅ Zero queue contention, zero context switch
- ✅ Kernel does the load balancing

---

## Proposed Changes

### Component 1: Protocol Buffer Definitions

#### [NEW] [kallisto.proto](file:///workspaces/kallisto/proto/kallisto.proto)

```protobuf
syntax = "proto3";
package kallisto;

option cc_generic_services = false;

service SecretService {
  rpc Get(GetRequest) returns (GetResponse);
  rpc Put(PutRequest) returns (PutResponse);
  rpc Delete(DeleteRequest) returns (DeleteResponse);
  rpc List(ListRequest) returns (ListResponse);
}

message GetRequest {
  string path = 1;
}

message GetResponse {
  bytes value = 1;
  int64 created_at = 2;
  int64 expires_at = 3;
}

message PutRequest {
  string path = 1;
  bytes value = 2;
  int64 ttl_seconds = 3;  // 0 = no expiry
}

message PutResponse {
  bool success = 1;
  string error = 2;
}

message DeleteRequest {
  string path = 1;
}

message DeleteResponse {
  bool success = 1;
}

message ListRequest {
  string prefix = 1;
  int32 limit = 2;
}

message ListResponse {
  repeated string paths = 1;
}
```

---

### Component 2: SO_REUSEPORT Listener

#### [NEW] [listener.hpp](file:///workspaces/kallisto/include/kallisto/net/listener.hpp)

Socket wrapper với SO_REUSEPORT để nhiều workers có thể bind cùng port:

```cpp
namespace kallisto::net {

class Listener {
public:
    // Create listening socket with SO_REUSEPORT
    static int createListenSocket(uint16_t port, bool reuseport = true);
    
    // Accept connection (non-blocking)
    static int acceptConnection(int listen_fd, struct sockaddr_in* addr);
    
    // Set socket options
    static void setNonBlocking(int fd);
    static void setTcpNoDelay(int fd);
};

} // namespace kallisto::net
```

#### [NEW] [listener.cpp](file:///workspaces/kallisto/src/net/listener.cpp)

```cpp
int Listener::createListenSocket(uint16_t port, bool reuseport) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    
    int enable = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    
    if (reuseport) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
    }
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(fd, SOMAXCONN);
    
    return fd;
}
```

---

### Component 3: Worker Extension

#### [MODIFY] [worker.hpp](file:///workspaces/kallisto/include/kallisto/event/worker.hpp)

Add listener binding capability:

```diff
class Worker {
public:
+   /**
+    * Bind a listening socket to this worker's epoll.
+    * Uses SO_REUSEPORT so multiple workers can bind same port.
+    */
+   virtual void bindListener(uint16_t port, 
+                             std::function<void(int client_fd)> on_accept) = 0;
```

#### [MODIFY] [worker_impl.cpp](file:///workspaces/kallisto/src/event/worker_impl.cpp)

```cpp
void WorkerImpl::bindListener(uint16_t port, 
                              std::function<void(int client_fd)> on_accept) {
    // Each worker creates its own socket with SO_REUSEPORT
    int listen_fd = Listener::createListenSocket(port, true);
    
    // Add to this worker's epoll
    dispatcher_->addFd(listen_fd, EPOLLIN, [=](uint32_t events) {
        while (true) {
            int client_fd = Listener::acceptConnection(listen_fd, nullptr);
            if (client_fd < 0) break;  // EAGAIN
            on_accept(client_fd);
        }
    });
    
    listen_fds_.push_back(listen_fd);
}
```

---

### Component 4: gRPC Handler (Async + Dispatcher Integration)

#### [NEW] [grpc_handler.hpp](file:///workspaces/kallisto/include/kallisto/server/grpc_handler.hpp)

Integrate gRPC CompletionQueue với Dispatcher thông qua eventfd:

```cpp
namespace kallisto::server {

class GrpcHandler {
public:
    GrpcHandler(event::Dispatcher& dispatcher, 
                std::shared_ptr<ShardedCuckooTable> storage);
    
    // Initialize gRPC server bound to this worker
    void initialize(uint16_t port);
    
    // Shutdown gracefully
    void shutdown();

private:
    void pollCompletionQueue();
    
    event::Dispatcher& dispatcher_;
    std::shared_ptr<ShardedCuckooTable> storage_;
    std::unique_ptr<grpc::Server> server_;
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;
    int notify_fd_;  // eventfd to bridge CQ -> epoll
};

} // namespace kallisto::server
```

#### [NEW] [grpc_handler.cpp](file:///workspaces/kallisto/src/server/grpc_handler.cpp)

```cpp
void GrpcHandler::initialize(uint16_t port) {
    // Create eventfd for CQ -> Dispatcher notification
    notify_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    
    // Build gRPC server
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:" + std::to_string(port), 
                             grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
    
    // Integrate CQ polling with Dispatcher
    // Option 1: Use timer to poll CQ periodically (simpler)
    poll_timer_ = dispatcher_.createTimer([this]() {
        pollCompletionQueue();
        poll_timer_->enableTimer(1);  // Poll every 1ms
    });
    poll_timer_->enableTimer(1);
}

void GrpcHandler::pollCompletionQueue() {
    void* tag;
    bool ok;
    
    // Non-blocking poll
    while (cq_->AsyncNext(&tag, &ok, 
           gpr_time_0(GPR_CLOCK_REALTIME)) == grpc::CompletionQueue::GOT_EVENT) {
        auto* call = static_cast<CallData*>(tag);
        call->Proceed(ok);
    }
}
```

---

### Component 5: HTTP Handler (Vault-Compatible)

#### [NEW] [http_handler.hpp](file:///workspaces/kallisto/include/kallisto/server/http_handler.hpp)

Minimal HTTP/1.1 parser cho Vault KV v2 API:

```cpp
namespace kallisto::server {

class HttpHandler {
public:
    HttpHandler(event::Dispatcher& dispatcher,
                std::shared_ptr<ShardedCuckooTable> storage);
    
    // Called when new client connection accepted
    void onNewConnection(int client_fd);

private:
    void onReadable(int fd);
    void onWritable(int fd);
    
    // Vault API handlers
    void handleGetSecret(int fd, const std::string& path);
    void handlePutSecret(int fd, const std::string& path, std::string_view body);
    void handleDeleteSecret(int fd, const std::string& path);
    
    event::Dispatcher& dispatcher_;
    std::shared_ptr<ShardedCuckooTable> storage_;
};

// Vault API Routes:
// GET  /v1/secret/data/:path  -> lookup
// POST /v1/secret/data/:path  -> insert
// DELETE /v1/secret/data/:path -> remove

} // namespace kallisto::server
```

---

### Component 6: Main Server Entry Point

#### [NEW] [kallisto_server.cpp](file:///workspaces/kallisto/src/kallisto_server.cpp)

```cpp
int main(int argc, char** argv) {
    // Parse config
    uint16_t http_port = 8200;
    uint16_t grpc_port = 8201;
    size_t num_workers = std::thread::hardware_concurrency();
    
    // Create shared storage
    auto storage = std::make_shared<ShardedCuckooTable>(1024 * 1024);
    
    // Create TLS instance
    auto tls = tls::createThreadLocalInstance();
    
    // Create WorkerPool
    auto pool = createWorkerPool(num_workers);
    
    // Start workers with listeners
    pool->start([&]() {
        for (size_t i = 0; i < pool->size(); ++i) {
            auto& worker = pool->getWorker(i);
            
            // Each worker binds HTTP port (SO_REUSEPORT)
            auto http_handler = std::make_shared<HttpHandler>(
                worker.dispatcher(), storage);
            worker.bindListener(http_port, [http_handler](int fd) {
                http_handler->onNewConnection(fd);
            });
            
            // Each worker binds gRPC port (SO_REUSEPORT)
            auto grpc_handler = std::make_shared<GrpcHandler>(
                worker.dispatcher(), storage);
            grpc_handler->initialize(grpc_port);
        }
    });
    
    info("[SERVER] Kallisto running on HTTP:" + std::to_string(http_port) +
         " gRPC:" + std::to_string(grpc_port));
    
    // Wait for shutdown signal
    std::signal(SIGINT, [](int) { /* set flag */ });
    std::signal(SIGTERM, [](int) { /* set flag */ });
    
    while (!shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    pool->stop();
    return 0;
}
```

---

### Component 7: CMake Updates

#### [MODIFY] [CMakeLists.txt](file:///workspaces/kallisto/CMakeLists.txt)

```cmake
# Find gRPC and Protobuf
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Generate proto files
set(PROTO_FILES proto/kallisto.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_FILES})

# Add simdjson for HTTP JSON parsing
include(FetchContent)
FetchContent_Declare(simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG v3.6.0
)
FetchContent_MakeAvailable(simdjson)

# Server library
add_library(kallisto_server_lib
    src/net/listener.cpp
    src/server/grpc_handler.cpp
    src/server/http_handler.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)
target_link_libraries(kallisto_server_lib 
    kallisto_lib 
    gRPC::grpc++ 
    protobuf::libprotobuf
    simdjson
)

# Server executable
add_executable(kallisto_server src/kallisto_server.cpp)
target_link_libraries(kallisto_server kallisto_server_lib)
```

---

## Verification Plan

### Automated Tests

#### 1. Unit Test: SO_REUSEPORT Listener

```bash
# File: tests/test_listener.cpp
# Command:
cd /workspaces/kallisto/build && make test_listener && ./test_listener
```

Test cases:
- `TestMultipleWorkersBindSamePort` - 4 workers bind port 9999
- `TestAcceptDistribution` - connections distributed across workers
- `TestNonBlockingAccept` - EAGAIN when no pending connections

#### 2. Unit Test: gRPC Handler

```bash
# File: tests/test_grpc_handler.cpp
# Command:
cd /workspaces/kallisto/build && make test_grpc_handler && ./test_grpc_handler
```

Test cases:
- `TestGrpcPut` - Insert secret via gRPC
- `TestGrpcGet` - Retrieve secret via gRPC
- `TestGrpcNotFound` - Get non-existent key

#### 3. Integration Test: HTTP Vault API

```bash
# File: tests/test_vault_api.cpp
# Command:
cd /workspaces/kallisto/build && make test_vault_api && ./test_vault_api
```

Test cases:
- `TestVaultPut` - `POST /v1/secret/data/mykey`
- `TestVaultGet` - `GET /v1/secret/data/mykey`
- `TestVaultDelete` - `DELETE /v1/secret/data/mykey`

#### 4. Load Test: Throughput Benchmark

```bash
# File: tests/bench_server.cpp
# Command:
cd /workspaces/kallisto/build && make bench_server && ./bench_server --threads=4 --duration=10
```

Target: **1M+ RPS** với 4 workers trên 4-core machine.

---

### Manual Verification

> [!NOTE]
> **Bước 1: Start server**
> ```bash
> cd /workspaces/kallisto/build
> ./kallisto_server --http-port=8200 --grpc-port=8201 --workers=4
> ```

> [!NOTE]
> **Bước 2: Test Vault API với curl**
> ```bash
> # PUT secret
> curl -X POST http://localhost:8200/v1/secret/data/myapp \
>   -H "Content-Type: application/json" \
>   -d '{"data":{"password":"secret123"}}'
> 
> # GET secret
> curl http://localhost:8200/v1/secret/data/myapp
> 
> # DELETE secret
> curl -X DELETE http://localhost:8200/v1/secret/data/myapp
> ```

> [!NOTE]
> **Bước 3: Test gRPC với grpcurl**
> ```bash
> # Install grpcurl if needed: go install github.com/fullstorydev/grpcurl/cmd/grpcurl@latest
> 
> # Put secret
> grpcurl -plaintext -d '{"path":"myapp","value":"c2VjcmV0MTIz"}' \
>   localhost:8201 kallisto.SecretService/Put
> 
> # Get secret
> grpcurl -plaintext -d '{"path":"myapp"}' \
>   localhost:8201 kallisto.SecretService/Get
> ```

> [!NOTE]
> **Bước 4: Verify SO_REUSEPORT works**
> ```bash
> # Check that multiple threads are accepting connections
> ss -tlnp | grep 8200
> # Should show multiple processes/threads bound to same port
> ```

---

## Implementation Order

| Phase | Component | Files | Est. Hours |
|-------|-----------|-------|------------|
| 2.1 | Proto + CMake gRPC | `proto/kallisto.proto`, `CMakeLists.txt` | 3-4h |
| 2.2 | SO_REUSEPORT Listener | `net/listener.hpp/cpp`, modify `worker.hpp` | 4-5h |
| 2.3 | gRPC Handler | `server/grpc_handler.hpp/cpp` | 6-8h |
| 2.4 | HTTP Handler | `server/http_handler.hpp/cpp` | 6-8h |
| 2.5 | Main Entry Point | `kallisto_server.cpp` | 3-4h |
| 2.6 | Tests + Benchmarks | `tests/*.cpp` | 4-6h |

**Total: 26-35 giờ**

---

## Risk Mitigation

1. **gRPC + epoll complexity**: Start với timer-based CQ polling (1ms). Optimize với eventfd later nếu cần.

2. **HTTP parsing bugs**: Use simdjson for JSON. Keep HTTP parser minimal - only support Vault KV v2 subset.

3. **SO_REUSEPORT kernel version**: Requires Linux 3.9+. Check với `uname -r`.
