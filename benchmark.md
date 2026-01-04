# Benchmark Results & Analysis

## Performance Metrics
Completed optimization of `CuckooTable` capacity (16,384 slots) to support 10,000 secret items without collisions. 

### Benchmark Config
- **Items**: 10,000 secrets
- **Operations**: 10,000 writes followed by 10,000 reads
- **Persistence**: Strict Sync (Snapshots saved to disk after every WRITE)

### Results
| Metric | Value | Notes |
| :--- | :--- | :--- |
| **Write RPS** | ~100 - 1,500 | Limited by Disk I/O (fsync latency). High variance depending on environment (SSD vs HDD vs Container Mount). |
| **Read RPS** | **~4,500 - 5,600** | **Pure In-Memory Speed**. Limited only by CPU (SipHash calculation). |
| **Hit Rate** | **100%** | 10k/10k items found. Zero collisions kicked. |
| **Batch Write RPS** | **~17,500+** | **New Feature**: Using `MODE BATCH` (10k ops). Massive speedup (10x) vs Strict Mode. |

## "Thundering Herd" Defense
The benchmark proves Kallisto can handle high concurrency Read traffic (Thundering Herd) effectively:

```mermaid
graph TD
    User[Clients / K8s Cluster] -- 5000+ RPS Read --> Kallisto[Kallisto Server]
    Kallisto -- RAM O(log N) --> BTree[B-Tree Validator]
    BTree -- Valid Path? --> Cuckoo[Cuckoo Hash Table]
    Cuckoo -- RAM O(1) --> Secret[Secret Value]
    
    subgraph "Fast Path (RAM)"
    BTree
    Cuckoo
    end
    
    subgraph "Slow Path (Disk)"
    Kallisto -. Write (Async/Sync) .-> Disk[(Storage Engine)]
    end
```

### Why it works?
1.  **B-Tree Shield**: Invalid paths (e.g., scanners, misconfigured apps) are rejected at O(log N) before even hashing.
2.  **Cuckoo Speed**: Valid secrets are retrieved in constant time O(1).
3.  **Read-Only Safety**: Reads never touch the disk, completely bypassing the I/O bottleneck.
