#!/usr/bin/env bash
# Launch Hermes + Daedalus side-by-side in a tmux session.
# Usage: ./scripts/run.sh [config.yaml]
#   Default config: references/hermes/examples/websocket_telemetry.yaml
#   Icarus rocket:  ./scripts/run.sh references/hermes/examples/icarus_rocket.yaml

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG="${1:-$PROJECT_DIR/references/hermes/examples/websocket_telemetry.yaml}"
SESSION="daedalus"

# Build first (outside tmux)
"$SCRIPT_DIR/build.sh"

# Kill existing session if present
tmux kill-session -t "$SESSION" 2>/dev/null || true

tmux new-session -d -s "$SESSION" -c "$PROJECT_DIR"
tmux send-keys -t "$SESSION" "$SCRIPT_DIR/dev.sh hermes run $CONFIG" Enter
tmux split-window -h -t "$SESSION" -c "$PROJECT_DIR"
tmux send-keys -t "$SESSION" "$SCRIPT_DIR/dev.sh $PROJECT_DIR/build/daedalus" Enter
tmux select-pane -t "$SESSION:0.0"
tmux attach -t "$SESSION"
