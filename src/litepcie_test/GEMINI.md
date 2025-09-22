# Gemini Project: litepcie_test

This project is for testing and interacting with a LitePCIe FPGA device. It includes Python code for building the FPGA gateware and C code for testing the PCIe interface, with a focus on DMA performance.

## Project Overview

The project is divided into two main parts:

1.  **FPGA Gateware:** The FPGA gateware is built using the [LiteX](https://github.com/enjoy-digital/litex) framework. The SoC architecture is defined in `axau15_soc.py`, which includes a PCIe interface, DDR4 memory, and other peripherals. The main entry point for the build is `build.py`.

2.  **Host Software:** The host software is written in C and is located in the `handy_tests/ubuntu` directory. It includes a library (`liblitepcie`), a kernel module, and several test programs for evaluating DMA performance and latency.

## Building and Running

### FPGA Gateware

To build the FPGA gateware, run the following command:

```bash
python3 build.py
```

This will generate the bitstream file in the `build` directory.

### Host Software

The host software is built using CMake. The recommended way to build is to use the provided shell script:

```bash
cd handy_tests/ubuntu
./build.sh
```

This will create a `build` directory and compile all the test programs.

To build with debug symbols, use the `--debug` flag:

```bash
./build.sh --debug
```

### Kernel Module

The Linux kernel module can be built as part of the CMake build process. To enable the kernel module build, use the `--kernel` flag:

```bash
./build.sh --kernel
```

Once the module is built, you can load it using `modprobe`:

```bash
sudo modprobe litepcie
```

## Development Conventions

*   The Python code uses the LiteX framework for hardware description.
*   The C code uses C11 and is built with `-Wall -Wextra` flags.
*   The project uses `CMake` for building the C code and `make` for some utility tasks.
*   There is a focus on performance optimization, including the use of compiler intrinsics, cache-line aligned buffers, and CPU affinity.
