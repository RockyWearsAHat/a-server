# Convenience Makefile wrapper for CMake build
# All actual build logic is in cmake/


BUILD_TYPE ?= Release

BUILD_DIR := ../build/generated/cmake
COVERAGE_BUILD_DIR := ../build/coverage_cmake
# Sentinel file — only re-configure when CMake files are newer than this
CMAKE_SENTINEL := $(BUILD_DIR)/.cmake_configured
# All cmake input files that, if changed, require a reconfigure
CMAKE_INPUTS := CMakeLists.txt core.cmake tests.cmake $(wildcard qt/*.cmake)

# Point ccache to the project config file (sloppiness, max_size, compiler_check=content)
# This works regardless of XDG_CONFIG_HOME or shell environment
export CCACHE_CONFIGPATH := $(HOME)/.config/ccache/ccache.conf

.PHONY: all clean configure reconfigure build test help

all:
	@$(MAKE) clean
	@$(MAKE) build

# Configure only when CMakeLists.txt or cmake/*.cmake files are newer than the sentinel.
# Touch the sentinel after a successful configure so subsequent builds skip this.
$(CMAKE_SENTINEL): $(CMAKE_INPUTS)
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake ../../../cmake \
		-G Ninja \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@cd $(BUILD_DIR) && test -f compile_commands.json && cp -f compile_commands.json ../compile_commands.json || true
	@touch $(CMAKE_SENTINEL)

configure: $(CMAKE_SENTINEL)

# Force a full cmake re-run regardless of sentinel state
reconfigure:
	@rm -f $(CMAKE_SENTINEL)
	@$(MAKE) configure

build: $(CMAKE_SENTINEL)
	@# Only build the main AIOServer binary — not the full test suite.
	@# Use 'make test' to build and run tests.
	@find ../build -name "*.gcda" -delete 2>/dev/null || true
	@cd $(BUILD_DIR) && ninja AIOServer
	@echo ""
	@echo "Build complete!"
	@echo "Executables: build/bin/"
	@echo "Libraries:   build/lib/"
	@echo "Generated:   build/generated/"

clean:
	@rm -rf ../build
	@echo "Build directory cleaned (run 'make reconfigure' or 'make build' to re-configure)"

test: $(CMAKE_SENTINEL)
	@# Build all targets (including test binaries) then run tests.
	@find ../build -name "*.gcda" -delete 2>/dev/null || true
	@cd $(BUILD_DIR) && ninja
	@cd $(BUILD_DIR) && ctest --output-on-failure

# Coverage build and report generation (requires lcov: brew install lcov)
# Uses a dedicated binary directory so coverage can coexist with the normal build.
coverage:
	@rm -rf $(COVERAGE_BUILD_DIR)
	@mkdir -p $(COVERAGE_BUILD_DIR)
	@cd $(COVERAGE_BUILD_DIR) && cmake ../../cmake -G Ninja -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
	@cd $(COVERAGE_BUILD_DIR) && ninja
	@cd $(COVERAGE_BUILD_DIR) && ctest --output-on-failure || true
	@echo "Generating coverage report..."
	@cd $(COVERAGE_BUILD_DIR) && lcov --rc lcov_excl_line='LCOV_EXCL_LINE|Logger::Instance|AIO::Emulator::Common::Logger' --capture --directory . --output-file coverage.info --ignore-errors mismatch,gcov,inconsistent,unsupported,format
	@cd $(COVERAGE_BUILD_DIR) && lcov --remove coverage.info '/usr/*' '*/googletest/*' '*/autogen/*' '*/Applications/*' --output-file coverage_filtered.info --ignore-errors unused,format,inconsistent
	@cd $(COVERAGE_BUILD_DIR) && genhtml coverage_filtered.info --output-directory ../coverage_html --ignore-errors inconsistent,inconsistent,format,format,category
	@# Copy lcov.info to project root for Coverage Gutters extension
	@cp $(COVERAGE_BUILD_DIR)/coverage_filtered.info ../lcov.info
	@echo ""
	@echo "Coverage report: build/coverage_html/index.html"
	@echo "Coverage Gutters: lcov.info (in project root)"

# Quick coverage regeneration without full rebuild (uses existing coverage .gcno/.gcda files)
coverage-regen:
	@echo "Regenerating coverage from existing test run..."
	@cd $(COVERAGE_BUILD_DIR) && lcov --rc lcov_excl_line='LCOV_EXCL_LINE|Logger::Instance|AIO::Emulator::Common::Logger' --capture --directory . --output-file coverage.info --ignore-errors mismatch,gcov,inconsistent,unsupported,format
	@cd $(COVERAGE_BUILD_DIR) && lcov --remove coverage.info '/usr/*' '*/googletest/*' '*/autogen/*' '*/Applications/*' --output-file coverage_filtered.info --ignore-errors unused,format,inconsistent
	@cd $(COVERAGE_BUILD_DIR) && genhtml coverage_filtered.info --output-directory ../coverage_html --ignore-errors inconsistent,inconsistent,format,format,category
	@cp $(COVERAGE_BUILD_DIR)/coverage_filtered.info ../lcov.info
	@echo ""
	@echo "Coverage report: build/coverage_html/index.html"
	@echo "Coverage Gutters: lcov.info (in project root)"

coverage-clean:
	@find $(COVERAGE_BUILD_DIR) -name "*.gcda" -delete 2>/dev/null || true
	@find $(COVERAGE_BUILD_DIR) -name "*.gcno" -delete 2>/dev/null || true
	@rm -rf $(COVERAGE_BUILD_DIR) ../build/coverage_html ../lcov.info
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
