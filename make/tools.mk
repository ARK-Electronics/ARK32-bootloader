###############################################################
#
# Host tools (Linux only)
#
###############################################################

# Pin the xPack GNU Arm Embedded GCC version used for local builds and CI.
# Keep in sync with make/tools_install.mk (arm_sdk_install downloads this).
XPACK_GCC_VER := 15.2.1-1.1
XPACK_GCC_DIR := xpack-arm-none-eabi-gcc-$(XPACK_GCC_VER)

# Builds and arm_sdk_install support Linux only.
OSDIR := linux
ARM_SDK_PREFIX := tools/linux/$(XPACK_GCC_DIR)/bin/arm-none-eabi-
CP := cp
DSEP := /
NUL := /dev/null
MKDIR := mkdir
RM := rm
CUT := cut
FGREP := fgrep

# workaround for lack of a lowercase function in GNU make
# look away before this sends you blind ....
lc = $(subst A,a,$(subst B,b,$(subst C,c,$(subst D,d,$(subst E,e,$(subst F,f,$(subst G,g,$(subst H,h,$(subst I,i,$(subst J,j,$(subst K,k,$(subst L,l,$(subst M,m,$(subst N,n,$(subst O,o,$(subst P,p,$(subst Q,q,$(subst R,r,$(subst S,s,$(subst T,t,$(subst U,u,$(subst V,v,$(subst W,w,$(subst X,x,$(subst Y,y,$(subst Z,z,$1))))))))))))))))))))))))))
