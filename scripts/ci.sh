#!/usr/bin/env bash
set -eo pipefail

# Ensure we are in a Nix environment
if [ -z "$IN_NIX_SHELL" ]; then
    echo "Not in Nix environment. Re-running inside Nix..."
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    "$SCRIPT_DIR/dev.sh" "$0" "$@"
    exit $?
fi

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

show_help() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Run CI checks for the Daedalus project.

Options:
  -h, --help        Show this help message

Examples:
  ./scripts/ci.sh   # Full CI (clean release build + tests)
EOF
    exit 0
}

for arg in "$@"; do
    case $arg in
        -h|--help)
            show_help
            ;;
        *)
            echo "Warning: Unknown argument ignored: $arg" >&2
            ;;
    esac
done

# Ensure logs directory exists
mkdir -p "$PROJECT_ROOT/logs"

# Create timestamp
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="$PROJECT_ROOT/logs/ci_${TIMESTAMP}.log"

# Run build and test scripts (CI uses Release by default for performance)
echo "Running CI (Release build)..."
cd "$PROJECT_ROOT"
(./scripts/build.sh --clean --release && ./scripts/test.sh --release) 2>&1 | tee "$LOG_FILE"

# Create symlink to latest
ln -sf "ci_${TIMESTAMP}.log" "$PROJECT_ROOT/logs/ci.log"

echo "CI Complete. Logs available at logs/ci_${TIMESTAMP}.log (symlinked to logs/ci.log)"
