#!/usr/bin/env bash
set -e

# Ensure we are in a Nix environment
if [ -z "$IN_NIX_SHELL" ]; then
    echo "Not in Nix environment. Re-running inside Nix..."
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    "$SCRIPT_DIR/dev.sh" "$0" "$@"
    exit $?
fi

# Setup directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/coverage"
REPORT_DIR="$BUILD_DIR/html"

mkdir -p "$BUILD_DIR"
mkdir -p "$PROJECT_ROOT/logs"

TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="$PROJECT_ROOT/logs/coverage_${TIMESTAMP}.log"

echo "=== Daedalus Code Coverage Generation ===" | tee "$LOG_FILE"
echo "Build Directory: $BUILD_DIR" | tee -a "$LOG_FILE"

# 1. Configure with Coverage Enabled
echo "Configuring with coverage enabled..." | tee -a "$LOG_FILE"
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" -DENABLE_COVERAGE=ON -G Ninja 2>&1 | tee -a "$LOG_FILE"

# 2. Build
echo "Building..." | tee -a "$LOG_FILE"
cmake --build "$BUILD_DIR" 2>&1 | tee -a "$LOG_FILE"

# 3. Run Tests
echo "Running tests..." | tee -a "$LOG_FILE"
CTEST_OUTPUT_ON_FAILURE=1 cmake --build "$BUILD_DIR" --target test 2>&1 | tee -a "$LOG_FILE"

# Determine GCOV tool
GCOV_TOOL=""
# Prioritize llvm-cov for Clang builds (common in Nix/LLVM environments)
if command -v llvm-cov &> /dev/null; then
    # Create wrapper script for llvm-cov gcov
    GCOV_WRAPPER="$BUILD_DIR/gcov_wrapper.sh"
    echo '#!/bin/sh' > "$GCOV_WRAPPER"
    echo 'exec llvm-cov gcov "$@"' >> "$GCOV_WRAPPER"
    chmod +x "$GCOV_WRAPPER"
    GCOV_TOOL="$GCOV_WRAPPER"
elif command -v gcov &> /dev/null; then
    GCOV_TOOL="gcov"
else
    echo "Error: Neither gcov nor llvm-cov found." | tee -a "$LOG_FILE"
    exit 1
fi

echo "Capturing coverage data using $GCOV_TOOL..." | tee -a "$LOG_FILE"
lcov --capture --directory "$BUILD_DIR" --output-file "$BUILD_DIR/coverage.info" \
    --gcov-tool "$GCOV_TOOL" \
    --ignore-errors mismatch,inconsistent,unsupported,format,empty 2>&1 | tee -a "$LOG_FILE"

# 4. Filter coverage data
# Remove external libraries (Nix, system headers) and test files from coverage
echo "Filtering coverage data..." | tee -a "$LOG_FILE"
lcov --remove "$BUILD_DIR/coverage.info" \
    '/nix/*' \
    '/usr/*' \
    '*/tests/*' \
    '*/_deps/*' \
    --output-file "$BUILD_DIR/coverage_clean.info" \
    --ignore-errors mismatch,inconsistent,unsupported,format,unused,empty 2>&1 | tee -a "$LOG_FILE"

# 5. Generate HTML Report
echo "Generating HTML report..." | tee -a "$LOG_FILE"
genhtml "$BUILD_DIR/coverage_clean.info" --output-directory "$REPORT_DIR" \
    --ignore-errors inconsistent,corrupt,unsupported,category,empty,missing 2>&1 | tee -a "$LOG_FILE"

echo "" | tee -a "$LOG_FILE"
echo "=== Coverage report generated ===" | tee -a "$LOG_FILE"
echo "HTML Report: $REPORT_DIR/index.html" | tee -a "$LOG_FILE"
echo "Log file: $LOG_FILE" | tee -a "$LOG_FILE"

# Create symlink to latest log
ln -sf "coverage_${TIMESTAMP}.log" "$PROJECT_ROOT/logs/coverage.log"
