#---------------------------------------------------------------------------------
# DTMF-Signer for the Nintendo DS / DS Lite: a custom ARM7 core and the ARM9
# firmware, which reuses core/ and app/ unchanged.
# After devkitPro's combined template ($(DEVKITPRO)/examples/nds/templates/combined),
# in one Makefile: each source is compiled to build/<its path in the
# repository>.o, so sources with the same name in different libraries never
# collide, and each library gets its own flags.
#
# Build inside the pinned image for a reproducible .nds (tools/build-nds.sh):
#   make -C ports/nds          -> ports/nds/dtmf-signer-ds.nds
#   make -C ports/nds AUTOTEST=1   development only (DS_NDS_AUTOTEST): never released
#
# No dswifi, no libfat, no maxmod: the signer never touches the network or
# a card's storage.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

TARGET     := dtmf-signer-ds
GAME_TITLE := DTMF-Signer
ROOT       := $(abspath $(CURDIR)/../..)
BUILD      := build

# The version the About page shows, as the CMake builds give it (app/):
# git describe. (git inside the builder container sees a repository owned
# by another user, hence safe.directory.)
GIT        := git -C $(ROOT) -c safe.directory='*'
DS_VERSION := $(shell $(GIT) describe --tags --always --dirty 2>/dev/null || echo unknown)

#---------------------------------------------------------------------------------
# Sources, relative to the repository's root
#---------------------------------------------------------------------------------
CORE_SRC := $(addprefix core/, ds_wipe.c ds_error.c ds_sha512.c ds_sha256.c \
	ds_address.c ds_ripemd160.c ds_bip39.c ds_bip32.c ds_wallet.c ds_tx.c \
	ds_psbt.c ds_sign.c ds_policy.c ds_session.c)
# app/ without the voice firmware's parts (Speex, the prompts, the keypad
# entry): the screen-and-buttons firmware does not use them.
APP_SRC  := $(addprefix app/, ds_qr.c ds_rx.c ds_ui.c ds_review.c ds_about.c \
	ds_btn.c ds_base64.c ds_crc32.c ds_app.c)
SECP_SRC := $(addprefix third_party/secp256k1/src/, secp256k1.c \
	precomputed_ecmult.c precomputed_ecmult_gen.c)
VEND_SRC := third_party/ur-tones/c/ur_tones.c third_party/qrcodegen/c/qrcodegen.c
PORT_SRC := ports/nds/nds_view.c $(addprefix ports/nds/arm9/, main.c mic.c secp_callbacks.c)
ARM7_SRC := ports/nds/arm7/main.c

LIB_SRC  := $(CORE_SRC) $(APP_SRC) $(SECP_SRC) $(VEND_SRC)
LIB_OBJ  := $(addprefix $(BUILD)/arm9/,$(LIB_SRC:.c=.o))
PORT_OBJ := $(addprefix $(BUILD)/arm9/,$(PORT_SRC:.c=.o))
ARM7_OBJ := $(addprefix $(BUILD)/arm7/,$(ARM7_SRC:.c=.o))

#---------------------------------------------------------------------------------
# Flags
#---------------------------------------------------------------------------------
# -ffile-prefix-map keeps this machine's paths out of the binary.
COMMON   := -O2 -ffunction-sections -fdata-sections -ffile-prefix-map=$(ROOT)=dtmf-signer
ARCH9    := -march=armv5te -mtune=arm946e-s
ARCH7    := -march=armv4t -mtune=arm7tdmi -mthumb
NDSINC   := -D__NDS__ -I$(CALICO)/include -I$(LIBNDS)/include
WARN     := -Wall -Wextra -Werror -Wno-format-truncation

INC_LIB  := -I$(ROOT)/core -I$(ROOT)/app -I$(ROOT)/third_party/secp256k1/include \
            -I$(ROOT)/third_party/ur-tones/c -I$(ROOT)/third_party/qrcodegen/c

# libsecp256k1 as the Pico builds it: what core/ uses (key derivation and
# ECDSA signing), small tables, no assembly, no optional modules, and the
# default callbacks from secp_callbacks.c.
SECP_DEFS := -DECMULT_WINDOW_SIZE=2 -DCOMB_BLOCKS=2 -DCOMB_TEETH=5 \
             -DUSE_EXTERNAL_DEFAULT_CALLBACKS=1 -DSECP256K1_NO_API_VISIBILITY_ATTRIBUTES

# What the About pages say about this build.
ABOUT    := -DDS_VERSION='"$(DS_VERSION)"' -DDS_HARDWARE='"Nintendo DS"' \
            -DDS_PROFILE='"Nintendo DS (air)"' \
            -DDS_CAPS='"TONES-SW IN-MIC IN-BUTTONS VERIFY-SCREEN OUT-QR BTC-INPUT-AMOUNTS"'

CFLAGS9_CORE := $(COMMON) $(ARCH9) -std=c99 $(WARN) $(INC_LIB) $(ABOUT)
CFLAGS9_VEND := $(COMMON) $(ARCH9) -std=c99 -w $(INC_LIB) $(SECP_DEFS)
CFLAGS9_PORT := $(COMMON) $(ARCH9) -std=gnu11 $(WARN) -DARM9 $(NDSINC) $(INC_LIB) \
                -I$(ROOT)/ports/nds -I$(ROOT)/ports/nds/arm9
CFLAGS7      := $(COMMON) $(ARCH7) -std=gnu11 -Wall -Wextra -Werror -DARM7 $(NDSINC)

# Development only: `make AUTOTEST=1`, for an end-to-end run in an emulator.
# Never used for a released .nds. (Flags are not tracked: make clean first.)
ifneq ($(strip $(AUTOTEST)),)
CFLAGS9_PORT += -DDS_NDS_AUTOTEST
endif

NDS_LIBDIRS := -L$(CALICO)/lib -L$(LIBNDS)/lib

#---------------------------------------------------------------------------------
.PHONY: all clean
all: $(TARGET).nds

$(TARGET).nds: $(BUILD)/arm7.elf $(BUILD)/arm9.elf
	ndstool -c $@ -7 $(BUILD)/arm7.elf -9 $(BUILD)/arm9.elf \
		-b $(CALICO)/share/nds-icon.bmp "$(GAME_TITLE); ; "
	@echo built ... $(notdir $@)

$(BUILD)/libdtmf.a: $(LIB_OBJ)
	@rm -f $@
	$(AR) rcsD $@ $^

$(BUILD)/arm9.elf: $(PORT_OBJ) $(BUILD)/libdtmf.a
	$(CC) $(NDS_LIBDIRS) -specs=$(CALICO)/share/ds9.specs $(ARCH9) -Wl,--gc-sections \
		-Wl,-Map,$(BUILD)/arm9.map $(PORT_OBJ) $(BUILD)/libdtmf.a -lnds9 -lcalico_ds9 -o $@

$(BUILD)/arm7.elf: $(ARM7_OBJ)
	$(CC) $(NDS_LIBDIRS) -specs=$(CALICO)/share/ds7.specs $(ARCH7) -Wl,--gc-sections \
		-Wl,-Map,$(BUILD)/arm7.map $(ARM7_OBJ) -lnds7 -lcalico_ds7 -o $@

# Our own code (core/, app/): C99 and warnings as errors, as on the host.
$(addprefix $(BUILD)/arm9/,$(CORE_SRC:.c=.o) $(APP_SRC:.c=.o)): $(BUILD)/arm9/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) -MMD -MP $(CFLAGS9_CORE) -c $< -o $@

# Vendored libraries, unmodified: without our warning flags.
$(addprefix $(BUILD)/arm9/,$(SECP_SRC:.c=.o) $(VEND_SRC:.c=.o)): $(BUILD)/arm9/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) -MMD -MP $(CFLAGS9_VEND) -c $< -o $@

# The port (libnds and calico).
$(PORT_OBJ): $(BUILD)/arm9/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) -MMD -MP $(CFLAGS9_PORT) -c $< -o $@

$(ARM7_OBJ): $(BUILD)/arm7/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) -MMD -MP $(CFLAGS7) -c $< -o $@

clean:
	rm -rf $(BUILD) $(TARGET).nds

-include $(LIB_OBJ:.o=.d) $(PORT_OBJ:.o=.d) $(ARM7_OBJ:.o=.d)
