GBDK_HOME ?= ../gbdk
GBDKDIR := $(abspath $(GBDK_HOME))/
LCC := $(GBDKDIR)bin/lcc

ROM := mgb.gb
SOURCES := \
	mGB.c \
	mgb_save.c \
	mGBASMFunctions.s \
	mGBASMSynthFunctions.s \
	mGBASMMidiFunctions.s

CFLAGS := -I.
LCCFLAGS := -Wa-l -Wl-m -Wl-j
ROMFLAGS := -Wm-ynMGB -Wm-yc -Wm-yt0x02 -Wm-yo4 -Wm-ya4 -Wm-yl0x00
UPSTREAM_BASELINE ?= upstream/master:mGB.gb

.PHONY: build clean test test-upstream sameboy-midi-regression

build: $(ROM)

$(ROM): $(SOURCES)
	@test -x "$(LCC)" || (echo "Missing GBDK-2020 lcc at $(LCC). Set GBDK_HOME=/path/to/gbdk."; exit 1)
	GBDKDIR="$(GBDKDIR)" "$(LCC)" $(CFLAGS) $(LCCFLAGS) $(ROMFLAGS) -o $@ $(SOURCES)
	python3 ../tools/normalize_rom_header.py $@

clean:
	rm -f $(ROM) mgb.ihx mgb.map mgb.noi mgb.sym mgb.cdb
	rm -f *.o *.lst *.rst *.asm *.adb

test: build
	python3 ../tools/regression_check.py --rom $(ROM) --map mgb.map --symbols mgb.noi --baseline ../mGB.gb --baseline ../Releases/mGB_1_3_3.gb

test-upstream: build
	python3 ../tools/regression_check.py --rom $(ROM) --map mgb.map --symbols mgb.noi --git-baseline $(UPSTREAM_BASELINE)

sameboy-midi-regression:
	python3 ../tools/run_sameboy_midi_regression.py --gbdk-home "$(GBDK_HOME)"
