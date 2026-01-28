# Convenience Makefile wrapper for CMake build
# All actual build logic is in cmake/


BUILD_TYPE ?= Release

.PHONY: all clean configure build test help

all:
	@$(MAKE) clean
	@$(MAKE) build

configure:
	@mkdir -p ../build/generated/cmake
	@cd ../build/generated/cmake && cmake ../../../cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@# Help clangd: place compile_commands.json where VS Code clangd expects it
	@cd ../build/generated/cmake && test -f compile_commands.json && cp -f compile_commands.json ../compile_commands.json || true

build: configure
	@# Clean stale coverage data to prevent corrupt .gcda merge errors
	@find ../build -name "*.gcda" -delete 2>/dev/null || true
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

# Coverage build and report generation (requires lcov: brew install lcov)
# Does a full clean build to ensure gcno/gcda files are in sync
coverage:
	@rm -rf ../build
	@mkdir -p ../build/generated/cmake
	@cd ../build/generated/cmake && cmake ../../../cmake -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
	@cd ../build/generated/cmake && $(MAKE) -j8
	@cd ../build/generated/cmake && ctest --output-on-failure || true
	@echo "Generating coverage report..."
	@cd ../build/generated/cmake && lcov --capture --directory . --output-file coverage.info --ignore-errors mismatch,gcov,inconsistent,unsupported,format
	@cd ../build/generated/cmake && lcov --remove coverage.info '/usr/*' '*/googletest/*' '*/autogen/*' '*/Applications/*' --output-file coverage_filtered.info --ignore-errors unused,format,inconsistent
	@cd ../build/generated/cmake && genhtml coverage_filtered.info --output-directory ../../../build/coverage_html --ignore-errors inconsistent,inconsistent,format,format,category
	@# Copy lcov.info to project root for Coverage Gutters extension
	@cp ../build/generated/cmake/coverage_filtered.info ../lcov.info
	@echo ""
	@echo "Coverage report: build/coverage_html/index.html"
	@echo "Coverage Gutters: lcov.info (in project root)"

# Quick coverage regeneration without full rebuild (uses existing .gcno files)
# Use this after running tests to update coverage without rebuilding
coverage-regen:
	@echo "Regenerating coverage from existing test run..."
	@cd ../build/generated/cmake && lcov --capture --directory . --output-file coverage.info --ignore-errors mismatch,gcov,inconsistent,unsupported,format
	@cd ../build/generated/cmake && lcov --remove coverage.info '/usr/*' '*/googletest/*' '*/autogen/*' '*/Applications/*' --output-file coverage_filtered.info --ignore-errors unused,format,inconsistent
	@cd ../build/generated/cmake && genhtml coverage_filtered.info --output-directory ../../../build/coverage_html --ignore-errors inconsistent,inconsistent,format,format,category
	@cp ../build/generated/cmake/coverage_filtered.info ../lcov.info
	@echo ""
	@echo "Coverage report: build/coverage_html/index.html"
	@echo "Coverage Gutters: lcov.info (in project root)"

coverage-clean:
	@find ../build -name "*.gcda" -delete 2>/dev/null || true
	@find ../build -name "*.gcno" -delete 2>/dev/null || true
	@rm -rf ../build/coverage.info ../build/coverage_filtered.info ../build/coverage_html ../lcov.info
	@echo "Coverage data cleaned"

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
	@echo "Variables:"
	@echo "  BUILD_TYPE=<Release|Debug|RelWithDebInfo|MinSizeRel> (default: Release)"
	@echo ""
	@echo "Note: All CMake configuration is in this directory (cmake/)"
	@echo "      'make' or 'make all' always does a full clean rebuild"
