# DS45PAD - PS3 VSH plugin build
#
# Requires the Cell SDK PPU toolchain (ppu-lv2-gcc) and scetool on PATH, the
# same combination webMAN MOD and PS3xPAD are built with. See ../docs/BUILD.md.
#
#   make            -> ds45pad_vsh.sprx  (plugin loader / XMB build)
#   make game       -> ds45pad_game.sprx (optional in-game build)
#   make all-builds -> both
#   make clean

CELL_MK_DIR = $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

CELL_BUILD_TOOLS = GCC

PPU_AR              = ppu-lv2-ar
PPU_RANLIB          = ppu-lv2-ranlib
PPU_STRIP           = ppu-lv2-strip
PPU_CC              = ppu-lv2-gcc
PPU_CXX             = ppu-lv2-g++
PPU_LD              = ppu-lv2-ld
PPU_CCLD            = ppu-lv2-gcc
PPU_CXXLD           = ppu-lv2-g++
PPU_PRX_CCLD        = ppu-lv2-gcc
PPU_PRX_CXXLD       = ppu-lv2-g++
PPU_PRX_STRIP       = ppu-lv2-prx-strip

PPU_PRX_FLAGS = -mprx -mno-sn-ld -Os -ffunction-sections -fdata-sections \
                -fno-builtin-printf -nodefaultlibs -std=gnu99 \
                -Wall -Wno-shadow -Wno-unused-parameter

PPU_CFLAGS       = $(PPU_PRX_FLAGS) $(EXTRA_CFLAGS)
PPU_LDFLAGS     += $(PPU_PRX_FLAGS)
PPU_PRX_LDFLAGS += -mprx -ffunction-sections -fdata-sections -Wl,--strip-unused-data

PPU_PRX_STRIPFLAGS += --strip-debug --strip-section-header

PPU_SRCS = libc.c util.c hook.c config.c ds4.c dualsense.c sticks.c padldd.c \
           gamehook.c usb.c passthru.c diag.c main.c

PPU_PRX_LDLIBS = -lusbd_stub -lio_stub -lfs_stub

PPU_PRX_TARGET = ds45pad.prx

# Signing parameters.
#
# -2 0A is the 3.55 key revision and it is not optional for PS3HEN: a plugin
# signed with an older revision makes HEN hang on boot. This is why PS3xPAD,
# which signs with -2 04, had to be re-released as "3.55 signed for HEN".
# webMAN MOD uses the values below and loads on both CFW and HEN.
SCETOOL_FLAGS = -0 SELF -1 TRUE -s FALSE -2 0A -3 1070000052000001 \
                -4 01000002 -5 APP -6 0003004000000000 -A 0001000000000000

SPRX_VSH  = ds45pad_vsh.sprx
SPRX_GAME = ds45pad_game.sprx

all: vsh

vsh:
	$(MAKE) $(PPU_OBJS_DEPENDS)
	$(PPU_PRX_STRIP) $(PPU_PRX_STRIPFLAGS) $(PPU_PRX_TARGET)
	scetool $(SCETOOL_FLAGS) -e $(PPU_PRX_TARGET) $(SPRX_VSH)

game:
	$(MAKE) clean-obj
	$(MAKE) EXTRA_CFLAGS=-DDS45_GAME $(PPU_OBJS_DEPENDS)
	$(PPU_PRX_STRIP) $(PPU_PRX_STRIPFLAGS) $(PPU_PRX_TARGET)
	scetool $(SCETOOL_FLAGS) -e $(PPU_PRX_TARGET) $(SPRX_GAME)

all-builds:
	$(MAKE) vsh
	$(MAKE) game

clean-obj:
	-rm -f *.o $(PPU_PRX_TARGET)

CLEANFILES = $(SPRX_VSH) $(SPRX_GAME) $(PPU_PRX_TARGET)

include $(CELL_MK_DIR)/sdk.target.mk
