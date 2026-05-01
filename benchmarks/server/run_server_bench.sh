#!/usr/bin/env bash
#
# Kallisto Server Load Test Suite
# Uses wrk to stress test the HTTP API
#
# Usage: ./bench/run_server_bench.sh [threads] [connections] [duration]
# Default: 4 threads, 200 connections, 10s duration
#
set -euo pipefail

# ── Config ──────────────────────────────────────────────────────────────
if command -v lscpu &> /dev/null; then
    TOTAL_CORES=$(lscpu -b -p=Core,Socket | grep -v '^#' | sort -u | wc -l)
else
    TOTAL_CORES=$(nproc)
fi

HALF_CORES=$(( TOTAL_CORES / 2 ))
if [ "$HALF_CORES" -lt 1 ]; then
    HALF_CORES=1
fi

THREADS=${1:-$HALF_CORES}
WORKERS=${2:-$HALF_CORES}
CONNECTIONS=${3:-200}
DURATION=${4:-10s}
HTTP_PORT=8200
# Use /tmp for socket and db to avoid permission issues in Docker/non-root env
export KALLISTO_SOCKET=${KALLISTO_SOCKET:-/tmp/kallisto.sock}
BENCH_DB_PATH="/tmp/kallisto_bench_data"
BENCH_LOG="/tmp/kallisto_bench.log"
SERVER_BIN="./build/kallisto_server"
CLI_BIN="./build/kallisto"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Helper to run commands with sudo only if necessary and available
run_cmd() {
    if [ "$(id -u)" -ne 0 ] && command -v sudo &>/dev/null; then
        sudo "$@"
    else
        "$@"
    fi
}

# ── Colors ──────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

banner() {
    echo ""
    echo -e "${CYAN}     KALLISTO SERVER LOAD TEST (wrk) ${NC}"
    printf "${CYAN}  %-14s ${YELLOW}%-40s${NC}\n" "Total Cores:" "$TOTAL_CORES"
    printf "${CYAN}  %-14s ${YELLOW}%-40s${NC}\n" "wrk Threads:" "$THREADS"
    printf "${CYAN}  %-14s ${YELLOW}%-40s${NC}\n" "Kal Workers:" "$WORKERS"
    printf "${CYAN}  %-14s ${YELLOW}%-40s${NC}\n" "Connections:" "$CONNECTIONS"
    printf "${CYAN}  %-14s ${YELLOW}%-40s${NC}\n" "Duration:" "$DURATION"
    printf "${CYAN}  %-14s ${YELLOW}%-40s${NC}\n" "Socket Path:" "$KALLISTO_SOCKET"
    echo ""
}

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

# ── Step 2: Start server ────────────────────────────────────────────────────────────────────────
start_server() {
    echo -e "${CYAN}[1/5] Starting Kallisto server (${WORKERS} workers)...${NC}"

    # Clean up old processes, socket files, and stale bench data
    pkill -f kallisto_server 2>/dev/null || true
    rm -f "$KALLISTO_SOCKET" 2>/dev/null
    rm -rf "$BENCH_DB_PATH" 2>/dev/null
    sleep 0.5

    # Run server with a temp db path (avoids /kallisto/data permission issues)
    $SERVER_BIN --http-port=$HTTP_PORT --workers=$WORKERS \
        --socket-path="$KALLISTO_SOCKET" \
        --db-path="$BENCH_DB_PATH" &>"$BENCH_LOG" &
    SERVER_PID=$!

    for i in $(seq 1 30); do
        if curl -s --max-time 1 -H "Connection: close" "http://localhost:$HTTP_PORT/v1/secret/data/health" &>/dev/null; then
            break
        fi
        sleep 0.25
    done
    sleep 0.5
    echo -e "${GREEN}  ✓ Server started (PID: $SERVER_PID)${NC}"

    echo -e "${CYAN}  Switching to BATCH mode...${NC}"
    $CLI_BIN "MODE BATCH"
    sleep 0.5
}

seed_data() {
    echo -e "${CYAN}[2/5] Seeding data with wrk in 3 seconds...${NC}"
    wrk -t2 -c10 -d3s -s "$SCRIPT_DIR/wrk_seed.lua" "http://localhost:$HTTP_PORT" 2>/dev/null

    VERIFY=$(curl -s --max-time 2 -H "Connection: close" "http://localhost:$HTTP_PORT/v1/secret/data/bench/s0" 2>/dev/null || echo "FAIL")
    if echo "$VERIFY" | grep -q "seed-value"; then
        echo -e "${GREEN}  ✓ Data seeded and verified${NC}"
    else
        echo -e "${YELLOW}  ⚠ Seed verification unclear (benching anyway)${NC}"
    fi
}

run_benchmarks() {
    echo ""
    echo -e "${CYAN}[3/5] Running GET benchmark (pure read, ${DURATION})...${NC}"
    echo "────────────────────────────────────────────────────────────────"
    wrk -t$THREADS -c$CONNECTIONS -d$DURATION -s "$SCRIPT_DIR/wrk_get.lua" "http://localhost:$HTTP_PORT" 2>&1
    sleep 1

    echo ""
    echo -e "${CYAN}[4/5] Running PUT benchmark (pure write, ${DURATION})...${NC}"
    echo "────────────────────────────────────────────────────────────────"
    wrk -t$THREADS -c$CONNECTIONS -d$DURATION -s "$SCRIPT_DIR/wrk_put.lua" "http://localhost:$HTTP_PORT" 2>&1
    sleep 1

    echo ""
    echo -e "${CYAN}[5/5] Running MIXED benchmark (95/5, ${DURATION})...${NC}"
    echo "────────────────────────────────────────────────────────────────"
    wrk -t$THREADS -c$CONNECTIONS -d$DURATION -s "$SCRIPT_DIR/wrk_mixed.lua" "http://localhost:$HTTP_PORT" 2>&1
}

cleanup() {
    echo ""
    echo -e "${CYAN}Shutting down server...${NC}"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    rm -rf "$BENCH_DB_PATH" 2>/dev/null
    rm -f "$BENCH_LOG" 2>/dev/null
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
echo -e "${BOLD}${GREEN}════════════"
