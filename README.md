AM32 Bootloader
---------------

This is the bootloader for the AM32 project

Installing Build Tools
----------------------

Building is supported on Linux only. To install the required build
tools run env_setup_scripts/gcc_setup_linux.sh, or run "make
arm_sdk_install" directly. This downloads the pinned xPack GCC Arm
toolchain from its GitHub release (verified against a SHA256 pinned
in make/tools_install.mk) plus the legacy AM32-tools archive for the
V203 RISC-V toolchain and openocd, and unpacks them in tools. The
script also copies the Linux vscode settings into place.

Runing VSCode
-------------

When you run vscode it will recommend you install some key extensions:

 - C/C++ tools
 - Cortex-Debug
 - Makefile Tools

You will need to install these before doing a build.

Command Line Build
------------------

To build with the command line use the command "make". If your
environment is setup correctly you should be able to tab complete the
available targets. Otherwise you can run "make targets" to see the
available build targets.

CI Builds
---------

All of the bootloaders are automatically built in CI using github
actions. See the Actions tab on
https://github.com/am32-firmware/AM32-bootloader for the latest
builds.

Releases
--------

The latest release is available here:

https://github.com/am32-firmware/AM32-bootloader/releases

Getting Help
------------

If you need help with bootloader development please ask on the AM32
discord server in the development channel
https://discord.com/invite/h7ddYMmEVV
