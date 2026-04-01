#!/bin/bash
set -e

# Thiết lập các biến mặc định
WORKERS=${WORKERS:-$(nproc)}
DB_PATH=${DB_PATH:-/data/kallisto/rocksdb}
PORT_HTTP=${PORT_HTTP:-8200}

echo "====================================================================="
echo "  Starting Kallisto Server"
echo "  - DB Path: $DB_PATH"
echo "  - Workers: $WORKERS"
echo "====================================================================="

# Khởi chạy server nếu lệnh đầu tiên bắt đầu bằng cờ (-...) hoặc rỗng
if [ -z "$1" ] || [[ "$1" == -* ]]; then
    exec /app/kallisto_server --workers="$WORKERS" --db-path="$DB_PATH" "$@"
fi

# Chạy bất kỳ lệnh nào khác do user truyền vào
exec "$@"
