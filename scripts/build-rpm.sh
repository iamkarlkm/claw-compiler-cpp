#!/bin/bash
set -euo pipefail

# Build RPM package for claw-compiler
# Usage: ./scripts/build-rpm.sh [version]
# Requires: rpmbuild, make

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION="${1:-$(git describe --tags --abbrev=0 2>/dev/null || echo "0.2.0")}"
RELEASE="1"
ARCH="$(uname -m)"
PKG_NAME="claw-compiler"

echo "=== Building RPM package ${PKG_NAME}-${VERSION}-${RELEASE}.${ARCH}.rpm ==="

# Set up RPM build tree
RPMBUILD_DIR="${HOME}/rpmbuild"
mkdir -p "$RPMBUILD_DIR"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# Create source tarball
SOURCE_TARBALL="${RPMBUILD_DIR}/SOURCES/${PKG_NAME}-${VERSION}.tar.gz"
cd "$PROJECT_ROOT"
git archive --prefix="${PKG_NAME}-${VERSION}/" -o "$SOURCE_TARBALL" HEAD 2>/dev/null || \
    tar czf "$SOURCE_TARBALL" --exclude='.git' --exclude='build' --exclude='dist' -C "$PROJECT_ROOT" .

# Create spec file
cat > "$RPMBUILD_DIR/SPECS/${PKG_NAME}.spec" <<EOF
Name:           ${PKG_NAME}
Version:        ${VERSION}
Release:        ${RELEASE}%{?dist}
Summary:        Claw programming language compiler
License:        MIT
URL:            https://github.com/claw-lang/claw-compiler
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  clang, llvm, make, readline-devel
Requires:       readline, llvm

%description
The Claw compiler is an AI-native language compiler with multiple
backends: C, LLVM, AOT native, Bytecode VM, JIT, and WebAssembly.

%prep
%setup -q

%build
make all CXX=clang++

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_docdir}/%{name}
cp claw claw-lsp claw-repl claw-debugger %{buildroot}%{_bindir}/
cp -r docs %{buildroot}%{_docdir}/%{name}/ 2>/dev/null || true
cp README.md LICENSE %{buildroot}%{_docdir}/%{name}/ 2>/dev/null || true

%files
%{_bindir}/claw
%{_bindir}/claw-lsp
%{_bindir}/claw-repl
%{_bindir}/claw-debugger
%{_docdir}/%{name}

%changelog
* $(date '+%a %b %d %Y') Claw Compiler Team <claw@example.com> - ${VERSION}-${RELEASE}
- Release ${VERSION}
EOF

# Build RPM
rpmbuild -ba "$RPMBUILD_DIR/SPECS/${PKG_NAME}.spec"

# Copy result to project dist directory
mkdir -p "$PROJECT_ROOT/dist"
cp "$RPMBUILD_DIR/RPMS/${ARCH}/${PKG_NAME}-${VERSION}-${RELEASE}.*.${ARCH}.rpm" "$PROJECT_ROOT/dist/" 2>/dev/null || \
    find "$RPMBUILD_DIR/RPMS" -name "${PKG_NAME}-${VERSION}-${RELEASE}.*.${ARCH}.rpm" -exec cp {} "$PROJECT_ROOT/dist/" \;

echo ""
echo "Package built:"
ls -lh "$PROJECT_ROOT/dist/"*.rpm
