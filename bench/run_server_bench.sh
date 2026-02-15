#!/usr/bin/env bash
#
# Kallisto Server Load Test Suite
# Uses wrk to stress test the HTTP Vault API (SO_REUSEPORT architecture)
#
# Usage: ./bench/run_server_bench.sh [threads] [connections] [duration]
#   Default: 4 threads, 200 connections, 10s duration
#
set -euo pipefail

# ── Config ──────────────────────────────────────────────────────────────
THREADS=${1:-4}
CONNECTIONS=${2:-200}
DURATION=${3:-10s}
HTTP_PORT=8200
GRPC_PORT=8201
WORKERS=4
SERVER_BIN="./build/kallisto_server"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── Colors ──────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

banner() {
    echo ""
    echo -e "${CYAN}╔══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║${BOLD}     KALLISTO SERVER LOAD TEST (wrk)                        ${NC}${CYAN}║${NC}"
    echo -e "${CYAN}╠══════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${CYAN}║${NC}  Threads:     ${YELLOW}${THREADS}${NC}                                              ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}  Connections: ${YELLOW}${CONNECTIONS}${NC}                                            ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}  Duration:    ${YELLOW}${DURATION}${NC}                                            ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}  Workers:     ${YELLOW}${WORKERS}${NC}                                              ${CYAN}║${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

# ── Step 1: Check prerequisites ─────────────────────────────────────────
check_prereqs() {
    if ! command -v wrk &>/dev/null; then
        echo -e "${RED}[ERROR] wrk not found. Install with: sudo apt-get install wrk${NC}"
        exit 1
    fi
    if [ ! -f "$SERVER_BIN" ]; then
        echo -e "${RED}[ERROR] Server binary not found. Run 'make build-server' first.${NC}"
        exit 1
    fi
}

# ── Step 2: Start server ────────────────────────────────────────────────
start_server() {
    echo -e "${CYAN}[1/5] Starting Kallisto server (${WORKERS} workers)...${NC}"
    
    # Kill any existing server
    pkill -f kallisto_server 2>/dev/null || true
    sleep 0.5
    
    $SERVER_BIN --http-port=$HTTP_PORT --grpc-port=$GRPC_PORT --workers=$WORKERS &>/dev/null &
    SERVER_PID=$!
    
    # Wait for server to be ready (poll health endpoint)
    for i in $(seq 1 30); do
        if curl -s --max-time 1 -H "Connection: close" "http://localhost:$HTTP_PORT/v1/secret/data/health" &>/dev/null; then
            break
        fi
        sleep 0.25
    done
    sleep 0.5
    echo -e "${GREEN}  ✓ Server started (PID: $SERVER_PID)${NC}"
}

# ── Step 3: Seed data using wrk ────────────────────────────────────────
seed_data() {
    echo -e "${CYAN}[2/5] Seeding data with wrk (3 seconds)...${NC}"
    wrk -t2 -c10 -d3s -s "$SCRIPT_DIR/wrk_seed.lua" "http://localhost:$HTTP_PORT" 2>/dev/null
    
    # Quick verify
    VERIFY=$(curl -s --max-time 2 -H "Connection: close" "http://localhost:$HTTP_PORT/v1/secret/data/bench/s0" 2>/dev/null || echo "FAIL")
    if echo "$VERIFY" | grep -q "seed-value"; then
        echo -e "${GREEN}  ✓ Data seeded and verified${NC}"
    else
        echo -e "${YELLOW}  ⚠ Seed verification unclear (benching anyway)${NC}"
    fi
}

# ── Step 4: Run benchmarks ──────────────────────────────────────────────
run_benchmarks() {
    echo ""
    echo -e "${CYAN}[3/5] Running GET benchmark (pure read, ${DURATION})...${NC}"
    echo "────────────────────────────────────────────────────────────────"
    wrk -t$THREADS -c$CONNECTIONS -d$DURATION \
        -s "$SCRIPT_DIR/wrk_get.lua" \
        "http://localhost:$HTTP_PORT" 2>&1
    sleep 1
    
    echo ""
    echo -e "${CYAN}[4/5] Running PUT benchmark (pure write, ${DURATION})...${NC}"
    echo "────────────────────────────────────────────────────────────────"
    wrk -t$THREADS -c$CONNECTIONS -d$DURATION \
        -s "$SCRIPT_DIR/wrk_put.lua" \
        "http://localhost:$HTTP_PORT" 2>&1
    sleep 1
    
    echo ""
    echo -e "${CYAN}[5/5] Running MIXED benchmark (95/5, ${DURATION})...${NC}"
    echo "────────────────────────────────────────────────────────────────"
    wrk -t$THREADS -c$CONNECTIONS -d$DURATION \
        -s "$SCRIPT_DIR/wrk_mixed.lua" \
        "http://localhost:$HTTP_PORT" 2>&1
}

# ── Cleanup ─────────────────────────────────────────────────────────────
cleanup() {
    echo ""
    echo -e "${CYAN}Shutting down server...${NC}"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    echo -e "${GREEN}Done.${NC}"
}

trap cleanup EXIT

# ── Main ────────────────────────────────────────────────────────────────
banner
check_prereqs
start_server
seed_data
run_benchmarks

echo ""
echo -e "${BOLD}${GREEN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}${GREEN}  ALL BENCHMARKS COMPLETE${NC}"
echo -e "${BOLD}${GREEN}═══════════════════════════════════════════════════════════════${NC}"
