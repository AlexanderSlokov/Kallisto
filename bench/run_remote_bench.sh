#!/usr/bin/env bash
#
# Kallisto Remote/Cluster Load Test Suite
# Targets a standalone server (Bare-metal or Docker)
#
# Usage: ./bench/run_remote_bench.sh [target_url] [threads] [connections] [duration]
# Default: ./bench/run_remote_bench.sh http://kallisto:8200 4 200 10s
#
set -euo pipefail

# ── Config ──────────────────────────────────────────────────────────────
TARGET_URL=${1:-"http://kallisto:8200"}
THREADS=${2:-4}
CONNECTIONS=${3:-200}
DURATION=${4:-10s}
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
    echo -e "${CYAN}║${BOLD}     KALLISTO REMOTE LOAD TEST (wrk)                        ${NC}${CYAN}║${NC}"
    echo -e "${CYAN}╠══════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${CYAN}║${NC}  Target:      ${YELLOW}${TARGET_URL}${NC}"
    echo -e "${CYAN}║${NC}  Threads:     ${YELLOW}${THREADS}${NC}"
    echo -e "${CYAN}║${NC}  Connections: ${YELLOW}${CONNECTIONS}${NC}"
    echo -e "${CYAN}║${NC}  Duration:    ${YELLOW}${DURATION}${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

# ── Step 1: Check prerequisites ─────────────────────────────────────────
check_prereqs() {
    if ! command -v wrk &>/dev/null; then
        echo -e "${RED}[ERROR] wrk not found. Install with: sudo apt-get install wrk${NC}"
        exit 1
    fi
}

# ── Step 2: Wait for target server ──────────────────────────────────────
wait_for_server() {
    echo -e "${CYAN}[1/4] Checking connection to ${TARGET_URL}...${NC}"
    
    for i in $(seq 1 30); do
        if curl -s --max-time 1 -H "Connection: close" "${TARGET_URL}/v1/secret/data/health" &>/dev/null; then
            echo -e "${GREEN}  ✓ Server is ready!${NC}"
            return 0
        fi
        sleep 0.5
    done
    echo -e "${RED}[ERROR] Cannot connect to ${TARGET_URL}. Hint: check Docker or IP.${NC}"
    exit 1
}

# ── Step 3: Seed data using wrk ────────────────────────────────────────
seed_data() {
    echo -e "${CYAN}[2/4] Seeding data with wrk in 3 seconds...${NC}"
    wrk -t2 -c10 -d3s -s "$SCRIPT_DIR/wrk_seed.lua" "${TARGET_URL}" 2>/dev/null
    
    # Quick verify
    VERIFY=$(curl -s --max-time 2 -H "Connection: close" "${TARGET_URL}/v1/secret/data/bench/s0" 2>/dev/null || echo "FAIL")
    if echo "$VERIFY" | grep -q "seed-value"; then
        echo -e "${GREEN}  ✓ Seed data successfully!${NC}"
    else
        echo -e "${YELLOW}  ⚠ Could not verify seed data, but proceeding anyway!${NC}"
    fi
}

# ── Step 4: Run benchmarks ──────────────────────────────────────────────
run_benchmarks() {
    echo ""
    echo -e "${CYAN}[3/4] Running GET benchmark (pure read, ${DURATION})...${NC}"
    echo "────────────────────────────────────────────────────────────────"
    wrk -t$THREADS -c$CONNECTIONS -d$DURATION \
        -s "$SCRIPT_DIR/wrk_get.lua" \
        "${TARGET_URL}" 2>&1
    sleep 2
    
    echo ""
    echo -e "${CYAN}[4/4] Running PUT benchmark (pure write, ${DURATION})...${NC}"
    echo "────────────────────────────────────────────────────────────────"
    wrk -t$THREADS -c$CONNECTIONS -d$DURATION \
        -s "$SCRIPT_DIR/wrk_put.lua" \
        "${TARGET_URL}" 2>&1
    sleep 2
    
    echo ""
    echo -e "${CYAN}[5/5] Running MIXED benchmark (95% Read / 5% Write, ${DURATION})...${NC}"
    echo "────────────────────────────────────────────────────────────────"
    wrk -t$THREADS -c$CONNECTIONS -d$DURATION \
        -s "$SCRIPT_DIR/wrk_mixed.lua" \
        "${TARGET_URL}" 2>&1
}

# ── Main ────────────────────────────────────────────────────────────────
banner
check_prereqs
wait_for_server
seed_data
run_benchmarks

echo ""
echo -e "${BOLD}${GREEN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}${GREEN}  LOAD TEST COMPLETED.${NC}"
echo -e "${BOLD}${GREEN}═══════════════════════════════════════════════════════════════${NC}"
