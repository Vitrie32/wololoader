CC      = i686-w64-mingw32-gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-format-truncation -std=c11 -static-libgcc
LDFLAGS = -shared -static -Wl,--enable-stdcall-fixup -Wl,--kill-at -lshell32 -lgdi32 -luser32 -lole32 -lwindowscodecs -luuid -lwinhttp
SRC     = src/main.c src/log.c src/mods.c src/hook.c src/menu.c src/changelog.c src/mdtext.c src/autostart.c src/civs.c src/gamemodes.c src/files.c src/crash.c src/shell.c src/ui_mods.c src/modsettings.c src/ui_hub.c src/hubcore.c src/iconcache.c src/datbuild.c src/json.c src/zlib_game.c src/autobuild.c src/popcap.c src/overflow.c src/selpanel.c src/mpguard.c src/mpcheck.c src/noworkshop.c src/loadscreen.c src/borderless.c src/script.c src/luagame.c src/basemod.c src/lobby.c src/monk.c src/gameapi.c src/counter.c src/formtab.c src/gamecmd.c src/uikit.c src/resources.c src/resbar.c src/buildpage.c src/iconmerge.c src/taunts.c src/history.c src/victory.c src/hotreload.c src/proxy_version.S

# Embedded Lua 5.4 (third_party/lua, unmodified). Only the core and the safe
# libraries: no lua.c/luac.c, no linit.c (script.c opens libs itself), no
# io/os/package/debug libs. Built with relaxed warnings (third-party code); our
# own sources keep -Wall -Wextra.
LUA_DIR  = third_party/lua/src
LUA_SRCS = lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser \
           lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib lmathlib \
           lstrlib ltablib lutf8lib
LUA_OBJ  = $(addprefix build/lua/,$(addsuffix .o,$(LUA_SRCS)))
LUA_CFLAGS = -O2 -std=gnu99 -w

build/lua/%.o: $(LUA_DIR)/%.c
	mkdir -p build/lua
	$(CC) $(LUA_CFLAGS) -c -o $@ $<

# TRACE=1 enables runtime selection field access tracing
ifdef TRACE
CFLAGS  += -DWL_SELTRACE
SRC     += src/seltrace.c
endif
# Game folder: set GAME in wololoader/local.mk (not in git) or on the command line.
-include local.mk
GAME   ?= $(HOME)/.steam/steam/steamapps/common/Age2HD

# Scriptorium v2 (mod creator HTTP server & launcher) sources and flags
SC_CATALOG_SRC = $(if $(wildcard scriptorium/catalog.c),scriptorium/catalog.c,scriptorium/catalog_stub.c)
SC_SRC  = scriptorium/main.c scriptorium/server.c scriptorium/api.c scriptorium/launcher.c \
          scriptorium/sc_zlib.c scriptorium/modio.c scriptorium/hub.c src/hubcore.c \
          $(SC_CATALOG_SRC) scriptorium/gamepath.c src/json.c src/datbuild.c
SC_CFLAGS  = -O2 -Wall -Wextra -Wno-format-truncation -std=c11 -static-libgcc -Iscriptorium -Ibuild -Isrc
SC_LDFLAGS = -mwindows -static -lws2_32 -lshell32 -lwinhttp

all: build/version.dll build/Scriptorium.exe

build/wl_icon.o: src/version.rc art/wololoader.ico
	mkdir -p build
	i686-w64-mingw32-windres src/version.rc -O coff -o $@

# Rebuild when switching between normal and TRACE=1 builds (the stamp records which).
BUILD_KIND := $(if $(TRACE),trace,normal)
build/kind.$(BUILD_KIND):
	mkdir -p build
	rm -f build/kind.*
	touch $@

build/version.dll: build/kind.$(BUILD_KIND) $(SRC) $(LUA_OBJ) build/wl_icon.o src/wololoader.h src/datbuild.h src/json.h src/hubcore.h src/modsettings.h src/mdtext.h src/version.def
	mkdir -p build
	$(CC) $(CFLAGS) -I$(LUA_DIR) -o $@ $(SRC) $(LUA_OBJ) build/wl_icon.o src/version.def $(LDFLAGS)

build/ui_files.h: $(wildcard scriptorium/ui/*) tools/gen_ui_files.py
	mkdir -p build
	python3 tools/gen_ui_files.py scriptorium/ui build/ui_files.h

# The icon library (scriptorium/icon_library/<category>/<id>.png + catalog.json) is embedded the
# same way, as g_icons_files in build/icons_files.h (SVGs and _src are left out: the UI only shows PNGs).
build/icons_files.h: $(wildcard scriptorium/icon_library/*/*.png) scriptorium/icon_library/catalog.json tools/gen_ui_files.py
	mkdir -p build
	python3 tools/gen_ui_files.py scriptorium/icon_library build/icons_files.h --lib icons .png,.json

scriptorium: build/Scriptorium.exe

build/sc_icon.o: scriptorium/scriptorium.rc art/wololoader.ico
	mkdir -p build
	i686-w64-mingw32-windres scriptorium/scriptorium.rc -O coff -o $@

build/Scriptorium.exe: build/sc_icon.o build/ui_files.h build/icons_files.h $(SC_SRC) scriptorium/server.h scriptorium/api.h scriptorium/launcher.h scriptorium/catalog.h scriptorium/sc_zlib.h scriptorium/modio.h scriptorium/hub.h src/json.h src/datbuild.h src/hubcore.h
	mkdir -p build
	$(CC) $(SC_CFLAGS) -o $@ $(SC_SRC) build/sc_icon.o $(SC_LDFLAGS)

install: build/version.dll build/Scriptorium.exe
	cp build/version.dll "$(GAME)/version.dll"
	mkdir -p "$(GAME)/wololoader/mods"
	cp build/Scriptorium.exe "$(GAME)/wololoader/Scriptorium.exe"
	# What's new window (changelog.c, a click on the main menu label)
	cp ../CHANGELOG.md "$(GAME)/wololoader/changelog.txt"
	rsvg-convert -h 66 art/wololoader_mark.svg -o build/menu_mark.png
	python3 -I tools/make_menu_icon.py "$(GAME)" build/menu_mark.png
	# Wololoader Base mod (feature switches): created once, never overwritten.
	test -e "$(GAME)/wololoader/mods/WololoaderBase/mod.json" || { mkdir -p "$(GAME)/wololoader/mods/WololoaderBase" && cp base_mod/mod.json "$(GAME)/wololoader/mods/WololoaderBase/mod.json"; }
	cp art/logo_256.png "$(GAME)/wololoader/mods/WololoaderBase/icon.png"
	# Wololoader's own command icons (iconmerge.c appends them to the game's sheets)
	mkdir -p "$(GAME)/wololoader/mods/WololoaderBase/icons/50721"
	cp base_mod/icons/50721/*.slp "$(GAME)/wololoader/mods/WololoaderBase/icons/50721/"
	# Wololoader Base scripts (DevPanel + console)
	mkdir -p "$(GAME)/wololoader/mods/WololoaderBase/scripts"
	cp -r base_mod/scripts/. "$(GAME)/wololoader/mods/WololoaderBase/scripts/"
	# Remove leftover old shipped DevPanel mod only if mod.json matches the original shipped one
	@python3 -c 'import json, os, shutil, sys; d = os.path.join(sys.argv[1], "wololoader", "mods", "DevPanel"); p = os.path.join(d, "mod.json"); info = None; exec("try:\n with open(p, encoding=\"utf-8\") as f: info = json.load(f)\nexcept Exception: pass"); (shutil.rmtree(d), print("Removed legacy DevPanel mod:", d)) if isinstance(info, dict) and info.get("name") == "Dev Panel" and info.get("author") == "ali" else None' "$(GAME)"
	# Icons drawn over the Mods screen buttons and mod rows (ui_mods.c loads <game>\wololoader\ui\<name>.png)
	mkdir -p "$(GAME)/wololoader/ui"
	$(MAKE) ui-icons
	cp build/ui/*.png "$(GAME)/wololoader/ui/"

# Release installers (needs `make install` once: the Mods button images come from the game folder).
VERSION := $(shell sed -n 's/^\#define WOLOLOADER_VERSION "\(.*\)"/\1/p' src/wololoader.h)

build/setup_res.o: installer/wlsetup.rc installer/wlsetup.manifest art/wololoader.ico
	mkdir -p build
	cd installer && i686-w64-mingw32-windres wlsetup.rc -O coff -o ../$@

build/wlsetup_stub.exe: installer/wlsetup.c build/setup_res.o src/wololoader.h
	$(CC) -O2 -Wall -Wextra -std=c11 -municode -mwindows -DWL_VERSION='"$(VERSION)"' -o $@ $(filter %.c %.o,$^) -static -lcomctl32 -lshell32 -lole32 -ladvapi32
	i686-w64-mingw32-strip $@

release: build/version.dll build/Scriptorium.exe build/wlsetup_stub.exe installer/install.sh.in tools/package.py ../CHANGELOG.md ui-icons
	python3 -I tools/package.py "$(VERSION)" "$(GAME)" build/release build/wlsetup_stub.exe

# Mods screen icons: one PNG per shown size (name_SIZE.png); the drawables are not scaled in
# ui_mods.c (see ui_icon). Needed by install AND release (0.6.0 almost shipped without them).
ui-icons:
	mkdir -p build/ui
	for f in art/ui/*.svg; do for s in 28 22 20 18; do rsvg-convert -w $$s -h $$s "$$f" -o build/ui/$$(basename "$$f" .svg)_$$s.png || exit 1; done; done

uninstall:
	rm -f "$(GAME)/version.dll"
	rm -f "$(GAME)/wololoader/Scriptorium.exe"

clean:
	rm -rf build

.PHONY: all scriptorium install uninstall clean release ui-icons
