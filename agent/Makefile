# Builds the agent's core and its tests with the QNX SDP (baseline SDP-BL-006; run `python sdp/check_sdp.py` first).
# Needs QNX_HOST and QNX_TARGET set, as the SDP's qnxsdp-env script sets them. Output goes to build/<arch>/.
#
#   make                 core tests for aarch64le and x86_64
#   make ARCH=aarch64le  one target only
#
# The tests run on a QNX target: copy build/aarch64le/core_test to the Pi and run it.

QCC ?= qcc
ARCH ?= aarch64le x86_64
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -Icore
CORE = core/on_core.c core/on_des.c

all: $(foreach a,$(ARCH),build/$(a)/core_test build/$(a)/opticnerve)

build/%/core_test: tests/core_test.c $(CORE) core/on_core.h core/on_des.h
	mkdir -p $(dir $@)
	$(QCC) -Vgcc_nto$* $(CFLAGS) -o $@ tests/core_test.c $(CORE)

build/%/opticnerve: qnx/main.c qnx/serve.c qnx/device.c qnx/cli.c qnx/device.h qnx/on_dev.h $(CORE) core/on_core.h core/on_des.h
	mkdir -p $(dir $@)
	$(QCC) -Vgcc_nto$* $(CFLAGS) -o $@ qnx/main.c qnx/serve.c qnx/device.c qnx/cli.c $(CORE) -lscreen -lsocket

clean:
	rm -rf build

.PHONY: all clean
