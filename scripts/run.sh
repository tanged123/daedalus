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

# ─── WSL2/WSLg display robustness ───────────────────────────────────────
# GLFW 3.4 prefers Wayland when WAYLAND_DISPLAY is set. WSLg provides
# both X11 and Wayland, but Wayland EGL context creation is unreliable —
# causing the window to intermittently fail to appear.
# Force X11 backend for consistent rendering.
if [ -n "${WSL_DISTRO_NAME:-}" ]; then
    export GLFW_PLATFORM=x11
fi

if [ -z "${DISPLAY:-}" ]; then
    echo "ERROR: DISPLAY is not set. Ensure WSLg or an X server is running." >&2
    exit 1
fi

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

# Wait for Hermes WebSocket port to be ready (up to 10s)
echo "Waiting for Hermes on port $HERMES_PORT..."
for i in $(seq 100); do
    if (echo > "/dev/tcp/127.0.0.1/$HERMES_PORT") 2>/dev/null; then
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
