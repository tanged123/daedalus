#!/usr/bin/env bash
set -e

# Ensure we are in a Nix environment
if [ -z "$IN_NIX_SHELL" ]; then
    echo "Not in Nix environment. Re-running inside Nix..."
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    "$SCRIPT_DIR/dev.sh" "$0" "$@"
    exit $?
fi

# Handle arguments
CLEAN=false
BUILD_TYPE="${BUILD_TYPE:-Debug}"

# Default jobs: half of available cores, capped at 6 (minimum 2)
DEFAULT_JOBS=$(( $(nproc) / 2 ))
[ "$DEFAULT_JOBS" -lt 2 ] && DEFAULT_JOBS=2
[ "$DEFAULT_JOBS" -gt 6 ] && DEFAULT_JOBS=6
JOBS="${JOBS:-$DEFAULT_JOBS}"

NEXT_IS_JOBS=false

show_help() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Build the Daedalus project.

Build Type Options:
  --debug           Build with debug symbols (default)
  --release         Build with optimizations
  --relwithdebinfo  Build with optimizations and debug info
  --clean           Clean build directory before building

Other Options:
  -j, --jobs N      Number of parallel build jobs (default: half of cores)
  -h, --help        Show this help message

Examples:
  ./scripts/build.sh                    # Debug build
  ./scripts/build.sh --release          # Release build
  ./scripts/build.sh --clean --release  # Clean release build
EOF
    exit 0
}

for arg in "$@"; do
    if [ "$NEXT_IS_JOBS" = true ]; then
        if [[ "$arg" =~ ^[0-9]+$ ]]; then
            JOBS="$arg"
            NEXT_IS_JOBS=false
            continue
        else
            echo "Error: --jobs/-j requires a numeric argument, got: '$arg'" >&2
            exit 1
        fi
    fi

    case $arg in
        -h|--help)
            show_help
            ;;
        --clean)
            CLEAN=true
            ;;
        --debug)
            BUILD_TYPE="Debug"
            ;;
        --release)
            BUILD_TYPE="Release"
            ;;
        --relwithdebinfo)
            BUILD_TYPE="RelWithDebInfo"
            ;;
        --jobs=*|-j=*)
            JOBS="${arg#*=}"
            ;;
        --jobs|-j)
            NEXT_IS_JOBS=true
            ;;
        *)
            echo "Warning: Unknown argument ignored: $arg" >&2
            ;;
    esac
done

if [ "$NEXT_IS_JOBS" = true ]; then
    echo "Error: --jobs/-j requires a numeric argument" >&2
    exit 1
fi

if ! [[ "$JOBS" =~ ^[0-9]+$ ]]; then
    echo "Error: Invalid job count: '$JOBS' (must be numeric)" >&2
    exit 1
fi

if [ "$CLEAN" = true ]; then
    echo "Clean build requested."
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    "$SCRIPT_DIR/clean.sh"
fi

echo "Building with CMAKE_BUILD_TYPE=$BUILD_TYPE (jobs: $JOBS)"

# Show ccache stats before build
if command -v ccache &> /dev/null; then
    echo ""
    echo "=== ccache stats (before build) ==="
    ccache -s | grep -E "(Hits|Misses|Hit rate|Cache size)" || ccache -s | head -10
fi

cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

ninja -C build -j "$JOBS"

# Show ccache stats after build
if command -v ccache &> /dev/null; then
    echo ""
    echo "=== ccache stats (after build) ==="
    ccache -s | grep -E "(Hits|Misses|Hit rate|Cache size)" || ccache -s | head -10
fi
