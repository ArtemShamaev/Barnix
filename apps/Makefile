# Convenience wrapper for application authors working inside apps/.
# Usage: make calc.elf

.PHONY: all clean

%.elf: %.c
	$(MAKE) -C .. apps/$@

all:
	$(MAKE) -C .. apps

clean:
	$(MAKE) -C .. clean
