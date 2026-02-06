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

# Ensure logs and build directories exist
mkdir -p "$PROJECT_ROOT/logs"
mkdir -p "$PROJECT_ROOT/build/coverage"

# Create timestamp
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="$PROJECT_ROOT/logs/coverage_${TIMESTAMP}.log"

cd "$PROJECT_ROOT"

echo "=== Daedalus Code Coverage ===" | tee "$LOG_FILE"
echo "" | tee -a "$LOG_FILE"

# Build with coverage flags
echo "Building with coverage instrumentation..." | tee -a "$LOG_FILE"
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_COVERAGE=ON 2>&1 | tee -a "$LOG_FILE"
ninja -C build 2>&1 | tee -a "$LOG_FILE"

# Reset coverage counters
echo "Resetting coverage counters..." | tee -a "$LOG_FILE"
lcov --zerocounters --directory build 2>&1 | tee -a "$LOG_FILE"
lcov --capture --initial --directory build --output-file build/coverage/base.info 2>&1 | tee -a "$LOG_FILE"

# Run tests
echo "Running tests..." | tee -a "$LOG_FILE"
cd build
ctest --output-on-failure 2>&1 | tee -a "$LOG_FILE"
cd "$PROJECT_ROOT"

# Capture coverage data
echo "Capturing coverage data..." | tee -a "$LOG_FILE"
lcov --capture --directory build --output-file build/coverage/test.info 2>&1 | tee -a "$LOG_FILE"
lcov --add-tracefile build/coverage/base.info \
     --add-tracefile build/coverage/test.info \
     --output-file build/coverage/total.info 2>&1 | tee -a "$LOG_FILE"

# Filter out system/test files
lcov --remove build/coverage/total.info \
     '/nix/*' '*/tests/*' '*/gtest/*' \
     --output-file build/coverage/coverage.info 2>&1 | tee -a "$LOG_FILE"

# Generate HTML report
genhtml build/coverage/coverage.info \
    --output-directory build/coverage/html 2>&1 | tee -a "$LOG_FILE"

# Create symlink to latest
ln -sf "coverage_${TIMESTAMP}.log" "$PROJECT_ROOT/logs/coverage.log"

echo "" | tee -a "$LOG_FILE"
echo "=== Coverage Report Generated ===" | tee -a "$LOG_FILE"
echo "HTML Report: build/coverage/html/index.html" | tee -a "$LOG_FILE"
echo "Log file:    logs/coverage_${TIMESTAMP}.log" | tee -a "$LOG_FILE"
