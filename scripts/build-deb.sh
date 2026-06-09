#!/bin/bash
set -euo pipefail

# Build Debian package for claw-compiler
# Usage: ./scripts/build-deb.sh [version]
# Requires: dpkg-deb, make

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION="${1:-$(git describe --tags --abbrev=0 2>/dev/null || echo "0.2.0")}"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo "amd64")"
PKG_NAME="claw-compiler"
PKG_DIR="${PROJECT_ROOT}/dist/${PKG_NAME}_${VERSION}_${ARCH}"

echo "=== Building Debian package ${PKG_NAME}_${VERSION}_${ARCH}.deb ==="

# Clean previous build
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/DEBIAN"
mkdir -p "$PKG_DIR/usr/bin"
mkdir -p "$PKG_DIR/usr/share/doc/claw-compiler"
mkdir -p "$PKG_DIR/usr/share/man/man1"

# Build binaries
cd "$PROJECT_ROOT"
make clean
make all CXX=clang++

# Install binaries
cp "$PROJECT_ROOT/claw" "$PKG_DIR/usr/bin/"
cp "$PROJECT_ROOT/claw-lsp" "$PKG_DIR/usr/bin/"
cp "$PROJECT_ROOT/claw-repl" "$PKG_DIR/usr/bin/"
cp "$PROJECT_ROOT/claw-debugger" "$PKG_DIR/usr/bin/"

# Install docs
cp "$PROJECT_ROOT/README.md" "$PROJECT_ROOT/LICENSE" "$PKG_DIR/usr/share/doc/claw-compiler/" 2>/dev/null || true
if [ -d "$PROJECT_ROOT/docs" ]; then
    cp -r "$PROJECT_ROOT/docs" "$PKG_DIR/usr/share/doc/claw-compiler/"
fi

# Create control file
cat > "$PKG_DIR/DEBIAN/control" <<EOF
Package: ${PKG_NAME}
Version: ${VERSION}
Section: devel
Priority: optional
Architecture: ${ARCH}
Depends: libreadline8, libc6
Recommends: llvm, clang
Maintainer: Claw Compiler Team <claw@example.com>
Description: Claw programming language compiler
 The Claw compiler is an AI-native language compiler with
 multiple backends: C, LLVM, AOT native, Bytecode VM, JIT,
 and WebAssembly.
EOF

# Build package
mkdir -p "$PROJECT_ROOT/dist"
dpkg-deb --build "$PKG_DIR" "${PROJECT_ROOT}/dist/${PKG_NAME}_${VERSION}_${ARCH}.deb"

echo ""
echo "Package built: dist/${PKG_NAME}_${VERSION}_${ARCH}.deb"
ls -lh "${PROJECT_ROOT}/dist/${PKG_NAME}_${VERSION}_${ARCH}.deb"
