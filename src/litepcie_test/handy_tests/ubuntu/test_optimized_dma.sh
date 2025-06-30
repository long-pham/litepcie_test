#!/bin/bash
# Test script for optimized DMA test

echo "=== LitePCIe Optimized DMA Test ==="
echo "This test requires a LitePCIe device at /dev/litepcie0"
echo ""

# Check if the device exists
if [ ! -e /dev/litepcie0 ]; then
    echo "Error: /dev/litepcie0 not found!"
    echo "Please ensure the LitePCIe driver is loaded and the device is present."
    exit 1
fi

# Check if running as root (may be required for DMA)
if [ "$EUID" -ne 0 ]; then 
    echo "Warning: Not running as root. DMA operations may require root privileges."
fi

# Determine build directory and build if needed
if [ -d "build" ]; then
    # CMake build
    BUILD_DIR="build"
    echo "Using CMake build directory: $BUILD_DIR"
    
    # Build if not exists
    if [ ! -f "$BUILD_DIR/litepcie_dma_test_optimized" ] || [ ! -f "$BUILD_DIR/litepcie_dma_test_optimized_v2" ]; then
        echo "Building optimized DMA tests..."
        ./build.sh
    fi
elif [ -f "Makefile" ]; then
    # Make build
    BUILD_DIR="."
    echo "Using Make build in current directory"
    
    # Build if not exists
    if [ ! -f "./litepcie_dma_test_optimized" ] || [ ! -f "./litepcie_dma_test_optimized_v2" ]; then
        echo "Building optimized DMA tests..."
        make
    fi
else
    echo "Error: No build system found. Please ensure you're in the correct directory."
    exit 1
fi

# Test programs
PROG_V1="$BUILD_DIR/litepcie_dma_test_optimized"
PROG_V2="$BUILD_DIR/litepcie_dma_test_optimized_v2"
UTIL="$BUILD_DIR/litepcie_util"

echo ""
echo "Running DMA tests..."
echo "===================="

# Test V1 if available
if [ -f "$PROG_V1" ]; then
    echo ""
    echo "=== Testing Optimized V1 ==="
    
    # Test 1: Basic internal loopback test
    echo ""
    echo "Test 1: Internal loopback, default settings"
    echo "$PROG_V1 -t 5"
    $PROG_V1 -t 5
    
    # Test 2: Zero-copy mode
    echo ""
    echo ""
    echo "Test 2: Internal loopback with zero-copy"
    echo "$PROG_V1 -z -t 5"
    $PROG_V1 -z -t 5
    
    # Test 3: Different patterns
    echo ""
    echo ""
    echo "Test 3: Sequential pattern test"
    echo "$PROG_V1 -p 0 -t 5"
    $PROG_V1 -p 0 -t 5
    
    # Test 4: Performance mode (no verification)
    echo ""
    echo ""
    echo "Test 4: Performance mode (no data verification)"
    echo "$PROG_V1 -n -t 5"
    $PROG_V1 -n -t 5
    
    # Test 5: External loopback if available
    echo ""
    echo ""
    echo "Test 5: External loopback test (if hardware supports it)"
    echo "$PROG_V1 -l -t 5"
    $PROG_V1 -l -t 5 2>/dev/null || echo "External loopback not available or failed"
fi

# Test V2 if available
if [ -f "$PROG_V2" ]; then
    echo ""
    echo ""
    echo "=== Testing Optimized V2 (Balanced Performance) ==="
    
    # Test 1: Basic test
    echo ""
    echo "Test 1: Default settings"
    echo "$PROG_V2 -t 5"
    $PROG_V2 -t 5
    
    # Test 2: Maximum performance
    echo ""
    echo ""
    echo "Test 2: Maximum performance (zero-copy, no verification, fast polling)"
    echo "$PROG_V2 -z -n -i 50 -t 5"
    $PROG_V2 -z -n -i 50 -t 5
    
    # Test 3: Different polling intervals
    echo ""
    echo ""
    echo "Test 3: Testing different polling intervals"
    echo "Fast polling (10 µs):"
    $PROG_V2 -i 10 -t 2
    echo ""
    echo "Slow polling (1000 µs):"
    $PROG_V2 -i 1000 -t 2
fi

# Compare with original if available
if [ -f "$UTIL" ]; then
    echo ""
    echo ""
    echo "=== Comparison with Original Implementation ==="
    echo "Running: $UTIL dma_test"
    timeout 5s $UTIL dma_test 2>&1 | tail -20
fi

echo ""
echo "=== Test Summary ==="
echo "All tests completed. Check above for any errors."
echo ""
echo "Expected performance:"
echo "  V1: TX ~0.25 Gbps, RX ~4.5 Gbps (TX bottleneck)"
echo "  V2: TX ~8.5 Gbps, RX ~8.5 Gbps (balanced)"
echo ""
echo "For continuous monitoring, run:"
echo "  watch -n 1 'dmesg | tail -20'  # In another terminal"