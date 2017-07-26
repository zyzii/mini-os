#
# ARM architecture specific makefiles.
#
TOPLEVEL_DIR = $(CURDIR)/../..
OBJ_DIR=$(CURDIR)

include ../../Config.mk
include ../../minios.mk

src-y += setup.c mm.c sched.c gic.c events.c time.c
src-$(CONFIG_BALLOON) += balloon.c

ARCH_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(src-y))

ifeq ($(MINIOS_TARGET_ARCH),arm64)
ARCH_OBJS += $(OBJ_DIR)/arm64/hypercalls64.o
ARCH_OBJS += $(OBJ_DIR)/arm64/traps.o
HEAD_ARCH_OBJ = arm64/arm64.o
endif

all: $(OBJ_DIR)/$(ARCH_LIB)

# $(HEAD_ARCH_OBJ) is only built here, needed on linking
# in ../../Makefile.
$(OBJ_DIR)/$(ARCH_LIB): $(ARCH_OBJS) $(OBJ_DIR)/$(HEAD_ARCH_OBJ)
	$(AR) rv $(OBJ_DIR)/$(ARCH_LIB) $(ARCH_OBJS)

clean:
	rm -f $(OBJ_DIR)/$(ARCH_LIB) $(ARCH_OBJS) $(OBJ_DIR)/$(HEAD_ARCH_OBJ)
