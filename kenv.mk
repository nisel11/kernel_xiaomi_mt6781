# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2019 MediaTek Inc.

KERNEL_ENV_PATH := $(call my-dir)
KERNEL_ROOT_DIR := $(PWD)

define touch-kernel-image-timestamp
if [ -e $(1) ] && [ -e $(2) ] && cmp -s $(1) $(2); then \
 echo $(2) has no change;\
 mv -f $(1) $(2);\
else \
 rm -f $(1);\
fi
endef

# '\\' in command is wrongly replaced to '\\\\' in kernel/out/arch/arm/boot/compressed/.piggy.xzkern.cmd
define fixup-kernel-cmd-file
if [ -e $(1) ]; then cp $(1) $(1).bak; sed -e 's/\\\\\\\\/\\\\/g' < $(1).bak > $(1); rm -f $(1).bak; fi
endef

ifneq ($(strip $(TARGET_NO_KERNEL)),true)
  KERNEL_DIR := $(KERNEL_ENV_PATH)

  ifeq ($(KERNEL_TARGET_ARCH),arm64)
    TARGET_KERNEL_CROSS_COMPILE ?= $(KERNEL_ROOT_DIR)/prebuilts/gcc/$(HOST_PREBUILT_TAG)/aarch64/aarch64-linux-android-4.9.1/bin/aarch64-linux-android-
    TARGET_KERNEL_CLANG_COMPILE :=
    CC := $(TARGET_KERNEL_CROSS_COMPILE)gcc
    ifeq ($(strip $(TARGET_KERNEL_USE_CLANG)),true)
      CLANG_PATH := $(KERNEL_ROOT_DIR)/prebuilts/clang/host/linux-x86/clang-r370808
      TARGET_KERNEL_CLANG_COMPILE := CLANG_TRIPLE=aarch64-linux-gnu-
      CC := $(CLANG_PATH)/bin/clang
    endif
  else
    TARGET_KERNEL_CROSS_COMPILE ?= $(KERNEL_ROOT_DIR)/prebuilts/gcc/$(HOST_PREBUILT_TAG)/arm/arm-linux-androideabi-4.9.1/bin/arm-linux-androidkernel-
    TARGET_KERNEL_CLANG_COMPILE :=
    CC := $(TARGET_KERNEL_CROSS_COMPILE)gcc
    ifeq ($(strip $(TARGET_KERNEL_USE_CLANG)),true)
      CLANG_PATH := $(KERNEL_ROOT_DIR)/prebuilts/clang/host/linux-x86/clang-r370808
      TARGET_KERNEL_CLANG_COMPILE := CLANG_TRIPLE=arm-linux-gnueabi-
      CC := $(CLANG_PATH)/bin/clang
    endif
  endif

  ifneq ($(filter-out false,$(USE_CCACHE)),)
    CCACHE_EXEC ?= /usr/bin/ccache
    CCACHE_EXEC := $(abspath $(wildcard $(CCACHE_EXEC)))
  else
    CCACHE_EXEC :=
  endif
  ifneq ($(CCACHE_EXEC),)
    TARGET_KERNEL_CLANG_COMPILE += CCACHE_CPP2=yes CC='$(CCACHE_EXEC) $(CC)'
  else
    TARGET_KERNEL_CLANG_COMPILE += CC=$(CC)
  endif

  ifeq ($(wildcard $(TARGET_PREBUILT_KERNEL)),)
    KERNEL_OUT ?= $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ
    KERNEL_ROOT_OUT := $(if $(filter /% ~%,$(KERNEL_OUT)),,$(KERNEL_ROOT_DIR)/)$(KERNEL_OUT)
    ifeq ($(KERNEL_TARGET_ARCH), arm64)
        KERNEL_ZIMAGE_OUT := $(KERNEL_OUT)/arch/$(KERNEL_TARGET_ARCH)/boot/Image.gz
        KERNEL_DTB_TARGET := $(KERNEL_OUT)/arch/$(KERNEL_TARGET_ARCH)/boot/dts/mediatek/$(TARGET_BOARD_PLATFORM).dtb
    else
        KERNEL_ZIMAGE_OUT := $(KERNEL_OUT)/arch/$(KERNEL_TARGET_ARCH)/boot/zImage
        KERNEL_DTB_TARGET := $(KERNEL_OUT)/arch/$(KERNEL_TARGET_ARCH)/boot/dts/$(TARGET_BOARD_PLATFORM).dtb
    endif

    KERNEL_DTB_FILE := $(KERNEL_OUT)/arch/$(KERNEL_TARGET_ARCH)/boot/mtk.dtb
    INSTALLED_MTK_DTB_TARGET := $(BOARD_PREBUILT_DTBIMAGE_DIR)/mtk_dtb
    BUILT_KERNEL_TARGET := $(KERNEL_ZIMAGE_OUT).bin
    INSTALLED_KERNEL_TARGET := $(PRODUCT_OUT)/kernel
    TARGET_KERNEL_CONFIG := $(KERNEL_OUT)/.config
    KERNEL_CONFIG_FILE := $(KERNEL_DIR)/arch/$(KERNEL_TARGET_ARCH)/configs/$(KERNEL_DEFCONFIG)
    KERNEL_MAKE_OPTION := O=$(KERNEL_ROOT_OUT) ARCH=$(KERNEL_TARGET_ARCH) CROSS_COMPILE=$(TARGET_KERNEL_CROSS_COMPILE) $(TARGET_KERNEL_CLANG_COMPILE) ROOTDIR=$(KERNEL_ROOT_DIR)
    KERNEL_MAKE_PATH_OPTION := /usr/bin
  ifeq ($(KERNEL_TARGET_ARCH),arm64)
      ifeq ($(strip $(TARGET_KERNEL_USE_CLANG)),true)
          # for CONFIG_LTO_CLANG to find clang llvm-dis & llvm-ar & LLVMgold.so
          KERNEL_MAKE_OPTION += LD_LIBRARY_PATH=$(CLANG_PATH)/lib64:$$LD_LIBRARY_PATH
          KERNEL_MAKE_PATH_OPTION := $(KERNEL_MAKE_PATH_OPTION):$(CLANG_PATH)/bin
      endif
  endif
  ifneq ($(KERNEL_MAKE_PATH_OPTION),)
    KERNEL_MAKE_OPTION += PATH=$(KERNEL_MAKE_PATH_OPTION):$$PATH
  endif
  else
    BUILT_KERNEL_TARGET := $(TARGET_PREBUILT_KERNEL)
  endif #TARGET_PREBUILT_KERNEL is empty

endif #TARGET_NO_KERNEL
