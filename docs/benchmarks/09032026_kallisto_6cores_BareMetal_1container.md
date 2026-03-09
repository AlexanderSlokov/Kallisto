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
  # The Benchmark / Tester container
  tester:
    build:
      context: .
      target: tester
    image: ghcr.io/alexanderslokov/kallisto-tester:latest
    container_name: kallisto-tester
    # We override the default entrypoint to run the benchmark script
    # It waits for the server, seeds data, and runs wrk tests against the 'kallisto' service
    command: bash -c "make bench-server"
    networks:
      - kallisto-net

networks:
  kallisto-net:
    driver: bridge
```

# Stats

## Image footprints

```plaintext
sha256:25bbc174fbfe24eb7df2b6e959285d...Unused	
ghcr.io/alexanderslokov/kallisto-tester:latest
791.5 MB	2026-03-08 17:48:54	hethong-B460M-GAMING-HD

sha256:8f7b85b8947403ea6adc364ad3fe5f...Unused	
ghcr.io/alexanderslokov/kallisto:latest
159.9 MB	2026-03-08 17:50:30	hethong-B460M-GAMING-HD
```

# Tester logs

```bash
╔══════════════════════════════════════════════════════════════╗
║     KALLISTO SERVER LOAD TEST (wrk)                          ║
╠══════════════════════════════════════════════════════════════╣
║  Threads:     4                                              ║
║  Connections: 200                                            ║
║  Duration:    10s                                            ║
║  Workers:     4                                              ║
╚══════════════════════════════════════════════════════════════╝
[1/5] Starting Kallisto server (4 workers)...
  ✓ Server started (PID: 12)
[2/5] Seeding data with wrk (3 seconds)...
Running 3s test @ http://localhost:8200
  2 threads and 10 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    38.47us   33.53us   3.07ms   97.81%
    Req/Sec   105.69k    15.30k  119.03k    83.87%
  650841 requests in 3.10s, 74.48MB read
Requests/sec: 209953.86
Transfer/sec:     24.03MB
[SEED] 650841 requests in 3.10s (209954 req/s)
  ✓ Data seeded and verified
[3/5] Running GET benchmark (pure read, 10s)...
────────────────────────────────────────────────────────────────
Running 10s test @ http://localhost:8200
  4 threads and 200 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   421.47us  179.91us   3.97ms   63.40%
    Req/Sec    99.76k    22.13k  143.91k    57.86%
  3979900 requests in 10.10s, 675.19MB read
Requests/sec: 394045.49
Transfer/sec:     66.85MB
=== Kallisto GET Benchmark ===
  Requests/sec: 394045.49
  Avg Latency:  0.42 ms
  p99 Latency:  0.80 ms
  Total Reqs:   3979900
  Errors:       0
[4/5] Running PUT benchmark (pure write, 10s)...
────────────────────────────────────────────────────────────────
Running 10s test @ http://localhost:8200
  4 threads and 200 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     3.56ms   11.02ms 151.57ms   93.54%
    Req/Sec    67.99k    43.69k  128.64k    50.50%
  2705961 requests in 10.00s, 309.67MB read
Requests/sec: 270534.74
Transfer/sec:     30.96MB
=== Kallisto PUT Benchmark ===
  Requests/sec: 270534.74
  Avg Latency:  3.56 ms
  p99 Latency:  60.18 ms
  Total Reqs:   2705961
  Errors:       0
[5/5] Running MIXED benchmark (95/5, 10s)...
────────────────────────────────────────────────────────────────
Running 10s test @ http://localhost:8200
  4 threads and 200 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   366.88us  181.72us   4.89ms   58.76%
    Req/Sec    96.33k    25.00k  140.07k    61.94%
  3851447 requests in 10.10s, 642.76MB read
Requests/sec: 381330.71
Transfer/sec:     63.64MB
=== Kallisto MIXED 95/5 Benchmark ===
  Requests/sec: 381330.71
  Avg Latency:  0.37 ms
  p99 Latency:  0.77 ms
  Total Reqs:   3851447
  Errors:       0
═══════════════════════════════════════════════════════════════
  ALL BENCHMARKS COMPLETE
═══════════════════════════════════════════════════════════════
Shutting down server...
Done.
```

# Liveness and readiness probes

```bash
curl -X POST http://localhost:8200/v1/secret/data/myapp/db-password   -H "Content-Type: application/json"   -d '{"data":{"value":"super-secret-123"}}' 
{"data":{"created":true}}

```bash
curl http://localhost:8200/v1/secret/data/myapp/db-password
{"data":{"data":{"value":"super-secret-123"}},"metadata":{"created_time":1773025845}}
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