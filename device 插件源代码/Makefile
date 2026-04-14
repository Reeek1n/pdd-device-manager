THEOS ?= /Users/leeekin/theos
SDKVERSION ?= 13.7
THEOS_PACKAGE_DIR ?= $(CURDIR)/../../runtime/packages

ARCHS = arm64
TARGET = iphone:clang:13.7:13.0
TARGET_CODESIGN ?= codesign
TARGET_CODESIGN_FLAGS ?= -s - --force
FINALPACKAGE ?= 1

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = pdd1.0

pdd1.0_FILES = Tweak.x
pdd1.0_PLIST_FILES = substrate/pdd1.0.plist
pdd1.0_CFLAGS = -fobjc-arc -DTHEOS_LEAN_AND_MEAN=1 -fmodules-cache-path=$(CURDIR)/.theos/module-cache -Wno-error=deprecated-module-dot-map
pdd1.0_FRAMEWORKS = Foundation Security

# 移除CydiaSubstrate依赖（用于IPA注入）
pdd1.0_LDFLAGS = -Wl,-no_weak_imports

include $(THEOS)/makefiles/tweak.mk
