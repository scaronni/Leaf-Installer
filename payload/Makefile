#------------------------------------------------------------------------------
# Leaf Updater payload — runs on the Tegra X1 BPMP after Hekate chainloads it.
#
# The payload is the SD-side "finalize" step for Leaf-Installer's offline-
# update flow: while HOS is running, Leaf stages CFW components under
# sdmc:/leaf-offline-update/. After a reboot, hekate auto-boots this payload
# (via the [Leaf Update] entry in hekate_ipl.ini) and we move the staged
# tree into place at the SD root, then chain back to hekate.
#
# Build requires devkitARM (BPMP is ARMv4T / ARM7TDMI).
#------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error Please set DEVKITARM in your environment. export DEVKITARM=<path to>/devkitARM)
endif

include $(DEVKITARM)/base_rules

TARGET        := leaf-updater
# Hekate's _launch_payload() stages the .bin in DRAM at EXT_PAYLOAD_ADDR
# (0xC0000000), prepends a relocator stub, and arranges for the actual
# payload bytes to end up running from IRAM at PATCHED_RELOC_ENTRY
# (0x40010000) — that's the address we have to link against, not the
# staging DRAM address.
LDR_LOAD_ADDR := 0x40010000
BL_MAGIC      := 0x4655414C # "LAUF" — Leaf Auto-Update Finalizer.

SOURCEDIR     := source
BUILDDIR      := build
OUTPUTDIR     := output
# NOTE: the include/hekate submodule is intentionally pinned to v6.5.2 (see
# .gitmodules). hekate v6.5.3 rewrote bdk/storage/sdmmc.c's SD UHS/1.8V init
# path; the payload's sd_mount() then hangs at runtime (the rewritten
# low-voltage / SD_VOLTAGE_SWITCH sequence doesn't complete under this
# payload's minimal hw_init). The payload needs none of 6.5.3's SD changes.
# Do NOT bump past v6.5.2 without re-testing SD mount on real hardware.
# See main.c::main() near sd_mount().
BDKDIR        := ../include/hekate/bdk

# Curated subset of BDK modules — just enough to bring up clocks, display, GFX,
# read buttons, sleep, and reboot. SD + FATFS modules join in round 3.
BDK_SUBDIRS := soc mem display power rtc utils sec storage libs/fatfs

VPATH := $(SOURCEDIR) $(foreach d,$(BDK_SUBDIRS),$(BDKDIR)/$(d))

# Pull in every .c the curated subdirs ship — let --gc-sections drop the
# functions we don't actually call, rather than maintaining a hand-pruned list
# that drifts every time hekate refactors.
BDK_C_SRCS := $(foreach d,$(BDK_SUBDIRS),$(wildcard $(BDKDIR)/$(d)/*.c))
USER_C_SRCS := $(wildcard $(SOURCEDIR)/*.c)
USER_S_SRCS := $(wildcard $(SOURCEDIR)/*.S)

OBJS := $(addprefix $(BUILDDIR)/,$(notdir $(BDK_C_SRCS:.c=.o))) \
        $(addprefix $(BUILDDIR)/,$(notdir $(USER_C_SRCS:.c=.o))) \
        $(addprefix $(BUILDDIR)/,$(notdir $(USER_S_SRCS:.S=.o)))

GFX_INC   := '"../$(SOURCEDIR)/gfx.h"'

FFCFG_INC := '"../$(SOURCEDIR)/ffconf.h"'

DEFINES := -DBL_MAGIC=$(BL_MAGIC) \
           -DBL_VER_MJ=0 -DBL_VER_MN=1 -DBL_VER_HF=0 -DBL_VER_RL=0 \
           -DGFX_INC=$(GFX_INC) -DFFCFG_INC=$(FFCFG_INC) \
           -DBDK_MALLOC_NO_DEFRAG

# Build C as ARM (not Thumb). Our start.S is .arm and calls `BL main`, which
# is a 24-bit ARM-mode branch with no mode switch — ARMv4T has no BLX label
# form, so if main were Thumb-compiled the CPU would jump into Thumb code
# while still in ARM state and silently fault. -mthumb-interwork stays so
# any explicit BX/BLX targets still work.
ARCH    := -march=armv4t -mtune=arm7tdmi -mthumb-interwork
WARN    := -Wall -Wsign-compare -Wno-array-bounds -Wno-stringop-overread -Wno-stringop-overflow
INCLUDES := -I$(BDKDIR) -I$(SOURCEDIR)
CFLAGS  := $(ARCH) $(WARN) $(INCLUDES) $(DEFINES) \
           -O2 -g -nostdlib -ffunction-sections -fdata-sections \
           -fomit-frame-pointer -fno-inline -std=gnu11
ASFLAGS := $(ARCH) -g
LDFLAGS := $(ARCH) -nostartfiles -lgcc -Wl,--nmagic,--gc-sections \
           -Wl,--defsym=LDR_LOAD_ADDR=$(LDR_LOAD_ADDR) \
           -Wl,-Map,$(BUILDDIR)/$(TARGET).map

.PHONY: all clean

all: $(OUTPUTDIR)/$(TARGET).bin

$(OUTPUTDIR)/$(TARGET).bin: $(BUILDDIR)/$(TARGET).elf | $(OUTPUTDIR)
	$(OBJCOPY) -S -O binary $< $@
	@printf '%s built — %s bytes\n' "$@" "$$(wc -c < $@)"

$(BUILDDIR)/$(TARGET).elf: $(OBJS) link.ld | $(BUILDDIR)
	$(CC) $(LDFLAGS) -T link.ld $(OBJS) -o $@

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: %.S | $(BUILDDIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILDDIR) $(OUTPUTDIR):
	@mkdir -p $@

clean:
	@rm -rf $(BUILDDIR) $(OUTPUTDIR)
