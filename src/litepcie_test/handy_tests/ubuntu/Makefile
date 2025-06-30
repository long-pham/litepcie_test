# Combined Makefile for LitePCIe DMA test optimized versions
CC = gcc
AR = ar
CFLAGS = -O3 -Wall -Wextra -march=native -mtune=native -pthread
CFLAGS += -I. -I./kernel -I./user -I./user/liblitepcie
LDFLAGS = -pthread -lrt

# Library path
LIBDIR = .
LDFLAGS += -L$(LIBDIR) -llitepcie

# Performance flags
CFLAGS += -funroll-loops -fprefetch-loop-arrays
CFLAGS += -fomit-frame-pointer -finline-functions

# Debug build
DEBUG ?= 0
ifeq ($(DEBUG),1)
    CFLAGS += -g -DDEBUG
    CFLAGS := $(filter-out -O3,$(CFLAGS))
    CFLAGS += -O0
endif

# Targets
TARGET1 = litepcie_dma_test_optimized
TARGET2 = litepcie_dma_test_optimized_v2
TARGETS = $(TARGET1) $(TARGET2)

# Source files
SRCS1 = litepcie_dma_test_optimized.c
SRCS2 = litepcie_dma_test_optimized_v2.c
OBJS1 = $(SRCS1:.c=.o)
OBJS2 = $(SRCS2:.c=.o)

# Library files
LIBLITEPCIE = $(LIBDIR)/liblitepcie.a
LIBSRCDIR = ./user/liblitepcie
LIBSRCS = $(LIBSRCDIR)/litepcie_dma.c $(LIBSRCDIR)/litepcie_flash.c $(LIBSRCDIR)/litepcie_helpers.c
LIBOBJS = litepcie_dma.o litepcie_flash.o litepcie_helpers.o

.PHONY: all clean install check-lib liblitepcie benchmark profile

# Default target builds both executables
all: check-lib $(TARGETS)

# Check if liblitepcie exists and build if necessary
check-lib:
	@if [ ! -f "$(LIBLITEPCIE)" ]; then \
		echo "Building liblitepcie..."; \
		$(MAKE) liblitepcie; \
	fi

# Build liblitepcie
liblitepcie: $(LIBLITEPCIE)

$(LIBLITEPCIE): $(LIBOBJS)
	$(AR) rcs $@ $(LIBOBJS)

litepcie_dma.o: $(LIBSRCDIR)/litepcie_dma.c
	$(CC) $(CFLAGS) -c $< -o $@

litepcie_flash.o: $(LIBSRCDIR)/litepcie_flash.c
	$(CC) $(CFLAGS) -c $< -o $@

litepcie_helpers.o: $(LIBSRCDIR)/litepcie_helpers.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build targets
$(TARGET1): $(OBJS1) $(LIBLITEPCIE)
	$(CC) $(OBJS1) -o $@ $(LDFLAGS)

$(TARGET2): $(OBJS2) $(LIBLITEPCIE)
	$(CC) $(OBJS2) -o $@ $(LDFLAGS)

# Pattern rule for object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Install both targets
install: $(TARGETS)
	install -m 755 $(TARGETS) /usr/local/bin/

# Clean all generated files
clean:
	rm -f $(TARGETS) $(OBJS1) $(OBJS2) $(LIBOBJS) $(LIBLITEPCIE)

# Run performance comparison
benchmark: all
	@echo "Running optimized implementation v1:"
	./$(TARGET1) -t 10
	@echo ""
	@echo "Running optimized implementation v2:"
	./$(TARGET2) -t 10

# Profile build (requires perf)
profile: CFLAGS += -pg -fno-omit-frame-pointer
profile: clean all
	@echo "Built with profiling support. Run with:"
	@echo "  ./$(TARGET1) [options]"
	@echo "  ./$(TARGET2) [options]"
	@echo "Then analyze with:"
	@echo "  gprof $(TARGET1) gmon.out > profile1.txt"
	@echo "  gprof $(TARGET2) gmon.out > profile2.txt"