-- wrk_mixed.lua: MIXED workload benchmark (95% READ, 5% WRITE)
-- Simulates production traffic pattern for Vault-like secret management
--
-- Usage: wrk -t4 -c100 -d10s -s bench/wrk_mixed.lua http://localhost:8200

counter = 0

wrk.headers["Content-Type"] = "application/json"

request = function()
    counter = counter + 1
    local id = counter % 1000

    if counter % 20 == 0 then
        -- 5% writes (every 20th request)
        local path = "/v1/secret/data/bench/s" .. id
        local body = '{"data":{"value":"updated-' .. counter .. '"}}'
        return wrk.format("POST", path, nil, body)
    else
        -- 95% reads
        local path = "/v1/secret/data/bench/s" .. id
        return wrk.format("GET", path)
    end
end

done = function(summary, latency, requests)
    io.write("\n=== Kallisto MIXED 95/5 Benchmark ===\n")
    io.write(string.format("  Requests/sec: %.2f\n", summary.requests / (summary.duration / 1000000)))
    io.write(string.format("  Avg Latency:  %.2f ms\n", latency.mean / 1000))
    io.write(string.format("  p99 Latency:  %.2f ms\n", latency:percentile(99) / 1000))
    io.write(string.format("  Total Reqs:   %d\n", summary.requests))
    io.write(string.format("  Errors:       %d\n", summary.errors.status + summary.errors.connect + summary.errors.read + summary.errors.write + summary.errors.timeout))
end
