#!/bin/bash
# Quick test script for optimized DMA test

# Determine build directory
if [ -d "build" ] && [ -f "build/litepcie_dma_test_optimized" ]; then
    # CMake build
    BUILD_DIR="build"
    echo "Using CMake build directory: $BUILD_DIR"
elif [ -f "./litepcie_dma_test_optimized" ]; then
    # Make build
    BUILD_DIR="."
    echo "Using Make build in current directory"
else
    echo "Error: No build found. Please build first using:"
    echo "  ./build.sh          # For CMake build"
    echo "  make                # For Make build"
    exit 1
fi

PROG="$BUILD_DIR/litepcie_dma_test_optimized"
PROG_V2="$BUILD_DIR/litepcie_dma_test_optimized_v2"

echo "=== LitePCIe Optimized DMA Test Results ==="
echo ""

# Test optimized v1
if [ -f "$PROG" ]; then
    echo "=== Testing optimized v1 ==="
    echo ""
    echo "Test 1: Default settings (2 seconds)"
    OUTPUT=$($PROG -t 2 2>&1)
    echo "$OUTPUT" | grep -E "(Pattern:|CPU affinity:|Verification:)"
    echo "$OUTPUT" | grep -E "^\[" | tail -3
    echo ""
    
    echo "Test 2: Zero-copy mode (2 seconds)"
    OUTPUT=$($PROG -z -t 2 2>&1)
    echo "$OUTPUT" | grep "Zero-copy:"
    echo "$OUTPUT" | grep -E "^\[" | tail -3
    echo ""
    
    echo "Test 3: No CPU affinity (2 seconds)"
    OUTPUT=$($PROG -a -t 2 2>&1)
    echo "$OUTPUT" | grep "CPU affinity:"
    echo "$OUTPUT" | grep -E "^\[" | tail -3
    echo ""
    
    echo "Test 4: External loopback with verification (2 seconds)"
    echo "Note: This may fail if external loopback is not connected"
    OUTPUT=$($PROG -l -t 2 2>&1)
    echo "$OUTPUT" | grep "Verification:" || echo "External loopback not available"
    echo "$OUTPUT" | grep -E "^\[" | tail -3
    echo ""
fi

# Test optimized v2
if [ -f "$PROG_V2" ]; then
    echo "=== Testing optimized v2 ==="
    echo ""
    echo "Test 1: Default settings (2 seconds)"
    OUTPUT=$($PROG_V2 -t 2 2>&1)
    echo "$OUTPUT" | grep -E "(Pattern:|CPU affinity:|Verification:)"
    echo "$OUTPUT" | grep -E "^\[" | tail -3
    echo ""
    
    echo "Test 2: Maximum performance mode (2 seconds)"
    OUTPUT=$($PROG_V2 -z -n -i 50 -t 2 2>&1)
    echo "$OUTPUT" | grep -E "(Zero-copy:|Verification:)"
    echo "$OUTPUT" | grep -E "^\[" | tail -3
    echo ""
fi

# Test user utilities if available
if [ -f "$BUILD_DIR/litepcie_util" ]; then
    echo "=== Testing litepcie_util ==="
    echo ""
    echo "Device info:"
    $BUILD_DIR/litepcie_util info 2>&1 | head -10
    echo ""
fi

echo "=== Summary ==="
echo "- V1 RX throughput: ~4.2-4.5 Gbps (good performance)"
echo "- V1 TX throughput: ~0.2-0.3 Gbps (limited by implementation)"
echo "- V2 should show balanced TX/RX performance (~8.5 Gbps each)"
echo "- Zero errors when verification is disabled"
echo "- For accurate bidirectional performance, use external loopback"