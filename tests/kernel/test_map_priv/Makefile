MDEF_FILE = prj.mdef
KERNEL_TYPE = micro
BOARD ?= qemu_x86
CONF_FILE = prj.conf
SOURCE_DIR := $(ZEPHYR_BASE)/tests/kernel/test_map/src

# Enable testing for private microkernel memory map objects
CFLAGS = -DTEST_PRIV_MEM_MAPS

include ${ZEPHYR_BASE}/Makefile.inc
