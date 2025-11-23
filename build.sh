#!/bin/bash

# Kosmos Build Script

echo "========================================="
echo "  Kosmos Space Flight Simulator Build"
echo "========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if build directory exists
if [ -d "build" ]; then
    echo -e "${YELLOW}Build directory exists. Cleaning...${NC}"
    rm -rf build
fi

# Create build directory
mkdir build
cd build

echo -e "${GREEN}Configuring with CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}Building Kosmos...${NC}"
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}  Build successful!${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""
echo "Executable: build/bin/Kosmos"
echo "Libraries:  build/lib/"
echo ""
echo "To run: cd build/bin && ./Kosmos"
