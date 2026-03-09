# About CPU

```bash
sudo lshw -C CPU
  *-cpu                     
       description: CPU
       product: Intel(R) Core(TM) i5-10400 CPU @ 2.90GHz
       vendor: Intel Corp.
       physical id: 4b
       bus info: cpu@0
       version: 6.165.3
       serial: To Be Filled By O.E.M.
       slot: U3E1
       size: 4171MHz
       capacity: 4300MHz
       width: 64 bits
       clock: 100MHz
       capabilities: lm fpu fpu_exception wp vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp x86-64 constant_tsc art arch_perfmon pebs bts rep_good nopl xtopology nonstop_tsc cpuid aperfmperf pni pclmulqdq dtes64 monitor ds_cpl vmx est tm2 ssse3 sdbg fma cx16 xtpr pdcm pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand lahf_lm abm 3dnowprefetch cpuid_fault ssbd ibrs ibpb stibp ibrs_enhanced tpr_shadow flexpriority ept vpid ept_ad fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid mpx rdseed adx smap clflushopt intel_pt xsaveopt xsavec xgetbv1 xsaves dtherm ida arat pln pts hwp hwp_notify hwp_act_window hwp_epp vnmi md_clear flush_l1d arch_capabilities cpufreq
       configuration: cores=6 enabledcores=6 microcode=256 threads=12
```

# Docker Compose setup

```yaml
services:
  # The Kallisto Production Server
  kallisto:
    image: ghcr.io/alexanderslokov/kallisto:latest
    container_name: kallisto-server
    ports:
      - "8200:8200" # HTTP API (Vault compatible)
      - "8201:8201" # gRPC API
    # volumes:
    #   - kallisto-data:/data/kallisto/rocksdb
    environment:
      - WORKERS=6
      - DB_PATH=/data/kallisto/rocksdb
      - PORT_HTTP=8200
      - PORT_GRPC=8201
    restart: unless-stopped
    networks:
      - kallisto-net

  # The Benchmark / Tester container
  tester:
    build:
      context: .
      target: tester
    image: ghcr.io/alexanderslokov/kallisto-tester:latest
    container_name: kallisto-tester
    # We override the default entrypoint to run the benchmark script
    # It waits for the server, seeds data, and runs wrk tests against the 'kallisto' service
    command: bash ./bench/run_remote_bench.sh http://kallisto:8200 4 200 10s
    networks:
      - kallisto-net

volumes:
  kallisto-data:

networks:
  kallisto-net:
    driver: bridge
```

# Stats

## Image footprints

```plaintext
sha256:0b05d16181810bcb26695be25ed6d1...Unused	
ghcr.io/alexanderslokov/kallisto-tester:latest
791.5 MB	2026-03-09 13:45:29

sha256:2127e4f9f04f05b4caac63225215d1...Unused	
ghcr.io/alexanderslokov/kallisto:latest
159.9 MB	2026-03-09 13:47:06
```

## Idle

```bash
CONTAINER ID   NAME               CPU %     MEM USAGE / LIMIT     MEM %     NET I/O           BLOCK I/O         PIDS 
bab3f3c69987   kallisto-server    0.13%     22.73MiB / 31.27GiB   0.07%     2.99kB / 126B     0B / 28.7kB       54
```

## After benchmark

```bash
CONTAINER ID   NAME              CPU %     MEM USAGE / LIMIT     MEM %     NET I/O           BLOCK I/O    PIDS 
bab3f3c69987   kallisto-server   0.11%     302.8MiB / 31.27GiB   0.95%     1.15GB / 1.58GB   0B / 144MB   54 
```

# Liveness and readyness probes

```bash
curl -X POST http://localhost:8200/v1/secret/data/myapp/db-password   -H "Content-Type: application/json"   -d '{"data":{"value":"super-secret-123"}}' 
{"data":{"created":true}}

```bash
curl http://localhost:8200/v1/secret/data/myapp/db-password
{"data":{"data":{"value":"super-secret-123"}},"metadata":{"created_time":1773039321}}
```

```bash
curl -X DELETE http://localhost:8200/v1/secret/data/myapp/db-password
curl http://localhost:8200/v1/secret/data/myapp/db-password
{"errors":["Secret not found"]}
```

```bash
curl http://localhost:8200/v1/secret/data/does-not-exist0/v1/secret/data/does-not-exist
{"errors":["Secret not found (B-Tree reject)"]}
```

# Tester logs

```bash
kallisto-tester  | 
kallisto-tester  | ╔══════════════════════════════════════════════════════════════╗
kallisto-tester  | ║     KALLISTO REMOTE LOAD TEST (wrk)                          ║
kallisto-tester  | ╠══════════════════════════════════════════════════════════════╣
kallisto-tester  | ║  Target:      http://kallisto:8200                           ║
kallisto-tester  | ║  Threads:     4                                              ║
kallisto-tester  | ║  Connections: 200                                            ║
kallisto-tester  | ║  Duration:    10s                                            ║
kallisto-tester  | ╚══════════════════════════════════════════════════════════════╝
kallisto-tester  | 
kallisto-tester  | [1/4] Đang kiểm tra kết nối tới http://kallisto:8200...
kallisto-tester  |   ✓ Server đã sẵn sàng hứng đạn!
kallisto-tester  | [2/4] Bơm dữ liệu (Seeding) bằng wrk (3 seconds)...
kallisto-tester  | Running 3s test @ http://kallisto:8200
kallisto-tester  |   2 threads and 10 connections
kallisto-tester  |   Thread Stats   Avg      Stdev     Max   +/- Stdev
kallisto-tester  |     Latency    70.87us   24.23us 618.00us   74.03%
kallisto-tester  |     Req/Sec    63.19k     6.65k   76.22k    70.97%
kallisto-tester  |   389579 requests in 3.10s, 44.58MB read
kallisto-tester  | Requests/sec: 125685.28
kallisto-tester  | Transfer/sec:     14.38MB
kallisto-tester  | [SEED] 389579 requests in 3.10s (125685 req/s)
kallisto-tester  |   ✓ Đã bơm data thành công!
kallisto-tester  | 
kallisto-tester  | [3/4] Chạy bài test GET (Thuần đọc, 10s)...
kallisto-tester  | ────────────────────────────────────────────────────────────────
kallisto-tester  | Running 10s test @ http://kallisto:8200
kallisto-tester  |   4 threads and 200 connections
kallisto-tester  |   Thread Stats   Avg      Stdev     Max   +/- Stdev
kallisto-tester  |     Latency   422.81us  170.69us   4.66ms   75.57%
kallisto-tester  |     Req/Sec    64.68k    11.84k   95.81k    79.50%
kallisto-tester  |   2573663 requests in 10.00s, 436.62MB read
kallisto-tester  | Requests/sec: 257323.97
kallisto-tester  | Transfer/sec:     43.65MB
kallisto-tester  | 
kallisto-tester  | === Kallisto GET Benchmark ===
kallisto-tester  |   Requests/sec: 257323.97
kallisto-tester  |   Avg Latency:  0.42 ms
kallisto-tester  |   p99 Latency:  0.95 ms
kallisto-tester  |   Total Reqs:   2573663
kallisto-tester  |   Errors:       0
kallisto-tester  | 
kallisto-tester  | [4/4] Chạy bài test PUT (Thuần ghi, 10s)...
kallisto-tester  | ────────────────────────────────────────────────────────────────
kallisto-tester  | Running 10s test @ http://kallisto:8200
kallisto-tester  |   4 threads and 200 connections
kallisto-tester  |   Thread Stats   Avg      Stdev     Max   +/- Stdev
kallisto-tester  |     Latency     3.16ms    6.51ms  90.07ms   91.49%
kallisto-tester  |     Req/Sec    34.86k    18.32k   50.93k    74.00%
kallisto-tester  |   1387220 requests in 10.00s, 158.75MB read
kallisto-tester  | Requests/sec: 138656.55
kallisto-tester  | Transfer/sec:     15.87MB
kallisto-tester  | 
kallisto-tester  | === Kallisto PUT Benchmark ===
kallisto-tester  |   Requests/sec: 138656.55
kallisto-tester  |   Avg Latency:  3.16 ms
kallisto-tester  |   p99 Latency:  34.64 ms
kallisto-tester  |   Total Reqs:   1387220
kallisto-tester  |   Errors:       0
kallisto-tester  | 
kallisto-tester  | [5/5] Chạy bài test MIXED (95% Đọc / 5% Ghi, 10s)...
kallisto-tester  | ────────────────────────────────────────────────────────────────
kallisto-tester  | Running 10s test @ http://kallisto:8200
kallisto-tester  |   4 threads and 200 connections
kallisto-tester  |   Thread Stats   Avg      Stdev     Max   +/- Stdev
kallisto-tester  |     Latency   428.45us  181.54us   2.20ms   76.61%
kallisto-tester  |     Req/Sec    64.78k    12.86k   91.81k    66.25%
kallisto-tester  |   2578323 requests in 10.00s, 430.29MB read
kallisto-tester  | Requests/sec: 257777.47
kallisto-tester  | Transfer/sec:     43.02MB
kallisto-tester  | 
kallisto-tester  | === Kallisto MIXED 95/5 Benchmark ===
kallisto-tester  |   Requests/sec: 257777.47
kallisto-tester  |   Avg Latency:  0.43 ms
kallisto-tester  |   p99 Latency:  0.98 ms
kallisto-tester  |   Total Reqs:   2578323
kallisto-tester  |   Errors:       0
kallisto-tester  | 
kallisto-tester  | ═══════════════════════════════════════════════════════════════
kallisto-tester  |   ĐÃ CÀY NÁT SERVER XONG! KIỂM TRA LOG THÔI!
kallisto-tester  | ═══════════════════════════════════════════════════════════════
kallisto-tester exited with code 0
```