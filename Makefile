# Build, then copy to the game folder.
#
#   make install               Build the ModLoader and every Mod in mods/, and copy them to $(GAME_DIR)
#   make install <name> ...    Build only the Mod in mods/<name>/ and copy it to $(GAME_DIR)/mods/<name>/
#                              (give loader as a <name> to include the ModLoader too)
#   make loader                Build only the ModLoader (dinput8.dll) and copy it to $(GAME_DIR)
#   make build                 Build everything (no copy)
#   make test                  Run the tests
#
# Write GAME_DIR (the game folder) in local.mk. local.mk is not tracked by Git.
# See local.mk.example for the format. It can also be set with the GAME_DIR environment variable or make install GAME_DIR=....

-include local.mk

CMAKE ?= cmake
CTEST ?= ctest
CONFIG ?= Release
BUILD_DIR ?= build

# Mods that have mods/<name>/CMakeLists.txt
ALL_MODS := $(patsubst mods/%/CMakeLists.txt,%,$(wildcard mods/*/CMakeLists.txt))

# Take the <name>s from make install <name> ... and make them do nothing as targets
ifeq (install,$(firstword $(MAKECMDGOALS)))
  INSTALL_NAMES := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  UNKNOWN_NAMES := $(filter-out loader $(ALL_MODS),$(INSTALL_NAMES))
  ifneq ($(strip $(UNKNOWN_NAMES)),)
    $(error Not found in mods/: $(UNKNOWN_NAMES) (available: $(ALL_MODS)))
  endif
  $(eval $(filter-out loader,$(INSTALL_NAMES)):;@:)
endif

ifeq ($(strip $(INSTALL_NAMES)),)
  INSTALL_MODS := $(ALL_MODS)
  INSTALL_LOADER := loader
else
  INSTALL_MODS := $(filter-out loader,$(INSTALL_NAMES))
  INSTALL_LOADER := $(filter loader,$(INSTALL_NAMES))
endif

.PHONY: configure build test loader install install-mods check-game-dir

configure:
	"$(CMAKE)" -S . -B "$(BUILD_DIR)" -A Win32

build: configure
	"$(CMAKE)" --build "$(BUILD_DIR)" --config $(CONFIG)

test: build
	"$(CTEST)" --test-dir "$(BUILD_DIR)" -C $(CONFIG) --output-on-failure

loader: check-game-dir configure
	"$(CMAKE)" --build "$(BUILD_DIR)" --config $(CONFIG) --target wots4_loader
	"$(CMAKE)" "-DSRC=$(BUILD_DIR)/game" "-DDEST=$(GAME_DIR)" -DONLY=dinput8.dll -P tools/deploy.cmake

install: $(INSTALL_LOADER) install-mods

# Copy one Mod
define deploy_mod
	"$(CMAKE)" "-DSRC=$(BUILD_DIR)/game/mods/$(1)" "-DDEST=$(GAME_DIR)/mods/$(1)" -P tools/deploy.cmake

endef

install-mods: check-game-dir configure
ifneq ($(strip $(INSTALL_MODS)),)
	"$(CMAKE)" --build "$(BUILD_DIR)" --config $(CONFIG) --target $(addprefix mod_,$(INSTALL_MODS))
	$(foreach m,$(INSTALL_MODS),$(call deploy_mod,$(m)))
endif

check-game-dir:
ifeq ($(strip $(GAME_DIR)),)
	$(error GAME_DIR is not set. Copy local.mk.example to local.mk and write the game folder in it)
endif
