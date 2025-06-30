cmake_minimum_required(VERSION 3.10)
project(litepcie_dma_test_optimized C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(DEBUG "Enable debug build" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_KERNEL_MODULE "Build kernel modules (requires Linux kernel headers)" OFF)

# Find required packages
find_package(Threads REQUIRED)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -march=native -mtune=native")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -funroll-loops -fprefetch-loop-arrays")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fomit-frame-pointer -finline-functions")

# Release/Debug configurations
set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g -DDEBUG")

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/user
    ${CMAKE_CURRENT_SOURCE_DIR}/user/liblitepcie
)

# Library sources
set(LIBLITEPCIE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/user/liblitepcie/litepcie_dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/user/liblitepcie/litepcie_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/user/liblitepcie/litepcie_helpers.c
)

# Create liblitepcie static library
add_library(litepcie STATIC ${LIBLITEPCIE_SOURCES})
target_compile_options(litepcie PRIVATE -fPIC)

# Set output directory for object files to avoid path issues
set_target_properties(litepcie PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Executable targets - Optimized DMA tests
add_executable(litepcie_dma_test_optimized litepcie_dma_test_optimized.c)
add_executable(litepcie_dma_test_optimized_v2 litepcie_dma_test_optimized_v2.c)

# Executable targets - User utilities
add_executable(litepcie_util ${CMAKE_CURRENT_SOURCE_DIR}/user/litepcie_util.c)
add_executable(litepcie_test ${CMAKE_CURRENT_SOURCE_DIR}/user/litepcie_test.c)
add_executable(litepcie_latency_test ${CMAKE_CURRENT_SOURCE_DIR}/user/litepcie_latency_test.c)
add_executable(litepcie_latency_test_simple ${CMAKE_CURRENT_SOURCE_DIR}/user/litepcie_latency_test_simple.c)
add_executable(litepcie_latency_test_final ${CMAKE_CURRENT_SOURCE_DIR}/user/litepcie_latency_test_final.c)

# Link libraries - Optimized DMA tests
target_link_libraries(litepcie_dma_test_optimized 
    litepcie
    Threads::Threads
    rt
)

target_link_libraries(litepcie_dma_test_optimized_v2
    litepcie
    Threads::Threads
    rt
)

# Link libraries - User utilities
target_link_libraries(litepcie_util
    litepcie
)

target_link_libraries(litepcie_test
    litepcie
    m
)

target_link_libraries(litepcie_latency_test
    litepcie
    m
)

target_link_libraries(litepcie_latency_test_simple
    litepcie
    m
)

target_link_libraries(litepcie_latency_test_final
    litepcie
    m
)

# Installation
install(TARGETS 
    litepcie_dma_test_optimized 
    litepcie_dma_test_optimized_v2
    litepcie_util
    litepcie_test
    litepcie_latency_test
    litepcie_latency_test_simple
    litepcie_latency_test_final
    RUNTIME DESTINATION bin
)

# Custom targets
add_custom_target(benchmark
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/litepcie_dma_test_optimized -t 10
    COMMAND echo ""
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/litepcie_dma_test_optimized_v2 -t 10
    DEPENDS litepcie_dma_test_optimized litepcie_dma_test_optimized_v2
    COMMENT "Running benchmark tests"
)

# Profile build configuration
add_custom_target(profile
    COMMAND ${CMAKE_COMMAND} -DCMAKE_C_FLAGS="${CMAKE_C_FLAGS} -pg -fno-omit-frame-pointer" ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} --build .
    COMMENT "Building with profiling support"
)

# Kernel module building (Linux only)
if(BUILD_KERNEL_MODULE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Kernel modules can only be built on Linux")
    endif()
    
    # Find kernel build directory
    execute_process(
        COMMAND uname -r
        OUTPUT_VARIABLE KERNEL_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    set(KERNEL_BUILD_DIR "/lib/modules/${KERNEL_VERSION}/build")
    
    if(NOT EXISTS ${KERNEL_BUILD_DIR})
        message(FATAL_ERROR "Kernel build directory not found: ${KERNEL_BUILD_DIR}. Install kernel headers.")
    endif()
    
    # Add custom target for kernel modules
    add_custom_target(kernel_modules
        COMMAND ${CMAKE_MAKE_PROGRAM} -C ${KERNEL_BUILD_DIR} M=${CMAKE_CURRENT_SOURCE_DIR}/kernel modules
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/kernel
        COMMENT "Building kernel modules"
    )
    
    # Clean kernel modules
    add_custom_target(clean_kernel_modules
        COMMAND ${CMAKE_MAKE_PROGRAM} -C ${KERNEL_BUILD_DIR} M=${CMAKE_CURRENT_SOURCE_DIR}/kernel clean
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/kernel
        COMMENT "Cleaning kernel modules"
    )
    
    # Install kernel modules (requires root)
    add_custom_target(install_kernel_modules
        COMMAND ${CMAKE_COMMAND} -E echo "Installing kernel modules (requires root)..."
        COMMAND ${CMAKE_COMMAND} -E make_directory /lib/modules/${KERNEL_VERSION}/kernel/drivers/misc/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/kernel/litepcie.ko /lib/modules/${KERNEL_VERSION}/kernel/drivers/misc/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/kernel/liteuart.ko /lib/modules/${KERNEL_VERSION}/kernel/drivers/misc/
        COMMAND depmod -a
        DEPENDS kernel_modules
        COMMENT "Installing kernel modules to system"
    )
    
    # Load kernel modules
    add_custom_target(load_kernel_modules
        COMMAND modprobe litepcie
        COMMAND modprobe liteuart
        DEPENDS install_kernel_modules
        COMMENT "Loading kernel modules"
    )
    
    # Unload kernel modules
    add_custom_target(unload_kernel_modules
        COMMAND modprobe -r liteuart || true
        COMMAND modprobe -r litepcie || true
        COMMENT "Unloading kernel modules"
    )
endif()

# Print configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C flags: ${CMAKE_C_FLAGS}")
message(STATUS "C flags (Release): ${CMAKE_C_FLAGS_RELEASE}")
message(STATUS "C flags (Debug): ${CMAKE_C_FLAGS_DEBUG}")
message(STATUS "Build kernel module: ${BUILD_KERNEL_MODULE}")