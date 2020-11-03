CUDA_VER?=10.2
ifeq ($(CUDA_VER),)
	$(error "CUDA_VER is not set")
endif

# Clear the flags from env
CPPFLAGS :=
LDFLAGS :=

# Verbose flag
ifeq ($(VERBOSE), 1)
AT =
else
AT = @
endif

# ARM ABI of the target platform
ifeq ($(TEGRA_ARMABI),)
TEGRA_ARMABI ?= aarch64-linux-gnu
endif

# Location of the target rootfs
ifeq ($(shell uname -m), aarch64)
TARGET_ROOTFS :=
else
ifeq ($(TARGET_ROOTFS),)
$(error Please specify the target rootfs path if you are cross-compiling)
endif
endif

ifeq ($(shell uname -m), aarch64)
CROSS_COMPILE :=
else
CROSS_COMPILE ?= aarch64-unknown-linux-gnu-
endif
AS             = $(AT) $(CROSS_COMPILE)as
LD             = $(AT) $(CROSS_COMPILE)ld
CC             = $(AT) $(CROSS_COMPILE)gcc
CPP            = $(AT) $(CROSS_COMPILE)g++
AR             = $(AT) $(CROSS_COMPILE)ar
NM             = $(AT) $(CROSS_COMPILE)nm
STRIP          = $(AT) $(CROSS_COMPILE)strip
OBJCOPY        = $(AT) $(CROSS_COMPILE)objcopy
OBJDUMP        = $(AT) $(CROSS_COMPILE)objdump

# Specify the logical root directory for headers and libraries.
ifneq ($(TARGET_ROOTFS),)
CPPFLAGS += --sysroot=$(TARGET_ROOTFS)
LDFLAGS += \
	-Wl,-rpath-link=$(TARGET_ROOTFS)/usr/lib \
	-Wl,-rpath-link=$(TARGET_ROOTFS)/lib/$(TEGRA_ARMABI) \
	-Wl,-rpath-link=$(TARGET_ROOTFS)/usr/lib/$(TEGRA_ARMABI)/gstreamer-1.0
endif

PKGS := opencv4
CPPFLAGS += $(shell pkg-config --cflags $(PKGS))
LDFLAGS += $(shell pkg-config --libs $(PKGS))

CPPFLAGS += -I /usr/local/cuda-$(CUDA_VER)/include
LDFLAGS += -L/usr/local/cuda-$(CUDA_VER)/lib64/ -lcudart -ldl -lcuda 

# All common header files
CPPFLAGS += -std=c++11 \
	-I"$(TARGET_ROOTFS)/usr/include/$(TEGRA_ARMABI)" \

# All common dependent libraries
LDFLAGS += \
	-lpthread \
	-L"$(TARGET_ROOTFS)/usr/lib" \
	-L"$(TARGET_ROOTFS)/usr/lib/$(TEGRA_ARMABI)" \
	-L"$(TARGET_ROOTFS)/usr/lib/$(TEGRA_ARMABI)/gstreamer-1.0" \

LDFLAGS += -L/usr/local/lib -Wl,-rpath,/usr/local/lib -lvideostitcher

APP_INSTALL_DIR ?= /usr/local/bin

APP := camerascalib

SRCS := camerascalib.cpp

OBJS := $(SRCS:.cpp=.o)

all: $(APP)

%.o: %.cpp
	@echo "Compiling: $<"
	$(CPP) $(CPPFLAGS) -c $<

$(APP): $(OBJS)
	@echo "Linking: $@"
	$(CPP) -o $@ $(OBJS) $(CPPFLAGS) $(LDFLAGS)

install: $(APP)
	cp -rv $(APP) $(APP_INSTALL_DIR)

clean:
	$(AT)rm -rf $(APP) $(OBJS)
