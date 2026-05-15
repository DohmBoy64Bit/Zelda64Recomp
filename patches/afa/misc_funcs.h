#ifndef AFA_MISC_FUNCS_H
#define AFA_MISC_FUNCS_H

#include "../patch_helpers.h"

DECLARE_FUNC(void, recomp_load_overlays, u32 rom, void* ram, u32 size);
DECLARE_FUNC(void, recomp_puts, const char* data, u32 size);
DECLARE_FUNC(void, recomp_exit);

#endif
