#!/usr/bin/env bash
# Launch Hermes in the background, then Daedalus in the foreground.
# Hermes is automatically killed when Daedalus exits (window closed).
#
# Usage: ./scripts/run.sh [config.yaml]
#   Default config: references/hermes/examples/websocket_telemetry.yaml
#   Icarus rocket:  ./scripts/run.sh references/hermes/examples/icarus_rocket.yaml
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG="${1:-$PROJECT_DIR/references/hermes/examples/websocket_telemetry.yaml}"
HERMES_PORT=8765

# ─── Enter Nix (single session for build + run) ─────────────────────────
if [ -z "${IN_NIX_SHELL:-}" ]; then
    exec "$SCRIPT_DIR/dev.sh" "$0" "$@"
fi

if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "ERROR: neither DISPLAY nor WAYLAND_DISPLAY is set. Ensure a GUI session is available." >&2
    exit 1
fi

socket_connectable() {
    local socket_path="$1"
    python - "$socket_path" <<'PY'
import socket
import sys

path = sys.argv[1]
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(0.5)
try:
    sock.connect(path)
except Exception:
    sys.exit(1)
finally:
    sock.close()
sys.exit(0)
PY
}

detect_display_platform() {
    local x11_ok=0
    local wayland_ok=0

    if [[ -n "${DISPLAY:-}" && "${DISPLAY}" =~ ^:([0-9]+) ]]; then
        local x11_socket="/tmp/.X11-unix/X${BASH_REMATCH[1]}"
        if [ -S "$x11_socket" ] && socket_connectable "$x11_socket"; then
            x11_ok=1
        fi
    fi

    if [ -n "${WAYLAND_DISPLAY:-}" ] && [ -n "${XDG_RUNTIME_DIR:-}" ]; then
        local wl_socket="${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}"
        if [ -S "$wl_socket" ] && socket_connectable "$wl_socket"; then
            wayland_ok=1
        fi
    fi

    if [ -z "${GLFW_PLATFORM:-}" ]; then
        if [ "$wayland_ok" -eq 1 ]; then
            export GLFW_PLATFORM=wayland
        elif [ "$x11_ok" -eq 1 ]; then
            export GLFW_PLATFORM=x11
        fi
    fi

    if [ "$x11_ok" -eq 0 ] && [ "$wayland_ok" -eq 0 ]; then
        echo "ERROR: no reachable GUI display backend." >&2
        echo "  DISPLAY=${DISPLAY:-<unset>}" >&2
        echo "  WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>}" >&2
        echo "  XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-<unset>}" >&2
        echo "Hint: restart your GUI session (WSLg/X server) and retry." >&2
        exit 1
    fi
}

detect_display_platform
echo "Using GLFW_PLATFORM=${GLFW_PLATFORM:-<auto>}"

# ─── Build ───────────────────────────────────────────────────────────────
"$SCRIPT_DIR/build.sh"

# ─── Launch Hermes ───────────────────────────────────────────────────────
HERMES_PID=""

cleanup() {
    local exit_code="$?"
    trap - EXIT INT TERM HUP

    if [ -n "$HERMES_PID" ] && kill -0 "$HERMES_PID" 2>/dev/null; then
        kill -TERM "$HERMES_PID" 2>/dev/null || true
        for _ in $(seq 30); do
            kill -0 "$HERMES_PID" 2>/dev/null || break
            sleep 0.1
        done
        kill -KILL "$HERMES_PID" 2>/dev/null || true
        wait "$HERMES_PID" 2>/dev/null || true
    fi

    exit "$exit_code"
}
trap cleanup EXIT INT TERM HUP

hermes run "$CONFIG" &
HERMES_PID="$!"

# Wait for Hermes WebSocket port to be ready (up to 10s).
# Check /proc/net/tcp to avoid connecting — a TCP probe triggers a
# spurious "opening handshake failed" error in the WebSocket server.
HERMES_PORT_HEX=$(printf '%04X' "$HERMES_PORT")
echo "Waiting for Hermes on port $HERMES_PORT..."
for i in $(seq 100); do
    if grep -q ":${HERMES_PORT_HEX} " /proc/net/tcp 2>/dev/null; then
        echo "Hermes ready."
        break
    fi
    if ! kill -0 "$HERMES_PID" 2>/dev/null; then
        echo "ERROR: Hermes exited before becoming ready." >&2
        exit 1
    fi
    if [ "$i" -eq 100 ]; then
        echo "ERROR: Hermes did not start within 10s." >&2
        exit 1
    fi
    sleep 0.1
done

# ─── Launch Daedalus ────────────────────────────────────────────────────
echo "Starting Daedalus..."
"$PROJECT_DIR/build/daedalus"
