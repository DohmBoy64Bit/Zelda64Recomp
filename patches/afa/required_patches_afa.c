// AFA retail patches — route cartridge DMA through recomp_load_overlays (librecomp overlays.cpp).
// Target: func_80248B70 / func_80248C50 (PI @ 0xA460 — see asm/49B20.s; Ghidra: Find_AFA_Boot_Dma_Patch_Candidates.py).
// ROM offsets match splat section_table (file offset, not 0xB0000000|off); RAM is KSEG0 0x80…… per call site.

#include "afa_patches.h"
#include "misc_funcs.h"

static u32 afa_rom_file_offset(void* cartAddr) {
    u32 rom = (u32)(uintptr_t)cartAddr;
    if (rom >= 0xB0000000U) {
        rom -= 0xB0000000U;
    }
    return rom;
}

// osEPiStartDma-style (write path) — registered @ D_802516B4 in func_8023E760 (asm/3F660.s).
RECOMP_PATCH s32 func_80248B70(s32 transferMode, void* cartAddr, void* dramAddr, s32 size) {
    (void)transferMode;
    recomp_load_overlays(afa_rom_file_offset(cartAddr), dramAddr, (u32)size);
    return 0;
}

// osEPiStartDma-style (read path) — pair @ D_802516B8.
RECOMP_PATCH s32 func_80248C50(s32 transferMode, void* cartAddr, void* dramAddr, s32 size) {
    (void)transferMode;
    recomp_load_overlays(afa_rom_file_offset(cartAddr), dramAddr, (u32)size);
    return 0;
}
