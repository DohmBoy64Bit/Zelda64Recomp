// Host exports referenced by RecompiledPatches/recomp_overlays.inl manual_patch_symbols (syms.ld).
// MM provides these via full patches.c + libc shims; AFA minimal patches only need a subset.

#include <cmath>
#include <cstring>

#include "recomp.h"
#include "librecomp/helpers.hpp"

extern "C" void __sinf_recomp(uint8_t* rdram, recomp_context* ctx) {
    float x = _arg<0, float>(rdram, ctx);
    _return(ctx, std::sin(x));
}

extern "C" void __cosf_recomp(uint8_t* rdram, recomp_context* ctx) {
    float x = _arg<0, float>(rdram, ctx);
    _return(ctx, std::cos(x));
}

extern "C" void bzero_recomp(uint8_t* rdram, recomp_context* ctx) {
    u32 dst = _arg<0, u32>(rdram, ctx);
    u32 size = _arg<1, u32>(rdram, ctx);
    std::memset(TO_PTR(void, dst), 0, size);
}

extern "C" void bcmp_recomp(uint8_t* rdram, recomp_context* ctx) {
    u32 a = _arg<0, u32>(rdram, ctx);
    u32 b = _arg<1, u32>(rdram, ctx);
    u32 size = _arg<2, u32>(rdram, ctx);
    _return(ctx, static_cast<s32>(std::memcmp(TO_PTR(void, a), TO_PTR(void, b), size)));
}
