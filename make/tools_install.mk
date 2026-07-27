# Download and install the tools used for local builds and GitHub CI.
#
# Linux only. The Arm compiler is the xPack GNU Arm Embedded GCC release
# pinned by XPACK_GCC_VER in make/tools.mk. Archives come from GitHub
# Releases so CI does not depend on a separately hosted tools tarball
# staying in sync with the path in tools.mk.
#
# The legacy AM32-tools archive is still needed for things the xPack
# release does not provide: the riscv-none-embed toolchain used by the
# V203 build, and openocd. It is only downloaded when those are missing.

# Shared pin (must match make/tools.mk)
XPACK_GCC_VER ?= 15.2.1-1.1
XPACK_GCC_DIR ?= xpack-arm-none-eabi-gcc-$(XPACK_GCC_VER)
XPACK_GCC_REL := https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v$(XPACK_GCC_VER)

# SHA256 of each Linux xPack asset, from the .sha files published
# alongside the release. Verified before extracting, so a corrupted or
# truncated download (or a swapped mirror) fails loudly instead of
# producing a broken toolchain. Update these together with XPACK_GCC_VER.
XPACK_SHA_linux-x64   := da6a49ad4003944b823c6c93702a8787c922ab34bd7e918ec0eaf6933a9b1ff6
XPACK_SHA_linux-arm64 := 67980c7990eba7bb7ffdf39699102effd70889f5ac427be19a8c8a6c5fab2972

LINUX_TOOLS := https://firmware.ardupilot.org/Tools/AM32-tools/linux-tools.tar.gz

# Linux x64 default (GitHub ubuntu-latest). arm64 hosts can override via env.
LINUX_XPACK_ARCH ?= $(shell uname -m | sed 's/x86_64/x64/;s/aarch64/arm64/')
ifeq ($(LINUX_XPACK_ARCH),arm64)
LINUX_XPACK_ASSET := xpack-arm-none-eabi-gcc-$(XPACK_GCC_VER)-linux-arm64.tar.gz
LINUX_XPACK_SHA := $(XPACK_SHA_linux-arm64)
else
LINUX_XPACK_ASSET := xpack-arm-none-eabi-gcc-$(XPACK_GCC_VER)-linux-x64.tar.gz
LINUX_XPACK_SHA := $(XPACK_SHA_linux-x64)
endif

arm_sdk_install:
	@echo Installing linux tools \(gcc $(XPACK_GCC_VER)\)
	@if [ ! -x tools/linux/$(XPACK_GCC_DIR)/bin/arm-none-eabi-gcc ]; then \
		echo "downloading $(LINUX_XPACK_ASSET)"; \
		mkdir -p tools/linux downloads; \
		wget -q -O downloads/$(LINUX_XPACK_ASSET) $(XPACK_GCC_REL)/$(LINUX_XPACK_ASSET); \
		echo "$(LINUX_XPACK_SHA)  downloads/$(LINUX_XPACK_ASSET)" | sha256sum -c -; \
		tar -xzf downloads/$(LINUX_XPACK_ASSET) -C tools/linux; \
	else \
		echo "already installed: tools/linux/$(XPACK_GCC_DIR)"; \
	fi
	@if [ ! -x tools/linux/riscv-embedded-gcc/bin/riscv-none-embed-gcc ]; then \
		echo "downloading linux-tools.tar.gz (riscv toolchain for V203, openocd)"; \
		mkdir -p downloads; \
		wget -q -O downloads/linux-tools.tar.gz $(LINUX_TOOLS); \
		tar xzf downloads/linux-tools.tar.gz; \
	fi
	@echo linux tools install done
