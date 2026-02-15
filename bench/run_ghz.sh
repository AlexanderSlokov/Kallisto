#!/bin/bash
# run_ghz.sh - Benchmark Kallisto gRPC using ghz

# Assume ghz is in PATH (installed in devcontainer)
GHZ_PATH=ghz
PROTO_PATH=./proto/kallisto.proto
HOST=127.0.0.1:8201

# Check if ghz is installed
if ! command -v $GHZ_PATH &> /dev/null; then
    echo "Error: ghz not found in PATH."
    echo "Please install ghz (e.g., copy to /usr/local/bin)"
    exit 1
fi

echo "======================================================="
echo "   Kallisto gRPC Benchmark (ghz)"
echo "======================================================="
echo "Mode: Direct Execution (Blocking IO)"
echo "Target: $HOST"
echo ""

# 1. PUT Benchmark (Warmup & Fill)
echo "[PUT] Filling data (100k reqs, 50 connections, 200 concurrency)..."
$GHZ_PATH --insecure \
  --proto $PROTO_PATH \
  --call kallisto.SecretService/Put \
  -d '{"path":"bench/key{{.RequestNumber}}", "value":"dmFsdWU="}' \
  --connections=50 \
  -c 200 \
  -n 100000 \
  --cpus=4 \
  $HOST

echo ""
echo "-------------------------------------------------------"
echo ""

# 2. GET Benchmark (Read Performance)
echo "[GET] Reading data (100k reqs, 50 connections, 200 concurrency)..."
$GHZ_PATH --insecure \
  --proto $PROTO_PATH \
  --call kallisto.SecretService/Get \
  -d '{"path":"bench/key{{.RequestNumber}}"}' \
  --connections=50 \
  -c 200 \
  -n 100000 \
  --cpus=4 \
  $HOST
