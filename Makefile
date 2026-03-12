# Root Makefile wrapper - delegates to cmake/Makefile
# Run this from the project root directory

.PHONY: all clean configure build test help install-vision-tool reinstall-vision-tool

%:
	@$(MAKE) -C cmake $@

all:
	@$(MAKE) -C cmake all

clean:
	@$(MAKE) -C cmake clean

configure:
	@$(MAKE) -C cmake configure

build:
	@$(MAKE) -C cmake build

test:
	@$(MAKE) -C cmake test

install-vision-tool:
	@cd tools/aioserver-vision-tool && npm run install:local

reinstall-vision-tool:
	@code --uninstall-extension local.aioserver-vision-tool >/dev/null 2>&1 || true
	@cd tools/aioserver-vision-tool && npm run install:local

coverage:
	@$(MAKE) -C cmake coverage

coverage-regen:
	@$(MAKE) -C cmake coverage-regen

coverage-clean:
	@$(MAKE) -C cmake coverage-clean

help:
	@$(MAKE) -C cmake help
