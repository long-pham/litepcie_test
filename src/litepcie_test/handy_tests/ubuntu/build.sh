#!/bin/bash

# Build script for LitePCIe DMA test with CMake

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}LitePCIe DMA Test Build Script${NC}"
echo "================================"

# Parse command line arguments
BUILD_TYPE="Release"
CLEAN_BUILD=0
VERBOSE=0
BUILD_KERNEL=0

while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -c|--clean)
            CLEAN_BUILD=1
            shift
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -k|--kernel)
            BUILD_KERNEL=1
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  -d, --debug     Build in debug mode"
            echo "  -c, --clean     Clean build (remove build directory first)"
            echo "  -v, --verbose   Verbose build output"
            echo "  -k, --kernel    Build kernel modules (Linux only, requires kernel headers)"
            echo "  -h, --help      Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Create build directory
BUILD_DIR="build"

if [ $CLEAN_BUILD -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${GREEN}Creating build directory...${NC}"
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Configure with CMake
echo -e "${GREEN}Configuring with CMake...${NC}"
echo "Build type: $BUILD_TYPE"

CMAKE_ARGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
if [ $BUILD_TYPE == "Debug" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DDEBUG=ON"
fi
if [ $BUILD_KERNEL -eq 1 ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DBUILD_KERNEL_MODULE=ON"
fi

if ! cmake $CMAKE_ARGS ..; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi

# Build
echo -e "${GREEN}Building...${NC}"
BUILD_ARGS=""
if [ $VERBOSE -eq 1 ]; then
    BUILD_ARGS="--verbose"
fi

if ! cmake --build . $BUILD_ARGS -- -j$(nproc); then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}Build completed successfully!${NC}"
echo ""
echo "Executables built:"
echo "  DMA Test Programs:"
echo "    - $BUILD_DIR/litepcie_dma_test_optimized"
echo "    - $BUILD_DIR/litepcie_dma_test_optimized_v2"
echo "  User Utilities:"
echo "    - $BUILD_DIR/litepcie_util"
echo "    - $BUILD_DIR/litepcie_test"
echo "    - $BUILD_DIR/litepcie_latency_test"
echo "    - $BUILD_DIR/litepcie_latency_test_simple"
echo "    - $BUILD_DIR/litepcie_latency_test_final"

if [ $BUILD_KERNEL -eq 1 ]; then
    echo ""
    echo "Kernel module targets available:"
    echo "  - cd $BUILD_DIR && make kernel_modules    # Build kernel modules"
    echo "  - cd $BUILD_DIR && sudo make install_kernel_modules  # Install modules (requires root)"
    echo "  - cd $BUILD_DIR && make clean_kernel_modules  # Clean kernel build"
fi

echo ""
echo "To run the tests:"
echo "  cd $BUILD_DIR"
echo "  ./litepcie_dma_test_optimized -h"
echo "  ./litepcie_dma_test_optimized_v2 -h"
echo ""
echo "To run benchmarks:"
echo "  cd $BUILD_DIR && make benchmark"