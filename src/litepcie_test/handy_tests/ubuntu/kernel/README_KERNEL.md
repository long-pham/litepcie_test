# LitePCIe Kernel Modules

This directory contains the LitePCIe kernel modules:
- `litepcie.ko` - Main PCIe driver for LitePCIe devices
- `liteuart.ko` - UART driver for LiteX UART peripherals

## Building

### Using CMake (from parent directory)

```bash
# Enable kernel module building
cd ..
./build.sh --kernel

# Build the kernel modules
cd build
make kernel_modules

# Install modules (requires root)
sudo make install_kernel_modules
```

### Using Make (standalone)

```bash
# Build modules
make

# Clean
make clean
```

## Manual Installation

```bash
# Copy modules to system directory
sudo cp litepcie.ko /lib/modules/$(uname -r)/kernel/drivers/misc/
sudo cp liteuart.ko /lib/modules/$(uname -r)/kernel/drivers/misc/

# Update module dependencies
sudo depmod -a

# Load modules
sudo modprobe litepcie
sudo modprobe liteuart

# Verify modules loaded
lsmod | grep lite
```

## Usage

### Loading Modules

```bash
# Load with default parameters
sudo modprobe litepcie

# Or manually with insmod
sudo insmod litepcie.ko
```

### Checking Module Status

```bash
# Check if loaded
lsmod | grep litepcie

# Check kernel messages
dmesg | grep litepcie

# Check device files created
ls -la /dev/litepcie*
```

### Unloading Modules

```bash
# Unload modules
sudo modprobe -r litepcie
sudo modprobe -r liteuart

# Or with rmmod
sudo rmmod litepcie
sudo rmmod liteuart
```

## Troubleshooting

### Module Build Fails

```bash
# Install kernel headers
sudo apt-get install linux-headers-$(uname -r)

# Check kernel version matches
uname -r
```

### Module Won't Load

```bash
# Check for errors
dmesg | tail -20

# Check module info
modinfo litepcie.ko

# Force load (use with caution)
sudo insmod litepcie.ko
```

### Device Not Created

```bash
# Check for PCIe device
lspci -d 10ee:

# Check module parameters
cat /sys/module/litepcie/parameters/*
```

## Development

### Module Parameters

The litepcie module supports various parameters that can be set during load:

```bash
# Example with parameters (if supported)
sudo modprobe litepcie param1=value param2=value
```

### Debugging

Enable debug messages:

```bash
# Enable dynamic debug (if CONFIG_DYNAMIC_DEBUG is enabled)
echo 'module litepcie +p' | sudo tee /sys/kernel/debug/dynamic_debug/control

# Or rebuild with debug flags
make clean
make CFLAGS_MODULE="-DDEBUG"
```

## Security Note

Kernel modules run with full kernel privileges. Only load modules from trusted sources and ensure they are properly tested before deployment.