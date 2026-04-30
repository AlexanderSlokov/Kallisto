-- wrk_get.lua: Pure READ benchmark for Kallisto Server
-- Pre-requisite: Server must be seeded with secrets at /v1/secret/data/bench/s0..s999
--
-- Usage: wrk -t4 -c100 -d10s -s bench/wrk_get.lua http://localhost:8200

counter = -1

request = function()
    counter = counter + 1
    local id = counter % 1000
    local path = "/v1/secret/data/bench/s" .. id
    return wrk.format("GET", path)
end

-- Report summary
done = function(summary, latency, requests)
    io.write("\n=== Kallisto GET Benchmark ===\n")
    io.write(string.format("  Requests/sec: %.2f\n", summary.requests / (summary.duration / 1000000)))
    io.write(string.format("  Avg Latency:  %.2f ms\n", latency.mean / 1000))
    io.write(string.format("  p99 Latency:  %.2f ms\n", latency:percentile(99) / 1000))
    io.write(string.format("  Total Reqs:   %d\n", summary.requests))
    io.write(string.format("  Errors:       %d\n", summary.errors.status + summary.errors.connect + summary.errors.read + summary.errors.write + summary.errors.timeout))
end
