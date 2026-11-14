.DEFAULT_GOAL := all
.PHONY: all run run-installed setup-build-gcc run-setup-gcc test
all:
	$(MAKE) -C .. setup-build
run:
	$(MAKE) -C .. run-setup
run-installed:
	$(MAKE) -C .. run-installed
setup-build-gcc:
	$(MAKE) -C .. setup-build-gcc
run-setup-gcc:
	$(MAKE) -C .. run-setup-gcc
test:
	$(MAKE) -C .. test-setup
