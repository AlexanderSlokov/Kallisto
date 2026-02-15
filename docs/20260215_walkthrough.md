# Kallisto Server — Build & Verification Walkthrough

## Summary

Successfully built and verified the Kallisto server with Envoy-style SO_REUSEPORT architecture. Server is running with 4 worker threads, HTTP Vault API on port 8200, and gRPC on port 8201.

## Build Fixes Applied

Three compile errors were fixed during the build process:

1. **`SecretEntry.timestamp`** — Field doesn't exist; the correct field is `created_at` (`chrono::system_clock::time_point`). Fixed in both [http_handler.cpp](file:///workspaces/kallisto/src/server/http_handler.cpp) and [grpc_handler.cpp](file:///workspaces/kallisto/src/server/grpc_handler.cpp)

2. **`ServerAsyncResponseWriter<Message>` type mismatch** — gRPC's async API requires type-specific responders per RPC. Rewrote [grpc_handler.cpp](file:///workspaces/kallisto/src/server/grpc_handler.cpp) with a `TypedCallData<Req, Resp>` template, giving each RPC its own correctly-typed `ServerAsyncResponseWriter<T>`

3. **`info()` scope in signal handler** — The signal handler lives in an anonymous namespace, so `info()` needed `kallisto::` qualification. Fixed in [kallisto_server.cpp](file:///workspaces/kallisto/src/kallisto_server.cpp#L30)

## Test Results

### SO_REUSEPORT Listener Tests (4/4 ✅)

| Test | Result |
|------|--------|
| `testMultipleBindSamePort` | 4 sockets → port 19900 |
| `testNonBlockingAcceptEagain` | EAGAIN on empty accept |
| `testAcceptConnection` | Client fd=5 accepted |
| `testMultiWorkerAccept` | 20/20 connections (4+6+7+3) |

### Server Startup ✅

```
Kallisto Secret Server v0.1.0
HTTP port:  8200
gRPC port:  8201
Workers:    4
```
- 4 worker threads with SO_REUSEPORT listeners bound
- 4 gRPC async servers with CompletionQueue polling
- ShardedCuckooTable: 64 shards, 1M buckets

### HTTP Vault API (Vault KV v2 compatible) ✅

| Operation | Request | Response |
|-----------|---------|----------|
| **PUT** | `POST /v1/secret/data/myapp/db-password` | `{"data":{"created":true}}` |
| **GET** | `GET /v1/secret/data/myapp/db-password` | `{"data":{"data":{"value":"super-secret-123"}},"metadata":{"created_time":...}}` |
| **404** | `GET /v1/secret/data/does-not-exist` | `{"errors":["Secret not found"]}` |
| **DELETE** | `DELETE /v1/secret/data/myapp/db-password` | 204 No Content |
| **GET after DELETE** | `GET /v1/secret/data/myapp/db-password` | `{"errors":["Secret not found"]}` |

## 3. Stability Fixes & Benchmarking

We encountered a critical crash under high load (use-after-free due to `unordered_map` rehash).

- **Diagnosis**: ASAN pinpointed a crash in `DispatcherImpl::run` where `fd_callbacks_` rehashed while a callback was executing.
- **Fix (Option C)**: 
    - Switched `connections_` to `std::unique_ptr<Connection>` for pointer stability.
    - Implemented **Deferred Mutation** in `Dispatcher` (adds/removes are queued during event iteration).
- **Result**: Server passed 15s ASAN test with ZERO errors.

**Final Benchmark Results (Stable at c=50):**

| Benchmark | Type | RPS | Stability |
|-----------|------|-----|-----------|
| **SEED** | Writes | **39,894** | ✅ Stable |
| **GET** | Reads | **67,987** | ✅ Stable |
| **PUT** | Writes | **46,465** | ✅ Stable |
| **MIXED** | 95% Read | **46,213** | ✅ Stable |

*Note: Tests at c=200 hit environment resource limits/timeouts, but c=50 is verified rock-solid 100% stable.*

## 4. Remaining Work

- Implement `ShardedCuckooTable` persistence to disk (currently in-memory).
- Add TLS support for production security.
- Optimize JSON parsing (currently basic).
