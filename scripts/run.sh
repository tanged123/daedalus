#!/usr/bin/env bash
# Launch Hermes in the background, then Daedalus in the foreground.
# Hermes is automatically killed when Daedalus exits (window closed).
#
# Usage: ./scripts/run.sh [config.yaml]
#   Default config: references/hermes/examples/websocket_telemetry.yaml
#   Icarus rocket:  ./scripts/run.sh references/hermes/examples/icarus_rocket.yaml
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG="${1:-$PROJECT_DIR/references/hermes/examples/websocket_telemetry.yaml}"
HERMES_PID=""
HERMES_PGID=""
HERMES_OWN_GROUP=0

# Build first
"$SCRIPT_DIR/build.sh"

# Start Hermes in background
if command -v setsid >/dev/null 2>&1; then
    setsid "$SCRIPT_DIR/dev.sh" hermes run "$CONFIG" &
    HERMES_OWN_GROUP=1
else
    "$SCRIPT_DIR/dev.sh" hermes run "$CONFIG" &
fi
HERMES_PID="$!"
HERMES_PGID="$(ps -o pgid= "$HERMES_PID" 2>/dev/null | tr -d '[:space:]' || true)"

cleanup() {
    local exit_code="$?"
    trap - EXIT INT TERM HUP

    if [[ "$HERMES_OWN_GROUP" -eq 1 && -n "$HERMES_PGID" ]]; then
        kill -TERM -- "-$HERMES_PGID" 2>/dev/null || true
    elif [[ -n "$HERMES_PID" ]]; then
        kill -TERM "$HERMES_PID" 2>/dev/null || true
    fi

    if [[ -n "$HERMES_PID" ]]; then
        for _ in {1..30}; do
            if ! kill -0 "$HERMES_PID" 2>/dev/null; then
                break
            fi
            sleep 0.1
        done

        if kill -0 "$HERMES_PID" 2>/dev/null; then
            if [[ "$HERMES_OWN_GROUP" -eq 1 && -n "$HERMES_PGID" ]]; then
                kill -KILL -- "-$HERMES_PGID" 2>/dev/null || true
            else
                kill -KILL "$HERMES_PID" 2>/dev/null || true
            fi
        fi

        wait "$HERMES_PID" 2>/dev/null || true
    fi

    exit "$exit_code"
}
trap cleanup EXIT INT TERM HUP

# Give Hermes a moment to start listening
sleep 1

# Run Daedalus in foreground — closing the window returns here
"$SCRIPT_DIR/dev.sh" "$PROJECT_DIR/build/daedalus"