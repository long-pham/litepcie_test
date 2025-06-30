#!/bin/bash
# Comprehensive benchmark script for LitePCIe DMA tests

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== LitePCIe DMA Benchmark Suite ===${NC}"
echo "This benchmark compares all available implementations"
echo ""

# Check for device
if [ ! -e /dev/litepcie0 ]; then
    echo -e "${RED}Error: /dev/litepcie0 not found!${NC}"
    echo "Please ensure the LitePCIe driver is loaded."
    exit 1
fi

# Check for root privileges
if [ "$EUID" -ne 0 ]; then 
    echo -e "${YELLOW}Warning: Not running as root. Some tests may fail.${NC}"
    echo "For best results, run: sudo $0"
    echo ""
fi

# Determine build directory
if [ -d "build" ] && [ -f "build/litepcie_dma_test_optimized" ]; then
    BUILD_DIR="build"
    echo -e "${BLUE}Using CMake build directory: $BUILD_DIR${NC}"
elif [ -f "./litepcie_dma_test_optimized" ]; then
    BUILD_DIR="."
    echo -e "${BLUE}Using Make build in current directory${NC}"
else
    echo -e "${YELLOW}No build found. Building now...${NC}"
    if [ -f "build.sh" ]; then
        ./build.sh
        BUILD_DIR="build"
    elif [ -f "Makefile" ]; then
        make
        BUILD_DIR="."
    else
        echo -e "${RED}Error: No build system found!${NC}"
        exit 1
    fi
fi

# Test duration
DURATION=${1:-10}
echo -e "${BLUE}Test duration: ${DURATION} seconds${NC}"
echo ""

# Function to run and parse results
run_benchmark() {
    local name=$1
    local cmd=$2
    local desc=$3
    
    echo -e "${GREEN}>>> $name${NC}"
    echo -e "${BLUE}$desc${NC}"
    echo "Command: $cmd"
    echo "---"
    
    # Run the command and capture output
    OUTPUT=$($cmd 2>&1)
    
    # Extract performance metrics
    if echo "$OUTPUT" | grep -q "TX:"; then
        # Extract last line with performance data
        PERF_LINE=$(echo "$OUTPUT" | grep -E "^\[.*TX:" | tail -1)
        if [ ! -z "$PERF_LINE" ]; then
            echo "$PERF_LINE"
            # Extract TX and RX values
            TX_GBPS=$(echo "$PERF_LINE" | sed -n 's/.*TX: *\([0-9.]*\) Gbps.*/\1/p')
            RX_GBPS=$(echo "$PERF_LINE" | sed -n 's/.*RX: *\([0-9.]*\) Gbps.*/\1/p')
            echo -e "Results: TX=${GREEN}${TX_GBPS}${NC} Gbps, RX=${GREEN}${RX_GBPS}${NC} Gbps"
        fi
    else
        echo "$OUTPUT" | tail -5
    fi
    echo ""
}

# Set CPU to performance mode if available
if command -v cpupower &> /dev/null; then
    echo -e "${YELLOW}Setting CPU to performance mode...${NC}"
    sudo cpupower frequency-set -g performance 2>/dev/null || true
    echo ""
fi

echo -e "${GREEN}=== Starting Benchmarks ===${NC}"
echo ""

# 1. Original implementation
if [ -f "$BUILD_DIR/litepcie_util" ]; then
    run_benchmark \
        "Original Implementation" \
        "timeout ${DURATION}s $BUILD_DIR/litepcie_util dma_test" \
        "Standard LitePCIe utility"
fi

# 2. Optimized V1
if [ -f "$BUILD_DIR/litepcie_dma_test_optimized" ]; then
    run_benchmark \
        "Optimized V1 - Default" \
        "$BUILD_DIR/litepcie_dma_test_optimized -t $DURATION" \
        "Multi-threaded with TX bottleneck"
    
    run_benchmark \
        "Optimized V1 - Zero-Copy" \
        "$BUILD_DIR/litepcie_dma_test_optimized -z -t $DURATION" \
        "Zero-copy mode enabled"
    
    run_benchmark \
        "Optimized V1 - Max Performance" \
        "$BUILD_DIR/litepcie_dma_test_optimized -z -n -t $DURATION" \
        "Zero-copy, no verification"
fi

# 3. Optimized V2
if [ -f "$BUILD_DIR/litepcie_dma_test_optimized_v2" ]; then
    run_benchmark \
        "Optimized V2 - Default" \
        "$BUILD_DIR/litepcie_dma_test_optimized_v2 -t $DURATION" \
        "Balanced TX/RX performance"
    
    run_benchmark \
        "Optimized V2 - Fast Polling" \
        "$BUILD_DIR/litepcie_dma_test_optimized_v2 -i 10 -t $DURATION" \
        "10µs polling interval"
    
    run_benchmark \
        "Optimized V2 - Max Performance" \
        "$BUILD_DIR/litepcie_dma_test_optimized_v2 -z -n -i 50 -t $DURATION" \
        "Zero-copy, no verification, 50µs polling"
fi

# 4. Latency tests
if [ -f "$BUILD_DIR/litepcie_latency_test_simple" ]; then
    echo -e "${GREEN}>>> Latency Test${NC}"
    echo -e "${BLUE}Simple round-trip latency measurement${NC}"
    $BUILD_DIR/litepcie_latency_test_simple 2>&1 | grep -E "(Average|Min|Max|packets)" | head -10
    echo ""
fi

# Restore CPU governor
if command -v cpupower &> /dev/null; then
    echo -e "${YELLOW}Restoring CPU governor...${NC}"
    sudo cpupower frequency-set -g ondemand 2>/dev/null || true
fi

echo -e "${GREEN}=== Benchmark Summary ===${NC}"
echo ""
echo "Expected performance on PCIe Gen3 x1:"
echo "  - Theoretical maximum: ~8 Gbps (after overhead)"
echo "  - Original: Variable, often lower"
echo "  - Optimized V1: TX ~0.25 Gbps, RX ~4.5 Gbps"
echo "  - Optimized V2: TX ~8.5 Gbps, RX ~8.5 Gbps"
echo ""
echo "Tips for better performance:"
echo "  1. Run with sudo for real-time scheduling"
echo "  2. Use CPU isolation (isolcpus kernel parameter)"
echo "  3. Disable CPU frequency scaling"
echo "  4. Use external loopback for true bidirectional test"
echo ""
echo -e "${GREEN}Benchmark complete!${NC}"