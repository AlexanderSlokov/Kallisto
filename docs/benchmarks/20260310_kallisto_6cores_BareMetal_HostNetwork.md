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
  tester:
    image: ghcr.io/alexanderslokov/kallisto-tester:latest
    network_mode: "host"
    container_name: kallisto-tester
    command: make bench-server
```

# Stats

## Image footprints

```plaintext
REPOSITORY                                    TAG          IMAGE ID       CREATED         SIZE
ghcr.io/alexanderslokov/kallisto-tester       latest       9eb4b8e11e6b   8 hours ago     792MB
```

# Tester logs

```bash
docker compose up
[+] Running 1/1
 ✔ Container kallisto-tester  Created                                                           0.2s 
Attaching to kallisto-tester
kallisto-tester  | 
kallisto-tester  | ╔══════════════════════════════════════════════════════════════╗
kallisto-tester  | ║     KALLISTO SERVER LOAD TEST (wrk)                          ║
kallisto-tester  | ╠══════════════════════════════════════════════════════════════╣
kallisto-tester  | ║  Total Cores:   6                                            ║
kallisto-tester  | ║  wrk Threads:   3                                            ║
kallisto-tester  | ║  Kal Workers:   3                                            ║
kallisto-tester  | ║  Connections:   200                                          ║
kallisto-tester  | ║  Duration:      10s                                          ║
kallisto-tester  | ╚══════════════════════════════════════════════════════════════╝
kallisto-tester  | 
kallisto-tester  | [1/5] Starting Kallisto server (3 workers)...
kallisto-tester  |   ✓ Server started (PID: 17)
kallisto-tester  | [2/5] Seeding data with wrk (3 seconds)...
kallisto-tester  | Running 3s test @ http://localhost:8200
kallisto-tester  |   2 threads and 10 connections
kallisto-tester  |   Thread Stats   Avg      Stdev     Max   +/- Stdev
kallisto-tester  |     Latency    39.76us   17.96us   1.07ms   86.98%
kallisto-tester  |     Req/Sec   104.41k    12.01k  113.74k    83.87%
kallisto-tester  |   643834 requests in 3.10s, 73.68MB read
kallisto-tester  | Requests/sec: 207720.62
kallisto-tester  | Transfer/sec:     23.77MB
kallisto-tester  | [SEED] 643834 requests in 3.10s (207721 req/s)
kallisto-tester  |   ✓ Data seeded and verified
kallisto-tester  | 
kallisto-tester  | [3/5] Running GET benchmark (pure read, 10s)...
kallisto-tester  | ────────────────────────────────────────────────────────────────
kallisto-tester  | Running 10s test @ http://localhost:8200
kallisto-tester  |   3 threads and 200 connections
kallisto-tester  |   Thread Stats   Avg      Stdev     Max   +/- Stdev
kallisto-tester  |     Latency   301.79us  136.22us   1.33ms   79.86%
kallisto-tester  |     Req/Sec   126.09k    10.85k  135.12k    89.33%
kallisto-tester  |   3764548 requests in 10.00s, 638.65MB read
kallisto-tester  | Requests/sec: 376406.39
kallisto-tester  | Transfer/sec:     63.86MB
kallisto-tester  | 
kallisto-tester  | === Kallisto GET Benchmark ===
kallisto-tester  |   Requests/sec: 376406.39
kallisto-tester  |   Avg Latency:  0.30 ms
kallisto-tester  |   p99 Latency:  0.92 ms
kallisto-tester  |   Total Reqs:   3764548
kallisto-tester  |   Errors:       0
kallisto-tester  | 
kallisto-tester  | [4/5] Running PUT benchmark (pure write, 10s)...
kallisto-tester  | ────────────────────────────────────────────────────────────────
kallisto-tester  | Running 10s test @ http://localhost:8200
kallisto-tester  |   3 threads and 200 connections
kallisto-tester  |   Thread Stats   Avg      Stdev     Max   +/- Stdev
kallisto-tester  |     Latency     5.48ms   18.35ms 203.03ms   94.18%
kallisto-tester  |     Req/Sec    82.57k    47.97k  120.81k    74.00%
kallisto-tester  |   2464616 requests in 10.00s, 282.05MB read
kallisto-tester  | Requests/sec: 246384.16
kallisto-tester  | Transfer/sec:     28.20MB
kallisto-tester  | 
kallisto-tester  | === Kallisto PUT Benchmark ===
kallisto-tester  |   Requests/sec: 246384.16
kallisto-tester  |   Avg Latency:  5.48 ms
kallisto-tester  |   p99 Latency:  105.78 ms
kallisto-tester  |   Total Reqs:   2464616
kallisto-tester  |   Errors:       0
kallisto-tester  | 
kallisto-tester  | [5/5] Running MIXED benchmark (95/5, 10s)...
kallisto-tester  | ────────────────────────────────────────────────────────────────
kallisto-tester  | Running 10s test @ http://localhost:8200
kallisto-tester  |   3 threads and 200 connections
kallisto-tester  |   Thread Stats   Avg      Stdev     Max   +/- Stdev
kallisto-tester  |     Latency   328.68us  163.12us   2.08ms   84.70%
kallisto-tester  |     Req/Sec   121.03k    10.39k  130.54k    87.67%
kallisto-tester  |   3612190 requests in 10.00s, 602.84MB read
kallisto-tester  | Requests/sec: 361160.89
kallisto-tester  | Transfer/sec:     60.27MB
kallisto-tester  | 
kallisto-tester  | === Kallisto MIXED 95/5 Benchmark ===
kallisto-tester  |   Requests/sec: 361160.89
kallisto-tester  |   Avg Latency:  0.33 ms
kallisto-tester  |   p99 Latency:  0.93 ms
kallisto-tester  |   Total Reqs:   3612190
kallisto-tester  |   Errors:       0
kallisto-tester  | 
kallisto-tester  | ═══════════════════════════════════════════════════════════════
kallisto-tester  |   ALL BENCHMARKS COMPLETE
kallisto-tester  | ═══════════════════════════════════════════════════════════════
kallisto-tester  | 
kallisto-tester  | Shutting down server...
kallisto-tester  | Done.
kallisto-tester exited with code 0
```
