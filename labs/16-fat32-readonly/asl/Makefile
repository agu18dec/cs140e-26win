# ASL inference subproject
# Standalone — zero changes needed to ../code/Makefile
# References lab 16 fat32/mbr/pi-sd sources by relative path.

CODE = ../code

COMMON_SRC += $(CODE)/fat32.c
COMMON_SRC += $(CODE)/mbr.c
COMMON_SRC += $(CODE)/pi-sd.c
COMMON_SRC += $(CODE)/mbr-helpers.c
COMMON_SRC += $(CODE)/fat32-helpers.c
COMMON_SRC += $(CODE)/fat32-lfn-helpers.c
COMMON_SRC += $(CODE)/external-code/unicode-utf8.c
COMMON_SRC += $(CODE)/external-code/emmc.c
INFERENCE ?= q10
ifeq ($(INFERENCE),float)
  COMMON_SRC += asl-cnn-float.c
  CFLAGS_EXTRA += -DASL_USE_FLOAT
else
  COMMON_SRC += asl-cnn.c
endif

ifeq ($(TRAIN),1)
  COMMON_SRC += asl-train.c
  CFLAGS_EXTRA += -DASL_USE_FLOAT
  ifeq ($(RANDOM_INIT),1)
    CFLAGS_EXTRA += -DRANDOM_INIT=1
  endif
endif

CFLAGS_EXTRA += -I$(CODE) -I$(CODE)/external-code

O := $(CS140E_2026_PATH)/libpi
STAFF_OBJS += $(O)/staff-objs/staff-kmalloc.o
STAFF_OBJS += $(O)/staff-objs/gpio.o

PROGS      := asl-golden.c
ifeq ($(TRAIN),1)
  PROGS += asl-train-golden.c
endif
ifeq ($(OVERFIT),1)
  COMMON_SRC += asl-train.c
  CFLAGS_EXTRA += -DASL_USE_FLOAT
  PROGS += asl-overfit.c
endif
ifeq ($(OVERFIT20),1)
  COMMON_SRC += asl-train.c
  CFLAGS_EXTRA += -DASL_USE_FLOAT
  PROGS += asl-overfit20.c
endif
TTYUSB     =
BOOTLOADER = my-install
RUN        = 1

include $(CS140E_2026_PATH)/libpi/mk/Makefile.robust-v3

# Convenience: copy compiled binary + model files to SD card (macOS)
SD ?= /Volumes/BOOT
install-sd: all
	cp asl-golden.bin $(SD)/kernel7.img
	cp model/asl_weights_q15.bin $(SD)/MODEL.BIN
	@echo "Eject SD and boot Pi"
