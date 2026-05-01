-- wrk_put.lua: Pure WRITE benchmark for Kallisto Server
-- Tests POST /v1/secret/data/<path> with JSON body
--
-- Usage: wrk -t4 -c100 -d10s -s benchmarks/server/workloads/wrk_put.lua http://localhost:8200

counter = -1

wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

request = function()
    counter = counter + 1
    local id = counter % 10000
    local path = "/v1/secret/data/bench/w" .. id
    local body = '{"data":{"value":"bench-val-' .. id .. '"}}'
    return wrk.format("POST", path, nil, body)
end

done = function(summary, latency, requests)
    io.write("\n=== Kallisto PUT Benchmark ===\n")
    io.write(string.format("  Requests/sec: %.2f\n", summary.requests / (summary.duration / 1000000)))
    io.write(string.format("  Avg Latency:  %.2f ms\n", latency.mean / 1000))
    io.write(string.format("  p99 Latency:  %.2f ms\n", latency:percentile(99) / 1000))
    io.write(string.format("  Total Reqs:   %d\n", summary.requests))
    io.write(string.format("  Errors:       %d\n", summary.errors.status + summary.errors.connect + summary.errors.read + summary.errors.write + summary.errors.timeout))
end
