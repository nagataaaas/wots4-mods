# ビルドして、ゲームのフォルダへコピーする。
#
#   make install               ModLoader と mods/ のすべての Mod をビルドして $(GAME_DIR) へコピー
#   make install <名前> ...    mods/<名前>/ の Mod だけをビルドして $(GAME_DIR)/mods/<名前>/ へコピー
#                              （<名前> に loader と書くと ModLoader も）
#   make loader                ModLoader（dinput8.dll）だけをビルドして $(GAME_DIR) へコピー
#   make build                 すべてビルドする（コピーはしない）
#   make test                  テストする
#
# GAME_DIR（ゲームのフォルダ）は local.mk に書く。local.mk は Git で追跡しない。
# 書き方は local.mk.example を見る。環境変数 GAME_DIR でも、make install GAME_DIR=... でも指定できる。

-include local.mk

CMAKE ?= cmake
CTEST ?= ctest
CONFIG ?= Release
BUILD_DIR ?= build

# mods/<名前>/CMakeLists.txt がある Mod
ALL_MODS := $(patsubst mods/%/CMakeLists.txt,%,$(wildcard mods/*/CMakeLists.txt))

# make install <名前> ... の <名前> を受け取り、ターゲットとしては何もしないようにする
ifeq (install,$(firstword $(MAKECMDGOALS)))
  INSTALL_NAMES := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  UNKNOWN_NAMES := $(filter-out loader $(ALL_MODS),$(INSTALL_NAMES))
  ifneq ($(strip $(UNKNOWN_NAMES)),)
    $(error mods/ にない名前です: $(UNKNOWN_NAMES)（あるもの: $(ALL_MODS)）)
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

# 1つの Mod をコピーする
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
	$(error GAME_DIR が決まっていません。local.mk.example を local.mk にコピーして、ゲームのフォルダを書いてください)
endif
