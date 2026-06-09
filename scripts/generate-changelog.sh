#!/bin/bash
set -euo pipefail

# Generate CHANGELOG.md entries from git history
# Usage: ./scripts/generate-changelog.sh [version]
# Outputs to stdout; redirect to file as needed.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION="${1:-$(git describe --tags --abbrev=0 2>/dev/null || echo "unreleased")}"
PREV_TAG="$(git describe --tags --abbrev=0 HEAD~1 2>/dev/null || echo "")"

echo "# Changelog"
echo ""
echo "All notable changes to this project will be documented in this file."
echo ""
echo "The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)."
echo ""

if [ "$VERSION" = "unreleased" ]; then
    echo "## [Unreleased]"
else
    echo "## [$VERSION]"
fi

echo ""

# If we have a previous tag, get commits between tags; otherwise get all commits
if [ -n "$PREV_TAG" ]; then
    COMMIT_RANGE="${PREV_TAG}..HEAD"
else
    COMMIT_RANGE="HEAD"
fi

# Helper to collect commits matching a pattern
collect_commits() {
    git log "$COMMIT_RANGE" --pretty=format:"%s" --no-merges | grep -iE "$1" || true
}

ADDED=$(collect_commits '^add|^feat')
CHANGED=$(collect_commits '^change|^update|^refactor|^perf')
FIXED=$(collect_commits '^fix|^bug|^correct')
REMOVED=$(collect_commits '^remove|^delete|^drop')
SECURITY=$(collect_commits '^security|^vuln|^cve')

print_section() {
    local title="$1"
    local items="$2"
    if [ -n "$items" ]; then
        echo "### $title"
        echo "$items" | while IFS= read -r line; do
            [ -n "$line" ] && echo "- $line"
        done
        echo ""
    fi
}

print_section "Added" "$ADDED"
print_section "Changed" "$CHANGED"
print_section "Fixed" "$FIXED"
print_section "Removed" "$REMOVED"
print_section "Security" "$SECURITY"
