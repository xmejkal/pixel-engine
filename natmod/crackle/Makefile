# crackle_c.mpy (P747, P751): core/fx_crackle.c and fx_grains.c as a native module for the ESP32-S3 board, MicroPython v1.29.0.
# The .mk sets CROSS and PYTHON with =, so only the make line overrides them:
#   make ARCH=xtensawin CROSS=xtensa-esp32s3-elf- PYTHON=<a Python with pyelftools>
# tools/crackle.sh in pixel-games runs this line. No LINK_RUNTIME: a libgcc or libc call fails the link.
MPY_DIR ?= $(HOME)/esp/micropython-v1.29.0
MOD = crackle_c
SRC = crackle_mod.c ../../core/fx_crackle.c ../../core/fx_grains.c
ARCH ?= xtensawin
CFLAGS += -I../../core -fno-builtin
include $(MPY_DIR)/py/dynruntime.mk
