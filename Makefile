# Makefile - Frontend to CMake build system
#
# This Makefile provides a simple interface to the CMake build system.
# All actual build logic is in CMakeLists.txt.

BUILD_DIR ?= build
CMAKE ?= cmake
CMAKE_BUILD_TYPE ?= Release

# Default target
all: $(BUILD_DIR)/Makefile
	$(CMAKE) --build $(BUILD_DIR)

# Configure CMake
$(BUILD_DIR)/Makefile:
	$(CMAKE) -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

# Build targets
linenoise-example: $(BUILD_DIR)/Makefile
	$(CMAKE) --build $(BUILD_DIR) --target linenoise-example

linenoise-test: $(BUILD_DIR)/Makefile
	$(CMAKE) --build $(BUILD_DIR) --target linenoise-test

# Run tests (requires both test binary and example binary)
test: $(BUILD_DIR)/Makefile
	$(CMAKE) --build $(BUILD_DIR) --target linenoise-example
	$(CMAKE) --build $(BUILD_DIR) --target linenoise-test
	cd $(BUILD_DIR) && ctest --output-on-failure

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

# Rebuild from scratch
rebuild: clean all

# Debug build
debug:
	$(CMAKE) -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(BUILD_DIR)

# Install (requires sudo for system directories)
install: $(BUILD_DIR)/Makefile
	$(CMAKE) --build $(BUILD_DIR)
	$(CMAKE) --install $(BUILD_DIR)

# Show CMake configuration
config:
	@echo "Build directory: $(BUILD_DIR)"
	@echo "CMake: $(CMAKE)"
	@echo "Build type: $(CMAKE_BUILD_TYPE)"
	@if [ -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo ""; \
		echo "Current CMake configuration:"; \
		grep -E "^(CMAKE_BUILD_TYPE|BUILD_EXAMPLES|BUILD_TESTS|BUILD_SHARED_LIBS)" $(BUILD_DIR)/CMakeCache.txt 2>/dev/null || true; \
	fi

# Help
help:
	@echo "Linenoise Makefile (CMake frontend)"
	@echo ""
	@echo "Targets:"
	@echo "  all              Build everything (default)"
	@echo "  test             Build and run tests"
	@echo "  clean            Remove build directory"
	@echo "  rebuild          Clean and rebuild"
	@echo "  debug            Build with debug symbols"
	@echo "  install          Install to system"
	@echo "  config           Show build configuration"
	@echo "  help             Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  BUILD_DIR        Build directory (default: build)"
	@echo "  CMAKE            CMake executable (default: cmake)"
	@echo "  CMAKE_BUILD_TYPE Build type (default: Release)"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build everything"
	@echo "  make test               # Run tests"
	@echo "  make BUILD_DIR=debug debug"
	@echo "  make CMAKE_BUILD_TYPE=Debug"

.PHONY: all test clean rebuild debug install config help linenoise-example linenoise-test
