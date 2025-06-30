#!/usr/bin/env python3
"""
Performance comparison script for LitePCIe DMA test
Compares original vs optimized implementation
"""

import subprocess
import re
import time
import statistics
import sys

def run_dma_test(command, duration=10):
    """Run a DMA test and extract performance metrics"""
    print(f"Running: {' '.join(command)}")
    
    try:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(duration + 1)  # Let it run for specified duration
        process.terminate()
        stdout, stderr = process.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate()
    except Exception as e:
        print(f"Error running command: {e}")
        return None
    
    # Parse the output for performance metrics
    tx_speeds = []
    rx_speeds = []
    
    for line in stdout.split('\n'):
        # Look for lines with TX/RX speeds
        match = re.search(r'TX:\s+(\d+\.?\d*)\s+Gbps.*RX:\s+(\d+\.?\d*)\s+Gbps', line)
        if match:
            tx_speeds.append(float(match.group(1)))
            rx_speeds.append(float(match.group(2)))
    
    if tx_speeds and rx_speeds:
        return {
            'tx_avg': statistics.mean(tx_speeds),
            'tx_max': max(tx_speeds),
            'rx_avg': statistics.mean(rx_speeds),
            'rx_max': max(rx_speeds),
            'samples': len(tx_speeds)
        }
    return None

def main():
    print("=== LitePCIe DMA Performance Comparison ===\n")
    
    # Check if device exists
    try:
        subprocess.run(['ls', '/dev/litepcie0'], check=True, capture_output=True)
    except:
        print("Error: /dev/litepcie0 not found!")
        sys.exit(1)
    
    # Test configurations
    tests = [
        {
            'name': 'Original Implementation',
            'command': ['litepcie_util', 'dma_test'],
            'duration': 10
        },
        {
            'name': 'Optimized - Default',
            'command': ['./litepcie_dma_test_optimized', '-t', '10'],
            'duration': 10
        },
        {
            'name': 'Optimized - Zero Copy',
            'command': ['./litepcie_dma_test_optimized', '-z', '-t', '10'],
            'duration': 10
        },
        {
            'name': 'Optimized - No Verification',
            'command': ['./litepcie_dma_test_optimized', '-n', '-t', '10'],
            'duration': 10
        },
        {
            'name': 'Optimized - No CPU Affinity',
            'command': ['./litepcie_dma_test_optimized', '-a', '-t', '10'],
            'duration': 10
        }
    ]
    
    results = []
    
    for test in tests:
        print(f"\nRunning: {test['name']}")
        print("-" * 40)
        
        result = run_dma_test(test['command'], test['duration'])
        
        if result:
            results.append((test['name'], result))
            print(f"TX Average: {result['tx_avg']:.3f} Gbps")
            print(f"TX Maximum: {result['tx_max']:.3f} Gbps")
            print(f"RX Average: {result['rx_avg']:.3f} Gbps")
            print(f"RX Maximum: {result['rx_max']:.3f} Gbps")
            print(f"Samples: {result['samples']}")
        else:
            print("Failed to get results")
        
        time.sleep(2)  # Brief pause between tests
    
    # Summary
    print("\n=== Performance Summary ===")
    print("-" * 60)
    print(f"{'Test':<30} {'TX Avg':<10} {'RX Avg':<10} {'Combined':<10}")
    print("-" * 60)
    
    if results:
        baseline_tx = results[0][1]['tx_avg'] if results else 1
        baseline_rx = results[0][1]['rx_avg'] if results else 1
        
        for name, result in results:
            combined = result['tx_avg'] + result['rx_avg']
            if name == 'Original Implementation':
                print(f"{name:<30} {result['tx_avg']:>7.3f} Gbps {result['rx_avg']:>7.3f} Gbps {combined:>7.3f} Gbps")
            else:
                tx_improve = ((result['tx_avg'] / baseline_tx) - 1) * 100
                rx_improve = ((result['rx_avg'] / baseline_rx) - 1) * 100
                print(f"{name:<30} {result['tx_avg']:>7.3f} Gbps {result['rx_avg']:>7.3f} Gbps {combined:>7.3f} Gbps")
                print(f"{'':30} ({tx_improve:+.1f}%)     ({rx_improve:+.1f}%)")
    
    print("\nNote: Results may vary based on system load and hardware configuration.")

if __name__ == "__main__":
    main()