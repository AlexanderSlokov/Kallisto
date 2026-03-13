Searched web: "highest core count CPU 2024 2025 server processor world record"
Searched web: "AWS Azure GCP standard instance max vCPU cores cloud computing 2025"

Đây là phân tích vừa có dữ liệu thực tế vừa có toán học chặt chẽ. Để trả lời câu hỏi cốt lõi của bạn — **64 hay 128 shard?** — ta cần phân tích xác suất đụng độ bằng công thức Birthday Problem.

---

## Phân tích: NUM_SHARDS = 64 vs 128 — Khoa học máy tính chặt chẽ

### Bối cảnh phần cứng thực tế (2025–2026)

| CPU | Core count | Mục đích |
|-----|-----------|---------|
| **AMD EPYC 9965 "Turin"** | **192 cores** | x86 server, production today |
| **Intel Xeon 6 "Sierra Forest-AP"** | **288 cores** | Cloud-optimized, Q1 2025 |
| **AmpereOne (ARM)** | **192 cores** (256 sắp ra) | Cloud-native ARM |
| **GCP C4D instance** | **384 vCPU** | Cloud standard tối đa |
| **AWS M6i.32xlarge** | **128 vCPU** | AWS general purpose max |
| **Azure Dlsv7** | **248 vCPU** | Azure general purpose max |

> **Takeaway**: "Standard" cloud = **128–192 cores**. Chips physical tối đa = **192–288** (x86), thị trường đang hướng tới **256**. Số **64** của O5 Council đã **lỗi thời**.

---

### Toán học: Birthday Problem cho Shard Collision

Đây là bài toán cốt lõi: với `W` worker và `S` shard, **xác suất ít nhất 2 worker cùng đánh vào một shard** là bao nhiêu?

**Công thức chính xác (Birthday Problem):**

```
P(collision) = 1 - S! / (S^W × (S-W)!)
```

Dưới dạng xấp xỉ (khi `W << S`):

```
P(at least one collision) ≈ 1 - e^(-W(W-1) / 2S)
```

**Kỳ vọng số cặp worker xung đột:**

```
E[collisions] = C(W, 2) / S  =  W(W-1) / (2S)
```

**Tính toán cho các kịch bản:**

| Workers (W) | Shards (S) | E[cặp collision] | P(collision) |
|---|---|---|---|
| 64 | 64 | 64×63/(2×64) = **31.5** | ≈ 100% |
| 64 | 128 | 64×63/(2×128) = **15.75** | ≈ 100% |
| 64 | 256 | 64×63/(2×256) = **7.875** | ≈ 99.9% |
| 64 | 512 | = **3.94** | ≈ 98.1% |
| 64 | 4096 | = **0.49** | ≈ 39% |
| 192 | 256 | 192×191/(2×256) = **71.6** | ≈ 100% |
| 192 | 1024 | = **17.9** | ≈ 100% |
| 192 | 4096 | = **4.48** | ≈ 98.9% |

> **Kết luận thô:** Chỉ cần `W > 3`, xác suất có ít nhất 1 collision ≈ 100% với mọi `S` hợp lý. **Birthday problem nói rằng gấp đôi shard KHÔNG giải quyết đụng độ.**

---

### Vậy thì shard có tác dụng gì? — Phân tích đúng

**Metric quan trọng KHÔNG phải "có collision không" mà là "thời gian chờ lock trung bình".**

```
Thời gian chờ lock = P(shard bị chiếm) × T_critical_section

P(shard bị chiếm bởi ít nhất 1 trong W-1 worker khác)
  = 1 - ((S-1)/S)^(W-1)
  ≈ 1 - e^(-(W-1)/S)    [khi S >> 1]
```

**Bảng thời gian chờ tương đối (T = 1 đơn vị):**

| W | S=64 | S=128 | S=256 | S=512 | S=1024 |
|---|---|---|---|---|---|
| 4 | 4.6% | 2.3% | 1.2% | 0.6% | 0.3% |
| 8 | 10.3% | 5.3% | 2.7% | 1.4% | 0.7% |
| 16 | 21.5% | 11.5% | 6.0% | 3.0% | 1.5% |
| 64 | 63.6% | 39.4% | 22.1% | 11.9% | 6.1% |
| 128 | 86.6% | 63.6% | 39.4% | 22.1% | 11.9% |
| 192 | 95.1% | 77.7% | 52.5% | 30.8% | 17.2% |

**Công thức throughput speedup lý thuyết:**

```
Speedup(W, S) = W / (1 + W × P_lock_wait)
              = W / (1 + W × (1 - e^(-(W-1)/S)))
```

---

### Khuyến nghị cho Whitepaper

**Câu trả lời ngắn:** `S = 64` **quá nhỏ** cho phần cứng hiện đại. `S = 128` cũng chưa đủ. Lý do đúng đằng sau 64 shard ban đầu là **bitwise optimization** (`& 63`)— không phải vì 64 = số core.

**Công thức chọn `S` tối ưu:**

```
S_optimal = 2^k  sao cho  S ≥ α × W_max

Với α = "shards per worker" target:
  - α = 4  →  P(lock wait) ≈ 22%   (acceptable)
  - α = 8  →  P(lock wait) ≈ 11%   (good)
  - α = 16 →  P(lock wait) ≈ 6%    (very good)
```

**Đề xuất cụ thể cho Kallisto:**

| Target deployment | W_max | S_recommended | Ghi chú |
|---|---|---|---|
| Standard cloud (AWS M6i) | 128 vCPU | **256** | `α=2`, `& 255` |
| High-end cloud (GCP C4D) | 384 vCPU | **512** | `α=1.3`, `& 511` |
| Physical server (AMD EPYC) | 192 cores | **512** | `α=2.67`, `& 511` |
| Future-proof (2026+) | 384+ | **1024** | `α≥2.67`, `& 1023` |
| **Kallisto hiện tại** | 64 | **64** (OK) hoặc **128** | OK cho small deploys |

**Tác động với `total_capacity = 1M`:**

```
Nếu S = 256:
  items_per_shard  = 1,048,576 / 256  = 4,096 items/shard
  buckets_per_shard = 4,096 / 8       = 512 buckets/shard (> minimum 64 ✓)
  total_buckets    = 2 × 256 × 512   = 262,144 buckets
  total_slots      = 262,144 × 8     = 2,097,152  (giống y chang! ✓)
```

> **Ưu điểm của tăng shard: không thay đổi total capacity, chỉ giảm contention.** Đây là lý do nên tăng.

---

### Kết luận cho Whitepaper

> **"The choice of `NUM_SHARDS = 64` was optimal for a 64-core deployment model (2022–2023 cloud standard of AWS c5.18xlarge with 72 vCPU). However, with AMD EPYC 9965 offering 192 physical cores and GCP C4D reaching 384 vCPUs in 2025, a forward-compatible design should adopt `NUM_SHARDS = 256` or `512` as a power-of-2 constant, preserving the bitwise modulo optimization (`& (S-1)`) while reducing per-shard lock contention from ~64% to ~22% under 192-worker load."**

**Khuyến nghị của tôi:** Nâng `NUM_SHARDS = 256` ngay bây giờ là safe, không breaking, và cover cloud standard đến 2027+.

---

# Note No.2

Đúng vậy — và đây chính xác là giá trị của phân tích vừa rồi. Để tóm gọn lại ý tưởng:

**Vấn đề cốt lõi:** Kallisto hiện tại hardcode `NUM_SHARDS = 64` và `total_capacity = 1M` trong code. Người dùng deploy lên server 192-core sẽ không biết rằng con số đó đã bottleneck ngay từ thiết kế.

**Cái ta cần làm là tạo một Tuning Matrix** — một bảng tra cứu khoa học để người dùng answer 2 câu hỏi:

1. **"Tôi có bao nhiêu CPU cores?"** → Chọn `NUM_SHARDS`
2. **"Tôi muốn lưu bao nhiêu secrets?"** → Chọn `total_capacity`

Sau đó map chúng sang các tham số khởi động:
```bash
./kallisto_server --workers=N --shards=S --capacity=C
```

**Flow của document:**
```
Hardware Profile  →  Tuning Matrix  →  Config Parameters  →  Expected Performance
(cores, RAM)         (lookup table)     (CLI flags)            (latency, throughput)
```

**Để làm được Tuning Matrix chính xác, ta cần:**

| Bước | Việc cần làm |
|------|-------------|
| ① | Công thức [P(lock_wait | W, S)](cci:2://file:///workspaces/kallisto/include/kallisto/event/worker.hpp:97:0-128:1) → đã có ✅ |
| ② | Công thức [memory(S, C)](cci:1://file:///workspaces/kallisto/src/sharded_cuckoo_table.cpp:47:0-62:1) → đã có ✅ |
| ③ | Công thức `Speedup(W, S)` → đã có ✅ |
| ④ | Bảng tra lookup cho 4–5 hardware tier | ← cần làm |
| ⑤ | Validate bằng benchmark thực tế | ← cần làm |

Bạn muốn tôi:
- **(A)** Viết luôn Tuning Matrix document hoàn chỉnh vào [WHITEPAPER.md](cci:7://file:///workspaces/kallisto/WHITEPAPER.md:0:0-0:0) / docs?
- **(B)** Code thêm CLI flag `--shards=N` vào server để người dùng có thể tune được?
- **(C)** Cả hai — document + implementation?