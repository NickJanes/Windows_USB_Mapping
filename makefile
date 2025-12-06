# Makefile for USB Topology Mapper DLL

# Compiler and flags
CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -shared
LIBS = -lsetupapi

# Output
TARGET = usb_mapper.dll
SRC = usb_mapper.c

# Build the DLL
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC) $(LIBS)
	@echo "Build complete: $(TARGET)"
	@echo "DLL architecture:"
	@file $(TARGET) 2>/dev/null || echo "(install 'file' command for details)"

# Clean build artifacts
clean:
	rm -f $(TARGET) *.o
	@echo "Cleaned build artifacts"

# Test with Python
test: $(TARGET)
	python usb_topology.py

# Check dependencies
check:
	@echo "Checking build environment..."
	@echo "GCC version:"
	@$(CC) --version | head -n 1
	@echo ""
	@echo "Python version:"
	@python --version
	@echo ""
	@echo "Python architecture:"
	@python -c "import ctypes; print(f'{ctypes.sizeof(ctypes.c_voidp) * 8}-bit')"
	@echo ""
	@echo "Looking for setupapi library..."
	@gcc -lsetupapi -xc -E - < /dev/null 2>&1 | grep -q "cannot find" && echo "WARNING: setupapi not found!" || echo "setupapi: OK"

# Install runtime dependencies if needed
install-deps:
	@echo "Copying MinGW runtime DLLs (if needed)..."
	@for dll in libgcc_s_seh-1.dll libwinpthread-1.dll libstdc++-6.dll; do \
		if [ -f /mingw64/bin/$$dll ]; then \
			cp /mingw64/bin/$$dll . 2>/dev/null || true; \
			echo "Copied $$dll"; \
		fi; \
	done

.PHONY: all clean test check install-deps