#!/usr/bin/dumb-init /bin/sh
set -e

# Run dumb-init as PID 1 in order to reap zombie processes
# as well as forward signals to all processes in its session.

# Prevent core dumps (secrets must never leak to disk via coredump)
ulimit -c 0

# Default configuration
WORKERS=${WORKERS:-$(nproc)}
DB_PATH=${DB_PATH:-/kallisto/data}
PORT_HTTP=${PORT_HTTP:-8200}

# KALLISTO_CONFIG_DIR isn't exposed as a volume but you can compose additional
# config files in there if you use this image as a base, or use
# KALLISTO_LOCAL_CONFIG below.
KALLISTO_CONFIG_DIR=/kallisto/config

# You can also set the KALLISTO_LOCAL_CONFIG environment variable to pass some
# Kallisto configuration JSON without having to bind any volumes.
if [ -n "$KALLISTO_LOCAL_CONFIG" ]; then
    echo "$KALLISTO_LOCAL_CONFIG" > "$KALLISTO_CONFIG_DIR/local.json"
fi

# If the user is trying to run Kallisto directly with some arguments, then
# pass them to the server.
if [ -z "$1" ] || [ "${1#-}" != "$1" ]; then

    # If we are running as root, ensure proper ownership of bind-mounted
    # volumes and then drop privileges via gosu.
    if [ "$(id -u)" = '0' ]; then
        if [ -z "$SKIP_CHOWN" ]; then
            # If the config dir is bind mounted then chown it
            if [ "$(stat -c %u /kallisto/config)" != "$(id -u kallisto)" ]; then
                chown -R kallisto:kallisto /kallisto/config || \
                    echo "Could not chown /kallisto/config (may not have appropriate permissions)"
            fi

            # If the logs dir is bind mounted then chown it
            if [ "$(stat -c %u /kallisto/logs)" != "$(id -u kallisto)" ]; then
                chown -R kallisto:kallisto /kallisto/logs
            fi

            # If the data dir is bind mounted then chown it
            if [ "$(stat -c %u /kallisto/data)" != "$(id -u kallisto)" ]; then
                chown -R kallisto:kallisto /kallisto/data
            fi
        fi

        echo "====================================================================="
        echo "  Starting Kallisto Server (dropping privileges to kallisto user)"
        echo "  - DB Path: $DB_PATH"
        echo "  - Workers: $WORKERS"
        echo "====================================================================="

        exec gosu kallisto /app/kallisto_server \
            --workers="$WORKERS" \
            --db-path="$DB_PATH" \
            "$@"
    fi

    echo "====================================================================="
    echo "  Starting Kallisto Server"
    echo "  - DB Path: $DB_PATH"
    echo "  - Workers: $WORKERS"
    echo "====================================================================="

    exec /app/kallisto_server --workers="$WORKERS" --db-path="$DB_PATH" "$@"
fi

# Run any other command the user passes in
exec "$@"