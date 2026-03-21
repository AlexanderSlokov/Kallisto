# Kallisto Architecture Refactoring: "The One True Core" & UDS Admin CLI

This plan defines the strategy to eliminate the "Split-Brain" architecture of Kallisto. We will consolidate the database logic into a single source of truth (`KallistoEngine`) and convert the CLI into a thin client communicating over Unix Domain Sockets (UDS). This strictly adheres to a Test-Driven Development (TDD) approach.

## User Review Required

> [!IMPORTANT]
> Please review the 4-phase plan below. Once approved, we will begin with **Phase 1: TDD & Header Definition**.

## Proposed Changes

We will execute the refactoring in 4 strict phases to ensure stability and correctness without memory leaks or regression.

### Phase 1: TDD & Header Definition
Focus on defining the public API of the Engine and writing the tests *before* the implementation.

---

#### [NEW] `include/kallisto/kallisto_engine.hpp`
Declaration of `KallistoEngine` utilizing the Repository Pattern. Encapsulates `ShardedCuckooTable`, `RocksDBStorage`, `TlsBTreeManager`, and Ops counters. Exposes only minimalistic APIs: `put()`, `get()`, `del()`, `change_sync_mode()`, `force_flush()`.
#### [NEW] `tests/test_kallisto_engine.cpp`
Google Test suite covering:
1. Initialization & basic R/W operations.
2. Fallback logic (Cache Miss resolving through RocksDB).
3. Boundary conditions (e.g., missing TTL must default safely to `3600`).

### Phase 2: Core Implementation
Implement the engine and pass the tests.

---

#### [NEW] `src/kallisto_engine.cpp`
Implementation of the Engine. Extrapolates distributed logic (B-Tree path validations, caching logic, `unsaved_ops_count`, `sync=true/false` persistence modes) from various locations into this single source of truth.

### Phase 3: Network Handlers Integration
Refactoring the network layer to act only as dumb I/O routers.

---

#### [MODIFY] [src/server/http_handler.cpp](file:///workspaces/kallisto/src/server/http_handler.cpp)
Remove direct shared pointers to RocksDB/Cuckoo. Inject `std::shared_ptr<KallistoEngine>`. Delegate all business logic to the engine.
#### [MODIFY] [src/server/grpc_handler.cpp](file:///workspaces/kallisto/src/server/grpc_handler.cpp)
Same as `http_handler.cpp`.

### Phase 4: Unix Domain Socket Admin
Replacing the fat CLI with a thin client.

---

#### [NEW] `src/server/uds_admin_handler.cpp`
A new listener that responds to UDS traffic (e.g., `/var/run/kallisto.sock`), acting as a control plane for Admin commands (SAVE, LOGLEVEL, MODE).
#### [MODIFY] [src/main.cpp](file:///workspaces/kallisto/src/main.cpp)
Refactor the current entry point. Build a "Dumb Client" that opens a UDS socket to send commands instead of instantiating the database itself.

## Verification Plan

### Automated Tests
- Run `make test` incorporating the new `test_kallisto_engine.cpp` to verify `KallistoEngine` correctly handles basic operations, Cuckoo-RocksDB fallback, and default TTL assignments.
- Ensure 100% test pass rate upon completing Phase 2.

### Manual Verification
- Start the server using `make run-server`.
- Run the new CLI `kallisto MODE STRICT` and verify it connects via UDS successfully and mutates the server state.
- Perform HTTP `POST/GET` to verify handlers function correctly with the refactored Engine.
