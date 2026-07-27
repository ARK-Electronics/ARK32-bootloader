# Download and install the tools used for local builds and GitHub CI.
#
# The Arm compiler is the xPack GNU Arm Embedded GCC release pinned by
# XPACK_GCC_VER in make/tools.mk. Archives come from GitHub Releases so CI
# does not depend on a separately hosted tools tarball staying in sync with
# the path in tools.mk.
#
# The legacy AM32-tools archive is still needed for things the xPack release
# does not provide: the riscv-none-embed toolchain used by the V203 build,
# openocd, and (on Windows) the make/ utilities. It is only downloaded when
# those are missing.

# Shared pin (must match make/tools.mk)
XPACK_GCC_VER ?= 15.2.1-1.1
XPACK_GCC_DIR ?= xpack-arm-none-eabi-gcc-$(XPACK_GCC_VER)
XPACK_GCC_REL := https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v$(XPACK_GCC_VER)

# SHA256 of each xPack asset, from the .sha files published alongside the
# release. Verified before extracting, so a corrupted or truncated download
# (or a swapped mirror) fails loudly instead of producing a broken toolchain.
# Update these together with XPACK_GCC_VER.
XPACK_SHA_linux-x64   := da6a49ad4003944b823c6c93702a8787c922ab34bd7e918ec0eaf6933a9b1ff6
XPACK_SHA_linux-arm64 := 67980c7990eba7bb7ffdf39699102effd70889f5ac427be19a8c8a6c5fab2972
XPACK_SHA_darwin-x64  := 5be906a5194c3173e145a58048e5607ffff947773237802b93e55e23c415f1b6
XPACK_SHA_darwin-arm64 := 574082d35e49a2bcbdc355836b2a3ae5e5bb3b9456c9f5e37177db2ab4aad870
XPACK_SHA_win32-x64   := bae6a3d1667697ce750c3b13d6d26d80973ecedc2cc87bf04869e83447fd93ea

# Legacy archives: riscv toolchain (V203), openocd, Windows make utilities
WINDOWS_TOOLS := https://firmware.ardupilot.org/Tools/AM32-tools/windows-tools.zip
LINUX_TOOLS := https://firmware.ardupilot.org/Tools/AM32-tools/linux-tools.tar.gz
MACOS_TOOLS := https://firmware.ardupilot.org/Tools/AM32-tools/macos-tools.tar.gz

ifeq ($(OS),Windows_NT)

# Windows recipes run under cmd.exe (see tools.mk SHELL).
# Hash with .NET SHA256 rather than Get-FileHash: some Windows runners
# resolve to a PowerShell host where that cmdlet is missing, which left
# $got empty and failed the checksum check in CI Build Windows.
arm_sdk_install:
	@echo Installing windows tools
	@powershell -NoProfile -ExecutionPolicy Bypass -Command "\
		if (-not (Test-Path 'tools/windows/make/bin/make.exe')) { \
			Write-Host 'downloading windows-tools.zip (make utilities, riscv, openocd)'; \
			(New-Object System.Net.WebClient).DownloadFile('$(WINDOWS_TOOLS)', 'windows-tools.zip'); \
			Write-Host 'unpacking windows-tools.zip'; \
			Expand-Archive -Path windows-tools.zip -Force -DestinationPath .; \
		}; \
		if (-not (Test-Path 'tools/windows/$(XPACK_GCC_DIR)/bin/arm-none-eabi-gcc.exe')) { \
			Write-Host 'downloading $(XPACK_GCC_DIR) (win32-x64)'; \
			(New-Object System.Net.WebClient).DownloadFile( \
				'$(XPACK_GCC_REL)/xpack-arm-none-eabi-gcc-$(XPACK_GCC_VER)-win32-x64.zip', \
				'xpack-gcc-win.zip'); \
			$$want = '$(XPACK_SHA_win32-x64)'; \
			$$fs = [System.IO.File]::OpenRead((Resolve-Path 'xpack-gcc-win.zip')); \
			$$sha = [System.Security.Cryptography.SHA256]::Create(); \
			try { \
				$$got = [BitConverter]::ToString($$sha.ComputeHash($$fs)).Replace('-','').ToLowerInvariant(); \
			} finally { \
				$$fs.Dispose(); \
				$$sha.Dispose(); \
			}; \
			if ($$got -ne $$want) { throw \"xpack gcc checksum mismatch: got $$got want $$want\"; }; \
			Write-Host 'checksum ok'; \
			Write-Host 'unpacking xpack gcc into tools/windows/'; \
			New-Item -ItemType Directory -Force -Path tools/windows | Out-Null; \
			Expand-Archive -Path xpack-gcc-win.zip -Force -DestinationPath tools/windows; \
			Remove-Item -Force xpack-gcc-win.zip -ErrorAction SilentlyContinue; \
		} else { \
			Write-Host 'already installed: tools/windows/$(XPACK_GCC_DIR)'; \
		}"
	@echo windows tools install done

else
# MacOS and Linux
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)

# Prefer native arm64 on Apple Silicon; fall back to x64 (Rosetta).
MAC_XPACK_ARCH := $(shell uname -m | sed 's/x86_64/x64/;s/arm64/arm64/')
ifeq ($(MAC_XPACK_ARCH),arm64)
MAC_XPACK_ASSET := xpack-arm-none-eabi-gcc-$(XPACK_GCC_VER)-darwin-arm64.tar.gz
MAC_XPACK_SHA := $(XPACK_SHA_darwin-arm64)
else
MAC_XPACK_ASSET := xpack-arm-none-eabi-gcc-$(XPACK_GCC_VER)-darwin-x64.tar.gz
MAC_XPACK_SHA := $(XPACK_SHA_darwin-x64)
endif

arm_sdk_install:
	@echo Installing macos tools \(gcc $(XPACK_GCC_VER)\)
	@if [ ! -x tools/macos/$(XPACK_GCC_DIR)/bin/arm-none-eabi-gcc ]; then \
		echo "downloading $(MAC_XPACK_ASSET)"; \
		mkdir -p tools/macos downloads; \
		wget -q -O downloads/$(MAC_XPACK_ASSET) $(XPACK_GCC_REL)/$(MAC_XPACK_ASSET); \
		echo "$(MAC_XPACK_SHA)  downloads/$(MAC_XPACK_ASSET)" | shasum -a 256 -c -; \
		tar -xzf downloads/$(MAC_XPACK_ASSET) -C tools/macos; \
	else \
		echo "already installed: tools/macos/$(XPACK_GCC_DIR)"; \
	fi
	@if [ ! -d tools/macos/openocd ]; then \
		echo "downloading macos-tools.tar.gz (openocd)"; \
		mkdir -p downloads; \
		wget -q -O downloads/macos-tools.tar.gz $(MACOS_TOOLS); \
		tar xzf downloads/macos-tools.tar.gz; \
	fi
	@echo macos tools install done

else

# Linux x64 default (GitHub ubuntu-latest). arm64 runners can override via env.
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

endif
endif
