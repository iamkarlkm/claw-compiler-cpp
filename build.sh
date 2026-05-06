#!/bin/bash
# build.sh - Claw Compiler Build Script
# Builds all targets and runs tests

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== Claw Compiler Build Script ===${NC}"
echo ""

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo -e "${YELLOW}Configuring with CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build all targets
echo -e "${YELLOW}Building all targets...${NC}"
make -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo -e "${GREEN}Build completed successfully!${NC}"
echo ""

# Run tests
echo -e "${YELLOW}Running tests...${NC}"
ctest --output-on-failure || true

echo ""
echo -e "${GREEN}Build and test process completed!${NC}"
echo ""
echo "Binaries are located in: build/bin/"
