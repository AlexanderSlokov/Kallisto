# Kallisto engine

*Fast like Redis, API request it? just like Vault.*

*Sound like something uses RocksDB? Hell yes, and it is a lovely daughter of Envoy proxy.*

*Plus, it can cluster up using NuRaft.*

Kallisto is a high-performance secret management system built with C++20. It provides a secure and efficient way to store and retrieve secrets, with a focus on performance and scalability.

And yes, we use C++20, not Rust, because 100 years of life-span is not long enough for us to fight with the borrow checker and C++ as the same time.

# HOW TO USE

Kallisto provides **two interfaces**: a **CLI (Command Line Interface)** for interactive local usage, and a **Server mode** with HTTP + gRPC APIs for production deployment.

## Prerequisites

- **C++20 compiler** (GCC 13+ or Clang 16+)
- **CMake** 3.20+
- **vcpkg** (only for Server mode — provides gRPC, Protobuf, simdjson)

## Building

### Core Build (CLI only — no external dependencies)

```bash
make build
```

### Server Build (HTTP + gRPC — requires vcpkg)

First time compiling, it will take a while to install dependencies with vcpkg (~30 min, and will use cache after first run)

```bash
export VCPKG_ROOT=/usr/local/vcpkg
make build-server
```

## CLI Mode (Interactive REPL)

Start the interactive CLI:

```bash
make run
```

### Available Commands

It feels like the Redis CLI, but it is not Redis.

| Command | Description | Example |
|---------|-------------|---------|
| `PUT <path> <key> <value>` | Store a secret | `PUT /prod/db password s3cret` |
| `GET <path> <key>` | Retrieve a secret | `GET /prod/db password` |
| `DEL <path> <key>` | Delete a secret | `DEL /prod/db password` |
| `BENCH <count>` | Run performance benchmark | `BENCH 1000000` |
| `SAVE` | Force flush to disk | `SAVE` |
| `MODE <STRICT\|BATCH>` | Set persistence mode | `MODE BATCH` |
| `LOGLEVEL <LEVEL>` | Set log verbosity | `LOGLEVEL DEBUG` |
| `HELP` | Show all commands | `HELP` |
| `EXIT` | Quit | `EXIT` |

### Usage Example

```bash
# Put a secret
> PUT /prod/db password super-secret-123
OK

# Get a secret
> GET /prod/db password
super-secret-123

# Delete a secret
> DEL /prod/db password
OK

# Set mode to BATCH
> MODE BATCH
OK (Mode: BATCH)

# Run performance benchmark
> BENCH 1000000
Write Time: 4.4811s | RPS: 223158.4057
Read Time : 2.7826s | RPS: 359379.4067
Hits      : 1000000/1000000
> SAVE
OK (Saved to disk)
```

## Server Mode

The server uses an **Envoy-style SO_REUSEPORT** architecture with a thread-per-core model. Each worker thread binds its own listener socket; the kernel distributes connections, eliminating central bottlenecks.

### Starting the Server

```bash
make run-server
```

Or with custom options:

```bash
./build/kallisto_server --http-port=8200 --grpc-port=8201 --workers=8
```

### Server CLI Options

| Option | Default | Description |
|--------|---------|-------------|
| `--http-port=PORT` | `8200` | HTTP API port (Vault-compatible) |
| `--grpc-port=PORT` | `8201` | gRPC API port |
| `--workers=N` | CPU cores | Number of worker threads |
| `--help`, `-h` | — | Show help |

### Expected Startup Output

```bash
========================================
  Kallisto Secret Server v0.1.0
  HTTP port:  8200
  gRPC port:  8201
  Workers:    8
========================================
[SERVER] Kallisto is READY. Accepting connections.
[SERVER] Press Ctrl+C to shutdown.
```

## HTTP API (Vault KV v2 Compatible)

Kallisto exposes a Vault-compatible HTTP API on port **8200** by default. All endpoints use the `/v1/secret/data/` prefix, matching HashiCorp Vault's KV v2 API.

### Store a Secret

```bash
curl -X POST http://localhost:8200/v1/secret/data/myapp/db-password \
  -H "Content-Type: application/json" \
  -d '{"data":{"value":"super-secret-123"}}'
```

Response:
```json
{"data":{"created":true}}
```

### Retrieve a Secret

```bash
curl http://localhost:8200/v1/secret/data/myapp/db-password
```

Response:
```json
{
  "data": {
    "data": {
      "value": "super-secret-123"
    }
  },
  "metadata": {
    "created_time": 1771065876
  }
}
```

### Delete a Secret

```bash
curl -X DELETE http://localhost:8200/v1/secret/data/myapp/db-password
```

Response: `204 No Content`

### Error Handling

```bash
# Requesting a non-existent secret
curl http://localhost:8200/v1/secret/data/does-not-exist
```

Response:
```json
{"errors":["Secret not found"]}
```

| Status Code | Meaning |
|-------------|---------|
| `200` | Success |
| `204` | Deleted successfully (no body) |
| `400` | Bad request (chunked encoding, Expect header) |
| `404` | Secret not found / invalid route |
| `405` | Method not allowed |
| `500` | Internal error |

## gRPC API

Kallisto exposes a `SecretService` on port **8201** with gRPC reflection enabled (inspectable with `grpcurl`).

### Service Definition

```protobuf
service SecretService {
  rpc Get(GetRequest) returns (GetResponse);
  rpc Put(PutRequest) returns (PutResponse);
  rpc Delete(DeleteRequest) returns (DeleteResponse);
  rpc List(ListRequest) returns (ListResponse);
}
```

### Example with grpcurl

```bash
# List available services
grpcurl -plaintext localhost:8201 list

# Store a secret
grpcurl -plaintext -d '{"path":"myapp/db-pass","value":"c2VjcmV0"}' \
  localhost:8201 kallisto.SecretService/Put

# Retrieve a secret
grpcurl -plaintext -d '{"path":"myapp/db-pass"}' \
  localhost:8201 kallisto.SecretService/Get

# List all secrets
grpcurl -plaintext -d '{"prefix":"myapp/","limit":10}' \
  localhost:8201 kallisto.SecretService/List

# Delete a secret
grpcurl -plaintext -d '{"path":"myapp/db-pass"}' \
  localhost:8201 kallisto.SecretService/Delete
```

## Makefile Targets

| Target | Description |
|--------|-------------|
| `make build` | Build core (CLI only) |
| `make build-server` | Build with gRPC/HTTP server |
| `make run` | Start interactive CLI |
| `make run-server` | Start the Kallisto server |
| `make test` | Run unit tests |
| `make test-listener` | Run SO_REUSEPORT tests |
| `make test-threading` | Run threading tests |
| `make benchmark-batch` | Benchmark 1M ops (Batch mode) |
| `make benchmark-strict` | Benchmark 5K ops (Strict mode) |
| `make benchmark-multithread` | Multi-threaded benchmark |
| `make benchmark-p99` | Latency p99 benchmark |
| `make benchmark-dos` | DoS resistance benchmark |
| `make clean` | Remove build artifacts |
| `make logs` | View server logs |

# Architecture Overview

```markdown
┌─────────────────────────────────────────────────────┐
│                  Kallisto Server                    │
│                                                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐           │
│  │ Worker 0 │  │ Worker 1 │  │ Worker N │   ...     │
│  │ ┌──────┐ │  │ ┌──────┐ │  │ ┌──────┐ │           │
│  │ │epoll │ │  │ │epoll │ │  │ │epoll │ │           │
│  │ └──┬───┘ │  │ └──┬───┘ │  │ └──┬───┘ │           │
│  │    │     │  │    │     │  │    │     │           │
│  │ ┌──┴───┐ │  │ ┌──┴───┐ │  │ ┌──┴───┐ │           │
│  │ │HTTP  │ │  │ │HTTP  │ │  │ │HTTP  │ │  :8200    │
│  │ │Listen│ │  │ │Listen│ │  │ │Listen│ │           │
│  │ └──────┘ │  │ └──────┘ │  │ └──────┘ │           │
│  │ ┌──────┐ │  │ ┌──────┐ │  │ ┌──────┐ │           │
│  │ │gRPC  │ │  │ │gRPC  │ │  │ │gRPC  │ │  :8201    │
│  │ │CQ    │ │  │ │CQ    │ │  │ │CQ    │ │           │
│  │ └──────┘ │  │ └──────┘ │  │ └──────┘ │           │
│  └──────────┘  └──────────┘  └──────────┘           │
│                      │                              │
│              SO_REUSEPORT                           │
│         (Kernel distributes conns)                  │
│                      │                              │
│          ┌───────────┴──────────┐                   │
│          │  ShardedCuckooTable  │                   │
│          │  (64 shards, 1M+)    │                   │
│          └──────────────────────┘                   │
└─────────────────────────────────────────────────────┘
```

Each worker is independent — zero lock contention, zero context switching. The kernel's `SO_REUSEPORT` distributes incoming connections, achieving near-linear scaling with CPU cores.