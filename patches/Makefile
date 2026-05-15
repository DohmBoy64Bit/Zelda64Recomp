TARGET = patches.elf

# AFA retail: MIPS patches without mm-decomp (see patches/afa/README.txt).
#   make AFA_PATCHES=1
AFA_PATCHES ?= 0

CC      ?= clang
LD      ?= ld.lld

CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
			-fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset \
			-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-variable -Wno-missing-braces -Wno-unsupported-floating-point-opt

ifeq ($(AFA_PATCHES),1)
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -I afa -I .
C_SRCS := afa/required_patches_afa.c
else
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -I dummy_headers -I ../lib/mm-decomp/include -I ../lib/mm-decomp/src -I ../lib/mm-decomp/assets -I../lib/rt64/include
C_SRCS := $(wildcard *.c)
endif

LDFLAGS  := -nostdlib -T patches.ld -T syms.ld -Map patches.map --unresolved-symbols=ignore-all --emit-relocs

C_OBJS := $(C_SRCS:.c=.o)
C_DEPS := $(C_SRCS:.c=.d)

$(TARGET): $(C_OBJS) patches.ld syms.ld
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

$(C_OBJS): %.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf $(C_OBJS) $(TARGET) $(C_DEPS)

-include $(C_DEPS)

.PHONY: clean
