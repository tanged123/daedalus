#!/usr/bin/env bash
set -e

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

BUILD_TYPE="${BUILD_TYPE:-Debug}"

show_help() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Run tests for the Daedalus project.

Options:
  --release         Test with release build
  -h, --help        Show this help message

Examples:
  ./scripts/test.sh                    # Run all tests
  ./scripts/test.sh --release          # Test release build
EOF
    exit 0
}

for arg in "$@"; do
    case $arg in
        -h|--help)
            show_help
            ;;
        --release)
            BUILD_TYPE="Release"
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
LOG_FILE="$PROJECT_ROOT/logs/tests_${TIMESTAMP}.log"

echo "=== Daedalus Tests ===" | tee "$LOG_FILE"
cd "$PROJECT_ROOT"

# Build if needed
if [ ! -f "build/daedalus_tests" ]; then
    echo "Build not found, building first..." | tee -a "$LOG_FILE"
    ./scripts/build.sh 2>&1 | tee -a "$LOG_FILE"
fi

# Run tests via ctest
cd build
ctest --output-on-failure 2>&1 | tee -a "$LOG_FILE"
cd "$PROJECT_ROOT"

# Create symlink to latest
ln -sf "tests_${TIMESTAMP}.log" "$PROJECT_ROOT/logs/tests.log"

echo ""
echo "Tests complete. Logs available at logs/tests_${TIMESTAMP}.log"
