#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HOOK_DIR="$PROJECT_ROOT/.git/hooks"
SOURCE_HOOK="$PROJECT_ROOT/.github/hooks/pre-commit"

# ------------------------------------------------------------------------------
# Pre-commit hook installation
# ------------------------------------------------------------------------------

if [ ! -d "$HOOK_DIR" ]; then
    echo "Error: .git/hooks directory not found. Are you in a git repository?"
    exit 1
fi

if [ ! -f "$SOURCE_HOOK" ]; then
    echo "Error: Pre-commit hook not found at $SOURCE_HOOK"
    exit 1
fi

cp "$SOURCE_HOOK" "$HOOK_DIR/pre-commit"
chmod +x "$HOOK_DIR/pre-commit"

echo "✓ Pre-commit hook installed successfully!"
echo "  Your code will be auto-formatted before each commit."

# ------------------------------------------------------------------------------
# Beads merge driver configuration
# ------------------------------------------------------------------------------

echo ""
echo "Configuring beads merge driver for .beads/issues.jsonl..."

# Check if bd is available on PATH
if ! command -v bd &> /dev/null; then
    echo ""
    echo "⚠ Warning: 'bd' command not found on PATH."
    echo ""
    echo "  The beads merge driver requires the 'bd' CLI tool."
    echo "  Without it, git merge conflicts on .beads/issues.jsonl will need"
    echo "  manual resolution."
    echo ""
    echo "  To install bd, run:"
    echo "    curl -fsSL https://raw.githubusercontent.com/steveyegge/beads/main/scripts/install.sh | bash"
    echo ""
    echo "  After installing, re-run this script to configure the merge driver."
    echo ""
    echo "  Skipping merge driver configuration."
else
    # Configure the beads merge driver in the repo's local git config
    git config merge.beads.name "Beads JSONL 3-way merge"
    git config merge.beads.driver "bd merge %A %O %A %B"

    echo "✓ Beads merge driver configured!"
    echo "   Git will use 'bd merge' for 3-way merges on .beads/issues.jsonl"
fi

echo ""
echo "Setup complete!"
