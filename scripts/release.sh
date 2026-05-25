#!/bin/bash
# Claw Compiler Release Script
# Usage: ./scripts/release.sh [version]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

VERSION="${1:-$(cat "$PROJECT_DIR/VERSION")}"
RELEASE_DIR="$PROJECT_DIR/release"
ARCHIVE_NAME="claw-${VERSION}-$(uname -s)-$(uname -m).tar.gz"

echo "=== Claw Compiler Release v${VERSION} ==="

cd "$PROJECT_DIR"

# Clean previous builds
make clean
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"

# Run tests
echo "Running tests..."
make test

# Build all targets
echo "Building binaries..."
make all

# Verify binaries exist
for bin in claw claw-lsp claw-repl; do
    if [[ ! -f "$bin" ]]; then
        echo "Error: $bin not found after build"
        exit 1
    fi
    echo "  $bin: $(ls -lh "$bin" | awk '{print $5}')"
done

# Create archive
echo "Creating archive: $ARCHIVE_NAME"
tar czf "$RELEASE_DIR/$ARCHIVE_NAME" \
    claw claw-lsp claw-repl \
    README.md LICENSE CHANGELOG.md VERSION

# Generate checksums
cd "$RELEASE_DIR"
shasum -a 256 "$ARCHIVE_NAME" > "${ARCHIVE_NAME}.sha256"

echo ""
echo "Release artifacts in $RELEASE_DIR:"
ls -lh "$RELEASE_DIR"

echo ""
echo "SHA-256:"
cat "${ARCHIVE_NAME}.sha256"

echo ""
echo "Next steps:"
echo "  1. Tag the release: git tag v${VERSION} && git push origin v${VERSION}"
echo "  2. Create GitHub Release and upload $RELEASE_DIR/$ARCHIVE_NAME"
echo "  3. Update homebrew formula with new URL and SHA-256"
