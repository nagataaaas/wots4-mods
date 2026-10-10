# ビルドして、ゲームのフォルダへコピーする。
#
#   make loader            ModLoader（dinput8.dll）をビルドして $(GAME_DIR) へコピー
#   make mod MOD=<名前>    mods/<名前>/ の Mod をビルドして $(GAME_DIR)/mods/<名前>/ へコピー
#   make build             すべてビルドする（コピーはしない）
#   make test              テストする
#
# GAME_DIR（ゲームのフォルダ）は local.mk に書く。local.mk は Git で追跡しない。
# 書き方は local.mk.example を見る。環境変数 GAME_DIR でも、make loader GAME_DIR=... でも指定できる。

-include local.mk

CMAKE ?= cmake
CTEST ?= ctest
CONFIG ?= Release
BUILD_DIR ?= build

.PHONY: configure build test loader mod check-game-dir check-mod

configure:
	"$(CMAKE)" -S . -B "$(BUILD_DIR)" -A Win32

build: configure
	"$(CMAKE)" --build "$(BUILD_DIR)" --config $(CONFIG)

test: build
	"$(CTEST)" --test-dir "$(BUILD_DIR)" -C $(CONFIG) --output-on-failure

loader: check-game-dir configure
	"$(CMAKE)" --build "$(BUILD_DIR)" --config $(CONFIG) --target wots4_loader
	"$(CMAKE)" "-DSRC=$(BUILD_DIR)/game" "-DDEST=$(GAME_DIR)" -DONLY=dinput8.dll -P tools/deploy.cmake

mod: check-game-dir check-mod configure
	"$(CMAKE)" --build "$(BUILD_DIR)" --config $(CONFIG) --target mod_$(MOD)
	"$(CMAKE)" "-DSRC=$(BUILD_DIR)/game/mods/$(MOD)" "-DDEST=$(GAME_DIR)/mods/$(MOD)" -P tools/deploy.cmake

check-game-dir:
ifeq ($(strip $(GAME_DIR)),)
	$(error GAME_DIR が決まっていません。local.mk.example を local.mk にコピーして、ゲームのフォルダを書いてください)
endif

check-mod:
ifeq ($(strip $(MOD)),)
	$(error MOD を指定してください（例：make mod MOD=example）。mods/<名前>/ の <名前> です)
endif
