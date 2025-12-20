# Convenience Makefile wrapper for CMake build
# All actual build logic is in cmake/


.PHONY: all clean configure build test help

all:
	@$(MAKE) clean
	@$(MAKE) build

configure:
	@mkdir -p ../build/generated/cmake
	@cd ../build/generated/cmake && cmake ../../../cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@# Help clangd: place compile_commands.json where VS Code clangd expects it
	@cd ../build/generated/cmake && test -f compile_commands.json && cp -f compile_commands.json ../compile_commands.json || true

build: configure
	@cd ../build/generated/cmake && $(MAKE) -j8
	@# Remove empty bin/lib folders created by CMake in generated/cmake
	@rmdir ../build/generated/cmake/bin 2>/dev/null || true
	@rmdir ../build/generated/cmake/lib 2>/dev/null || true
	@echo ""
	@echo "Build complete!"
	@echo "Executables: build/bin/"
	@echo "Libraries:   build/lib/"
	@echo "Generated:   build/generated/"

clean:
	@rm -rf ../build
	@echo "Build directory cleaned"

test: build
	@cd ../build/generated/cmake && ctest --output-on-failure

help:
	@echo "AIO Server Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all (default) - Clean and build the project from scratch"
	@echo "  configure     - Run CMake configuration"
	@echo "  build         - Configure and build all targets"
	@echo "  clean         - Remove build directory"
	@echo "  test          - Run all tests"
	@echo "  help          - Show this help message"
	@echo ""
	@echo "Note: All CMake configuration is in this directory (cmake/)"
	@echo "      'make' or 'make all' always does a full clean rebuild"
