# Root Makefile wrapper - delegates to cmake/Makefile
# Run this from the project root directory

.PHONY: all clean configure build test help

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

coverage:
	@$(MAKE) -C cmake coverage

coverage-regen:
	@$(MAKE) -C cmake coverage-regen

coverage-clean:
	@$(MAKE) -C cmake coverage-clean

help:
	@$(MAKE) -C cmake help
