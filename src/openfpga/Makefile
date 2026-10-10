# Butterscotch for openfpgaOS (Analogue Pocket / MiSTer), packaged as an
# Undertale core.
#
# Built against the openfpgaOS SDK, which is not part of this repository:
# point SDK_ROOT at a checkout of https://github.com/openfpgaOS/openfpgaSDK
# (default: one next to this repository). The software renderer draws into
# the SDK's RGB555 framebuffer. You supply your own Undertale data.win.
#
# Everything the build writes goes under out/ here; the SDK checkout is only
# read. The SD card tree ends up in out/build/pocket/butterscotch/.
#
# Targets:
#   make              Build the custom core
#   make debug        Build, push via UART, stream console
#   make copy         Copy this custom core to Pocket SD
#   make package      Package this custom core into a ZIP
#   make test         Test on desktop (SDL2) -> ./undertale_pc
#   make clean        Remove build artifacts
#

# GAME picks which game's program is built. The core is one, with an entry
# per game on the Pocket; each game has its own program, folder of game
# data and save file:
#   undertale (default)  data beside this Makefile                           WAD 16
#   deltarune            games/deltarune/ (see tools/deltarune-setup.sh)     WAD 17
# The SD tree is assembled from every game built so far (tools/mkcard.sh),
# so `make && make GAME=deltarune` gives a card with both.
GAME     ?= undertale
ifeq ($(GAME),undertale)
G         =
WADS     ?= 16
else ifeq ($(GAME),deltarune)
G         = games/deltarune/
WADS     ?= 17
# Sounds of audio group 1 live in their own file, and the game reads its
# text from lang_en.json; both go into the audio pack.
MUSIC_EXTRAS = group:1:$(G)audiogroup1.dat file:$(G)lang_en.json
GAME_DEFS = -DUT_GAME_DELTARUNE
else
$(error GAME=$(GAME) is not known; use undertale or deltarune)
endif
APP       = $(GAME)
# Platform target: pocket (APF SD tree under build/pocket/<app>/) or
# mister (disk-image bundle under build/mister/<app>/). Overridable per
# invocation: `make TARGET=mister`, or from the SDK root Makefile.
TARGET   ?= pocket
# MiSTer network target for `make copy TARGET=mister` (override: MISTER_IP=...).
MISTER_IP ?= mister.local
SDK_ROOT ?= $(abspath $(CURDIR)/../../../openfpgaSDK)
SDK_DIR   = $(SDK_ROOT)/src/sdk
ifeq ($(wildcard $(SDK_DIR)/sdk.mk),)
$(error No openfpgaOS SDK at $(SDK_ROOT). Clone https://github.com/openfpgaOS/openfpgaSDK there, or pass SDK_ROOT=<its path>)
endif
# The SDK's scripts take a root that holds runtime/ and receive build/; out/
# plays that part, with runtime/ linked to the SDK's.
ROOT      = $(CURDIR)/out
OBJ_DIR   = $(ROOT)/.obj/$(APP)
BUILD_DIR = $(OBJ_DIR)
DIST      = $(CURDIR)/dist
CARD      = butterscotch
OUT       = $(ROOT)/build/$(TARGET)/$(CARD)
RUNTIME   = $(SDK_ROOT)/runtime

# ── Butterscotch sources ─────────────────────────────────────────────
# Butterscotch is this repository. Absolute, so that object paths stay
# inside the object directory.
BS        = $(abspath $(CURDIR)/../..)
BS_SRC    = $(BS)/src
BS_VENDOR = $(BS)/vendor

BS_CORE_SRCS   = $(wildcard $(BS_SRC)/*.c)
BS_SW_SRCS     = $(wildcard $(BS_SRC)/sw/*.c)
BS_IMAGE_SRCS  = $(wildcard $(BS_SRC)/image/*.c)
BS_FONT_SRCS   = $(wildcard $(BS_SRC)/debug_font/*.c)
BS_VIDEO_SRCS  = $(BS_SRC)/video/null_video.c
BS_STB_SRCS    = $(BS_SRC)/embedded/stb_impl.c
BS_VENDOR_SRCS = $(wildcard $(BS_VENDOR)/bzip2/*.c) \
                 $(BS_VENDOR)/sha1/sha1.c $(BS_VENDOR)/md5/md5.c \
                 $(BS_VENDOR)/base64/base64.c \
                 $(BS_VENDOR)/miniz/miniz.c $(BS_VENDOR)/miniz/miniz_tdef.c $(BS_VENDOR)/miniz/miniz_tinfl.c
# Box2D physics is left out (ENABLE_PHYSICS off): Undertale does not use it.
BS_PHYSICS_SRCS = $(BS_SRC)/physics/physics.c $(BS_SRC)/physics/disabled/physics_disabled.c

SRCS = main.c platform/of_platform.c platform/of_diag.c platform/of_log.c \
       platform/of_perf.c platform/of_audio_system.c platform/ut_bench.c \
       platform/ut_strings.c platform/ut_save_fs.c platform/ut_native.c \
       $(BS_CORE_SRCS) $(BS_SW_SRCS) $(BS_IMAGE_SRCS) $(BS_FONT_SRCS) \
       $(BS_VIDEO_SRCS) $(BS_STB_SRCS) $(BS_VENDOR_SRCS) $(BS_PHYSICS_SRCS)
SRCS_CXX =

BS_INCLUDES = -Iplatform -I$(BS_SRC) -I$(BS_SRC)/sw -I$(BS_SRC)/image -I$(BS_SRC)/video \
              -I$(BS_SRC)/debug_font \
              -I$(BS_VENDOR)/stb/ds -I$(BS_VENDOR)/stb/image -I$(BS_VENDOR)/stb/vorbis \
              -I$(BS_VENDOR)/bzip2 -I$(BS_VENDOR)/sha1 -I$(BS_VENDOR)/md5 \
              -I$(BS_VENDOR)/base64 -I$(BS_VENDOR)/miniz

# Undertale v1.08 is WAD 16; building only that version shrinks the
# interpreter hot path. The CPU's FPU is single-precision, so GML reals are
# floats (same trade-off the PS2 port makes).
# Override to test other games on desktop, e.g. `make test WADS="14 16"`.
BS_DEFS = $(GAME_DEFS) -DUT_GAME_NAME='"$(APP)"' -DUT_SAVE_SLOT_FILE='"$(APP)_0.sav"' -DUT_LOG_SLOT_FILE='"$(APP)_1.sav"' -DUT_FAST_LOOP $(foreach w,$(WADS),-DENABLE_WAD$(w)) -DENABLE_SW_RENDERER -DPIXEL_SIZE=16 \
          -DUSE_FLOAT_REALS -DNO_RVALUE_INT64 -DSW_DRAW_PROFILE -DPLATFORM_BUSY_TICK -DENABLE_VM_GML_PROFILER \
          -DBUTTERSCOTCH_VIDEO_NULL -DUSE_PLATFORM_AUDIO -DUSE_PLATFORM_FILE_SYSTEM -DSW_PLATFORM_FRAMEBUFFER -DDATAWIN_LOG_CHUNKS \
          -DBUTTERSCOTCH_COMMIT_HASH='"openfpga"' -DBUTTERSCOTCH_COMMIT_DATE='"unknown"'

# Decoded texture pages are cached up to TEXTURE_CACHE_MB, but on the device
# the real limit is the heap (about 51 MB for everything): after each load
# the cache drops old pages until TEXTURE_RESERVE_MB can still be allocated.
TEXTURE_CACHE_MB ?= 40
TEXTURE_RESERVE_MB ?= 4
BS_DEFS += -DTEXTURE_CACHE_BYTES='($(TEXTURE_CACHE_MB)u*1024u*1024u)' \
           -DTEXTURE_CACHE_RESERVE_BYTES='($(TEXTURE_RESERVE_MB)u*1024u*1024u)'

ifeq ($(TRACE),1)
BS_DEFS += -DUT_TRACE_FRAMES
endif

BS_WARN = -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function \
          -Wno-sign-compare -Wno-missing-field-initializers

# UT_OPT: extra optimisation flags for the device build, after the SDK's -O2
# (the last -O wins), named in a benchmark report's title. Build with some
# into an OBJ_DIR of their own and hand mkcompare.sh "<name>:<app.elf>" to
# get a benchmark entry for it. Tried on the Pocket, 2026-10-09: -O3 and
# -O3 -flto change nothing; -Os is 16 to 22% slower; taking off the SDK's
# 64-byte alignment of functions and loops is 2 to 4% slower.
# Experiments ride the same way as defines. Tried so and dropped: a pool for
# script call frames in place of calloc and free (0.2 to 0.3 ms of a 46 ms
# undrawn battle frame, within run-to-run variation).
UT_OPT ?=
CFLAGS = $(BS_DEFS) $(BS_INCLUDES) $(BS_WARN) $(UT_OPT) -DUT_BUILD_NOTE='"$(UT_OPT)"'
LDFLAGS += $(UT_OPT)

# Data files bundled into the per-target deliverable (override per core).
# data.win is your own copy of the game data (gitignored); when present it is
# placed next to the ELF in the SD tree, together with the texture pack
# generated from it.
#
# music/ is a directory (or symlink) holding the game's .ogg files; when
# present they are converted into music.bin.
GAME_PACKS = $(if $(wildcard $(G)data.win),$(G)textures.bin) $(if $(wildcard $(G)music/*.ogg),$(G)music.bin)
MISTER_ASSETS ?= $(wildcard $(G)data.win) $(GAME_PACKS)

# Neither sdk.mk's rules nor the desktop ones track headers, and objects built
# against an old struct layout link without complaint and then crash (the
# device build did, twice). So a change to any header empties the object
# directories this run builds into. (Before sdk.mk, which looks at what is
# in them; PC_OBJ_DIR is only set this early when a sub-make passes it.)
HEADER_SUM := $(shell cat $(BS_SRC)/*.h $(BS_SRC)/*/*.h platform/*.h 2>/dev/null | cksum | cut -d' ' -f1)
$(foreach d,$(OBJ_DIR) $(or $(PC_OBJ_DIR),$(ROOT)/.obj/$(APP)-pc),$(shell [ "$$(cat $(d)/.headers 2>/dev/null)" = "$(HEADER_SUM)" ] || { rm -rf $(d); mkdir -p $(d); echo $(HEADER_SUM) > $(d)/.headers; }))

# Declare default target before sdk.mk's rules
.DEFAULT_GOAL := all

include $(SDK_DIR)/sdk.mk

# Host tool: converts data.win's texture pages to the renderer's 16-bit
# format so the device does not have to decode PNGs.
MKTEXPACK = $(ROOT)/.obj/$(APP)-tools/mktexpack
MKTEXPACK_SRCS = tools/mktexpack.c platform/of_log.c $(BS_SRC)/log.c $(BS_SRC)/gettime.c \
                 $(BS_IMAGE_SRCS) $(BS_STB_SRCS) $(wildcard $(BS_VENDOR)/bzip2/*.c)

$(MKTEXPACK): $(MKTEXPACK_SRCS)
	@mkdir -p $(dir $@)
	$(PC_CC) -O2 -DUT_HOST_TOOL $(BS_DEFS) $(BS_INCLUDES) $(BS_WARN) $(MKTEXPACK_SRCS) -lm -o $@

$(G)textures.bin: $(G)data.win $(MKTEXPACK)
	$(MKTEXPACK) $(G)data.win $@

# Host tool: converts the game's .ogg music and the sounds embedded in
# data.win to mono ADPCM so the device does not have to decode Vorbis.
MKMUSIC = $(ROOT)/.obj/$(APP)-tools/mkmusic
MUSIC_RATE ?= 32000

$(MKMUSIC): tools/mkmusic.c platform/ut_music_pack.h
	@mkdir -p $(dir $@)
	$(PC_CC) -O2 -w tools/mkmusic.c $(BS_VENDOR)/stb/vorbis/stb_vorbis.c -lm -o $@

# The extra inputs are "group:<n>:<file>" and "file:<file>"; a change in one
# of those files rebuilds the pack too.
MUSIC_EXTRA_FILES = $(wildcard $(foreach e,$(MUSIC_EXTRAS),$(lastword $(subst :, ,$(e)))))

$(G)music.bin: $(MKMUSIC) $(G)data.win $(wildcard $(G)music/*.ogg) $(MUSIC_EXTRA_FILES)
	$(MKMUSIC) $(G)data.win $(G)music $@ $(MUSIC_RATE) $(MUSIC_EXTRAS)

# Host tool: packs a desktop save folder into the save slot file and back.
#   make import-save SAVE_DIR=<desktop save folder>   -> undertale_0.sav
#   make export-save EXPORT_DIR=<folder to write>     <- undertale_0.sav
# The slot file belongs at Saves/butterscotch/common/ on the SD card. It is not
# part of the SD tree, so copying a build never replaces progress on the card.
MKSAVE = $(ROOT)/.obj/$(APP)-tools/mksave
SAVE_DIR ?= $(HOME)/Library/Application Support/com.tobyfox.undertale
SAVE_FILE ?= $(APP)_0.sav

$(MKSAVE): tools/mksave.c platform/ut_save_format.h
	@mkdir -p $(dir $@)
	$(PC_CC) -O2 -Wall -Wextra tools/mksave.c -o $@

.PHONY: import-save export-save
import-save: $(MKSAVE)
	$(MKSAVE) pack "$(SAVE_DIR)" $(SAVE_FILE)

export-save: $(MKSAVE)
	@[ -n "$(EXPORT_DIR)" ] || { echo "export-save: give EXPORT_DIR=<folder to write the save files into>"; exit 1; }
	$(MKSAVE) unpack $(SAVE_FILE) "$(EXPORT_DIR)"

# Build ELF, then assemble SD card image in build/<app>/
all: $(OBJ_DIR)/app.elf $(GAME_PACKS) release
	@$(SIZE) $<

# Assemble the per-target deliverable.  Each platform owns its layout
# (pocket = APF tree, mister = disk image); the rule names no target —
# adding a target = add platforms/<target>/image.sh.
$(ROOT)/runtime:
	@mkdir -p $(ROOT)
	ln -sfn $(SDK_ROOT)/runtime $@

ifeq ($(TARGET),pocket)
release: $(OBJ_DIR)/app.elf $(GAME_PACKS) | $(ROOT)/runtime
	@tools/mkcard.sh "$(SDK_DIR)/platforms/pocket/image.sh" "$(ROOT)" "$(DIST)" "$(CURDIR)"
else
release: $(OBJ_DIR)/app.elf $(GAME_PACKS) | $(ROOT)/runtime
	@$(SDK_DIR)/platforms/$(TARGET)/image.sh "$(APP)" "$(OBJ_DIR)/app.elf" "$(ROOT)" "$(DIST)" $(MISTER_ASSETS)
endif

debug: all
	@$(SDK_ROOT)/scripts/debug.sh $$(ls -d $(OUT)/Assets/*/common 2>/dev/null)/$(APP).elf

# Deploy to the device.  MISTER_IP rides the environment so the same
# invocation serves every platform's copy.sh (pocket ignores it and
# auto-detects the SD card; mister uses it as the network host).
# On macOS the Pocket copy goes through tools/sdcopy.sh, which also works over
# SSH on a headless machine (waits for the card, mounts, verifies, ejects).
ifeq ($(shell uname -s)-$(TARGET),Darwin-pocket)
COPY_CMD = tools/sdcopy.sh "$(OUT)"
else
COPY_CMD = MISTER_IP="$(MISTER_IP)" $(SDK_DIR)/platforms/$(TARGET)/copy.sh "$(APP)" "$(OBJ_DIR)/app.elf"
endif

copy: all
	@$(COPY_CMD)

# Side-by-side comparison of OS versions and CPU variants on one SD card.
# SDK09 is a checkout carrying the v0.9 SDK and Pocket runtime (the Diablo
# port's tree works). Adds Benchmark instances and two v0.9 cores next to
# this one; see tools/mkcompare.sh.
SDK09   ?= $(abspath $(CURDIR)/../../../Diablo)
OBJ09    = $(ROOT)/.obj/$(APP)-v09

compare: all
	$(MAKE) SDK_DIR=$(SDK09)/src/sdk OBJ_DIR=$(OBJ09) BUILD_DIR=$(OBJ09) $(OBJ09)/app.elf
	@tools/mkcompare.sh "$(OUT)" "$(SDK09)/runtime/pocket" "$(OBJ09)/app.elf"

# Like `copy`, but without re-running the assembly step, which would remove
# the comparison cores again.
compare-copy: compare
	@$(COPY_CMD)

package: all
	@$(SDK_ROOT)/scripts/package.sh $(APP)

# Desktop build. sdk.mk's app_pc rule assumes a Linux host (of_init.c needs
# <sys/auxv.h>, and sdl2-config omits the SDL2 parent include dir on
# Homebrew), so this core builds its own per-object desktop binary that
# works on macOS too. of_init.c only fetches the kernel's caps/services
# tables, which the PC shim does not use.
PC_OBJ_DIR = $(ROOT)/.obj/$(APP)-pc
PC_SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags)
PC_SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs)
PC_SRCS = $(SRCS) $(SDK_DIR)/pc/of_sdl2.c
PC_OBJS = $(patsubst %.c,$(PC_OBJ_DIR)/%.o,$(subst ../,up/,$(PC_SRCS)))

$(PC_OBJ_DIR)/up/%.o: ../%.c
	@mkdir -p $(dir $@)
	$(PC_CC) $(ALL_PC_CFLAGS) $(PC_SDL_CFLAGS) -c -o $@ $<

$(PC_OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(PC_CC) $(ALL_PC_CFLAGS) $(PC_SDL_CFLAGS) -c -o $@ $<

PC_BIN ?= $(APP)_pc

$(PC_BIN): $(PC_OBJS)
	$(PC_CC) $(PC_OBJS) $(PC_SDL_LIBS) $(PC_EXTRA_CFLAGS) -lm -o $@

test: $(PC_BIN) $(GAME_PACKS)

# The desktop build again, with AddressSanitizer and UndefinedBehaviorSanitizer,
# as undertale_pc_asan. Run it through the same frame scripts as undertale_pc:
# it stops with a report on an out-of-bounds access, a use after free or
# undefined arithmetic, which on the device would be garbage or a hang.
# Undefined-behaviour findings are printed once per place and the run goes on
# (vendored code has a few); a memory error stops it.
# On macOS, run it with DYLD_FALLBACK_LIBRARY_PATH=$(brew --prefix)/lib: under
# the sanitizer sdl2-compat cannot find SDL3 by itself and blocks on a dialog.
# -fno-sanitize=alignment,function: Butterscotch reads unaligned fields out of
# data.win and calls builtins through a differently typed pointer by design.
ASAN_FLAGS = -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined \
             -fno-sanitize=alignment,function
asan:
	$(MAKE) PC_BIN=undertale_pc_asan PC_OBJ_DIR=$(ROOT)/.obj/$(APP)-pc-asan \
	        PC_EXTRA_CFLAGS="$(ASAN_FLAGS)" undertale_pc_asan

# The desktop build again, counting every bytecode instruction the game runs,
# as undertale_pc_ops. Run it with UT_EXIT_FRAME=<n>: it prints the ranking
# when it leaves the main loop. A separate binary because the count is a
# compile-time option of Butterscotch's and costs time on every instruction.
ops:
	$(MAKE) PC_BIN=undertale_pc_ops PC_OBJ_DIR=$(ROOT)/.obj/$(APP)-pc-ops \
	        PC_EXTRA_CFLAGS="-DENABLE_VM_OPCODE_PROFILER" undertale_pc_ops

# Strict warnings for the code this port owns (not Butterscotch or the SDK,
# whose headers are treated as system headers so they stay quiet). Compiles
# nothing; fails if anything is reported.
LINT_SRCS = main.c $(wildcard platform/*.c) $(wildcard tools/*.c)
LINT_WARN = -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Wdouble-promotion \
            -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wformat=2 -Werror
LINT_INCLUDES = -Iplatform -I. $(patsubst -I%,-isystem %,$(filter-out -Iplatform,$(BS_INCLUDES))) \
                -isystem $(SDK_DIR)/include
lint:
	@for f in $(LINT_SRCS); do \
	    $(PC_CC) -fsyntax-only -DOF_PC $(BS_DEFS) $(LINT_INCLUDES) $(PC_SDL_CFLAGS) $(LINT_WARN) $$f || failed=1; \
	done; [ -z "$$failed" ] && echo "lint: clean"

clean: sdk-clean
	rm -rf $(PC_OBJ_DIR) $(ROOT)/.obj/$(APP)-pc-asan $(ROOT)/.obj/$(APP)-pc-ops undertale_pc undertale_pc_asan undertale_pc_ops
	rm -rf $(ROOT)/build/pocket/$(CARD) $(ROOT)/build/mister/$(APP)

.PHONY: all release debug copy compare compare-copy package test asan ops lint clean
