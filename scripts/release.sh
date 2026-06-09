#!/bin/bash
set -euo pipefail

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 0.2.1"
    exit 1
fi

# Validate version format
if ! echo "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Error: version must be semver (e.g., 0.2.1)"
    exit 1
fi

echo "=== Releasing v$VERSION ==="

# Update version string in main.cpp
sed -i.bak "s/constexpr const char\* CLAW_VERSION = \".*\"/constexpr const char* CLAW_VERSION = \"$VERSION\"/" src/main.cpp 2>/dev/null || true
rm -f src/main.cpp.bak

# Update Homebrew formula
sed -i.bak "s/version \".*\"/version \"$VERSION\"/" homebrew/claw.rb 2>/dev/null || true
rm -f homebrew/claw.rb.bak

# Git commit
git add -A
git commit -m "Release v$VERSION"

# Create tag
git tag -a "v$VERSION" -m "Release v$VERSION"

echo ""
echo "Release v$VERSION prepared."
echo "Next steps:"
echo "  1. Review the commit: git show HEAD"
echo "  2. Push: git push origin main --tags"
echo "  3. GitHub Actions will build and publish the release"
