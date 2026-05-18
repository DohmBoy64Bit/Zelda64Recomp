// AFA retail: PatchesLib RECOMP_PATCH bodies link at patches.ld RAMBASE (0x80801000).
// The game still dispatches PI DMA through retail VRAM (func_8023E760 table — asm/3F660.s).
// CPU N64Recomp stubs register empty bodies at 0x80248B70 / 0x80248C50 in func_map during load_overlays;
// re-point after init() so jal targets hit PatchesLib (required_patches_afa.c → recomp_load_overlays).
//
// func_8024A710 / func_80248040: direct jal from RecompiledFuncs (e.g. funcs_8.c) — add_loaded_function
// does not replace those calls; provide host bodies here (link /FORCE:MULTIPLE vs RecompiledFuncs).

#include "aero_build_config.h"

#if AEROASSAULT64_AFA_PRODUCT && AEROASSAULT64_AFA_RETAIL_PIPELINES

#include <atomic>
#include <cstdint>
#include <csetjmp>
#include <cstdio>
#include <span>
#include <thread>
#include <unordered_map>

#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/boot_log.hpp"
#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "../../../RecompiledFuncs/funcs.h"
#include "../../RecompiledPatches/funcs.h"

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
extern "C" void recomp_rom_main(uint8_t* rdram, recomp_context* ctx);
// Recompiled thread/RSP entry points (funcs_*.c); trampolines log first entry.
extern "C" void func_8023169C(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_80231584(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_80231630(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_80248D30(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_8023E6B0(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_802374B0(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_8023DF30(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_80246BB0(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_8023DCE8(uint8_t* rdram, recomp_context* ctx); // asm/3EA90.s thread entry (pri 0xFE)
extern "C" void func_8022CA30(uint8_t* rdram, recomp_context* ctx); // asm/2CE30.s event dispatcher (func_8022C838)
extern "C" void func_80241DFC(uint8_t* rdram, recomp_context* ctx); // asm/42750.s context switch

// Defined before extern "C" so hooks inside the block can call it without MSVC linkage clashes.
static recomp_func_t* afa_lookup_recomp_func(uint32_t vram) {
    if (recomp_func_t* f = recomp::overlays::try_get_function(static_cast<int32_t>(vram))) {
        return f;
    }
    for (const auto& [rom, section_index] : recomp::overlays::get_vrom_to_section_map()) {
        (void)section_index;
        if (recomp_func_t* f = recomp::overlays::get_func_by_section_rom_function_vram(rom, vram)) {
            return f;
        }
    }
    switch (vram) {
    case 0x8023DCE8u:
        return func_8023DCE8;
    case 0x80248D30u:
        return func_80248D30;
    case 0x8023169Cu:
        return func_8023169C;
    case 0x80231584u:
        return func_80231584;
    case 0x80231630u:
        return func_80231630;
    case 0x8022CA30u:
        return func_8022CA30;
    default:
        return nullptr;
    }
}

extern "C" {

// MIPS 32-bit VRAM pointers must be sign-extended for MEM_* (see recomp.h / generated S32(lui…)).
static gpr afa_vaddr(uint32_t vaddr) {
    return static_cast<gpr>(static_cast<int32_t>(vaddr));
}

// N64Recomp datasyms.toml maps game VRAM into the 0x80A0/0x8027 band — full pin list: AFA_PORT.md § datasyms.
// Entire OS globals slab: game 0x80251680.. was emitted as 0x809F9960.. (constant +0x7A82E0 slip).
static gpr afa_fixup_datasyms_809f99xx(gpr p) {
    constexpr uint32_t k_wrong_base = 0x809F9960u;
    constexpr uint32_t k_right_base = 0x80251680u;
    constexpr uint32_t k_span = 0x800u; // D_80251680 .. ~D_80251E80
    const uint32_t lo = static_cast<uint32_t>(p);
    if (lo >= k_wrong_base && lo < k_wrong_base + k_span) {
        return afa_vaddr(k_right_base + (lo - k_wrong_base));
    }
    return p;
}

static gpr afa_fixup_list_head_ptr(gpr p) {
    return afa_fixup_datasyms_809f99xx(p);
}

// N64Recomp datasyms.toml VRAM slips (game asm uses left column).
static gpr afa_fixup_relocated_vram(gpr p) {
    p = afa_fixup_datasyms_809f99xx(p);
    const uint32_t lo = static_cast<uint32_t>(p);
    switch (lo) {
    case 0x80275398u:
        return afa_vaddr(0x80281068u); // D_80281068 — asm/31B30.s
    case 0x80278FD0u:
        return afa_vaddr(0x80284CA0u); // D_80284CA0 — asm/3F660.s DMA thread TCB
    case 0x80279180u:
        return afa_vaddr(0x80284E50u); // D_80284E50 — asm/3F660.s DMA stack base
    case 0x802751E8u:
        return afa_vaddr(0x80280EB8u); // D_80280EB8 — asm/31B30.s (func_8023169C)
    case 0x802851E8u:
        return afa_vaddr(0x80280EB8u); // recomp slip (lui 0x8028 + addiu 0x51E8)
    case 0x80275038u:
        return afa_vaddr(0x80280D08u); // D_80280D08 — asm/31B30.s
    case 0x80285038u:
        return afa_vaddr(0x80280D08u); // recomp slip (lui 0x8028 + addiu 0x5038)
    case 0x8026EE90u:
        return afa_vaddr(0x8027AB60u); // D_8027AB60 — stack base in asm/31B30.s sp+0x10
    case 0x80267EC0u:
        return afa_vaddr(0x80273B90u); // D_80273B90 — RSP task queue (func_802207E8)
    case 0x80267EE0u:
        return afa_vaddr(0x80273BB0u); // D_80273BB0 — asm/20F50.s
    case 0x80280CB0u:
        return afa_vaddr(0x80274FE0u); // func_8023169C → func_8023E6B0 a1 (lui 0x8027 + 0x4FE0)
    case 0x80280C90u:
        return afa_vaddr(0x80274FC0u); // func_8023169C → func_8023E6B0 a2 (lui 0x8027 + 0x4FC0)
    case 0x80280C30u:
        return afa_vaddr(0x8027C900u); // D_8027C900 — func_8023169C osCreateMesgQueue (recomp lui 0x8028+0xC30)
    case 0x80280C28u:
        return afa_vaddr(0x8027C958u); // VI retrace msg[1] — func_8023169C (asm addiu 0x4F58)
    case 0x80280C910u:
        return afa_vaddr(0x8027C940u); // func_80231A40 osSetEventMesg mq (recomp lui 0x8028+0xC10)
    case 0x802857F0u:
        return afa_vaddr(0x802757F0u); // func_80230F68 osRecvMesg case 2 (recomp lui 0x8028+0x57F0)
    case 0x80A00584u:
        return afa_vaddr(0x80280C70u); // D_80280C70 audio OSMesgQueue — asm/31B30.s (RecompiledFuncs lui 0x80A0+0x584)
    default:
        p = afa_fixup_datasyms_809f99xx(p);
        return p;
    }
}

static gpr afa_fixup_main_thread_tcb(gpr p) {
    return afa_fixup_relocated_vram(p);
}

static gpr afa_fixup_thread_tcb(gpr p) {
    return afa_fixup_relocated_vram(p);
}

static gpr afa_fixup_current_thread_ptr(gpr p) {
    return afa_fixup_datasyms_809f99xx(p);
}

static bool afa_is_game_vram_ptr(gpr p) {
    const uint32_t lo = static_cast<uint32_t>(p);
    return lo >= 0x80200000u && lo < 0x80800000u;
}

// Scheduler globals live at 0x802516xx (D_802516D8/E0/DC); do not treat as a thread TCB.
static bool afa_is_plausible_tcb(gpr p) {
    const uint32_t lo = static_cast<uint32_t>(p);
    if (!afa_is_game_vram_ptr(p)) {
        return false;
    }
    if (lo >= 0x80251600u && lo < 0x80251800u) {
        return false;
    }
    return true;
}

// func_80241EFC (asm/42750.s) expects a0 = address of a list-head word (&D_802516D8 or &mq->mtQueue).
static bool afa_is_scheduler_list_head_ptr(gpr p) {
    if (p == 0) {
        return false;
    }
    if (!afa_is_game_vram_ptr(p)) {
        return false;
    }
    if (afa_is_plausible_tcb(p)) {
        return false;
    }
    return true;
}

// lib/mm-decomp createmesgqueue.c: mq->mtQueue = &__osThreadTail.next; *mq->mtQueue == 0x802516D0.
static bool afa_is_wait_list_head_ptr(uint8_t* rdram, gpr p) {
    (void)rdram;
    if (afa_is_scheduler_list_head_ptr(p)) {
        return true;
    }
    if (!afa_is_game_vram_ptr(p)) {
        return false;
    }
    gpr link = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, p)));
    link = afa_fixup_relocated_vram(link);
    return link == afa_vaddr(0x802516D0u);
}

// TCB+0x11C resume PC must be a registered recomp function, not data (D_802420D0 eret trampoline in .data).
static bool afa_is_recomp_thread_entry_pc(uint32_t vram) {
    if (!afa_is_game_vram_ptr(static_cast<gpr>(static_cast<int32_t>(vram)))) {
        return false;
    }
    if (vram == 0x802420D0u) {
        return false;
    }
    return afa_lookup_recomp_func(vram) != nullptr;
}

static gpr afa_main_thread_tcb() {
    // main() creates the game thread at D_80281068 (asm/31B30.s 80231214); D_80280EB8 is the audio TCB.
    return afa_vaddr(0x80281068u);
}

static gpr afa_get_current_thread_tcb(uint8_t* rdram) {
    (void)rdram;
    const gpr cur_ptr = afa_fixup_current_thread_ptr(afa_vaddr(0x802516E0u));
    gpr tcb = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, cur_ptr)));
    tcb = afa_fixup_thread_tcb(tcb);
    if (!afa_is_plausible_tcb(tcb)) {
        tcb = afa_main_thread_tcb();
        MEM_W(0, cur_ptr) = static_cast<int32_t>(static_cast<uint32_t>(tcb));
    }
    return tcb;
}

// Cooperative scheduler: retail func_80241DFC saves GPRs then erets via TCB+0x11C (asm/42750.s L_80241E48).
// jb  = first entry from func_80241F54; dfc_jb = return from nested dispatch back to host jal caller.
struct AfaThreadContinuation {
    jmp_buf jb{};
    jmp_buf dfc_jb{};
    bool jmp_active = false;
    bool dfc_active = false;
    bool suspended = false;
};

static std::unordered_map<uint32_t, AfaThreadContinuation> afa_thread_cont;

static AfaThreadContinuation& afa_thread_cont_for(gpr tcb) {
    return afa_thread_cont[static_cast<uint32_t>(tcb)];
}

static void afa_thread_cont_reset(gpr tcb) {
    auto& cont = afa_thread_cont_for(tcb);
    cont.jmp_active = false;
    cont.dfc_active = false;
    cont.suspended = false;
}

static void afa_start_thread_cpu(uint8_t* rdram, recomp_context* ctx, gpr tcb);
static void afa_dispatch_until_self(uint8_t* rdram, recomp_context* ctx, gpr self);
static thread_local recomp_context* g_afa_active_recomp_ctx = nullptr;

static uint32_t afa_default_thread_entry_pc(gpr tcb) {
    switch (static_cast<uint32_t>(tcb)) {
    case 0x80284CA0u:
        return 0x80248D30u;
    case 0x80280EB8u:
        return 0x80231584u;
    case 0x80280D08u:
        return 0x80231630u;
    case 0x80281068u:
        return 0x8023169Cu;
    case 0x802812C8u:
        return 0x8022CA30u;
    case 0x80277D20u:
        return 0x8023DCE8u;
    default:
        return 0x8023169Cu;
    }
}

static uint32_t afa_resolve_thread_dispatch_pc(uint8_t* rdram, gpr tcb) {
    (void)rdram;
    uint32_t pc = static_cast<uint32_t>(MEM_W(0x11C, tcb));
    if (pc == 0u || pc == 0x802420D0u || !afa_is_game_vram_ptr(static_cast<gpr>(static_cast<int32_t>(pc)))) {
        pc = afa_default_thread_entry_pc(tcb);
    }
    if (afa_lookup_recomp_func(pc) != nullptr) {
        return pc;
    }
    return afa_default_thread_entry_pc(tcb);
}

static void afa_load_context_from_tcb(uint8_t* rdram, recomp_context* ctx, gpr tcb) {
    (void)rdram;
    cop0_status_write(ctx, MEM_W(0x118, tcb));
    ctx->hi = LD(0x108, tcb);
    ctx->lo = LD(0x110, tcb);

    ctx->r1 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x20, tcb))));
    ctx->r2 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x28, tcb))));
    ctx->r3 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x30, tcb))));
    ctx->r4 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x38, tcb))));
    ctx->r5 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x40, tcb))));
    ctx->r6 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x48, tcb))));
    ctx->r7 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x50, tcb))));
    ctx->r8 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x58, tcb))));
    ctx->r9 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x60, tcb))));
    ctx->r10 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x68, tcb))));
    ctx->r11 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x70, tcb))));
    ctx->r12 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x78, tcb))));
    ctx->r13 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x80, tcb))));
    ctx->r14 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x88, tcb))));
    ctx->r15 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x90, tcb))));
    ctx->r16 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x98, tcb))));
    ctx->r17 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xA0, tcb))));
    ctx->r18 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xA8, tcb))));
    ctx->r19 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xB0, tcb))));
    ctx->r20 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xB8, tcb))));
    ctx->r21 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xC0, tcb))));
    ctx->r22 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xC8, tcb))));
    ctx->r23 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xD0, tcb))));
    ctx->r24 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xD8, tcb))));
    ctx->r25 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xE0, tcb))));
    ctx->r28 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xE8, tcb))));
    ctx->r29 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xF0, tcb))));
    ctx->r30 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0xF8, tcb))));
    ctx->r31 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(LD(0x100, tcb))));

    if (MEM_W(0x20, tcb) == 0 && MEM_W(0x28, tcb) == 0) {
        const gpr init_sp = static_cast<gpr>(static_cast<int32_t>(MEM_W(0xF0, tcb)));
        if (afa_is_game_vram_ptr(init_sp)) {
            ctx->r29 = init_sp;
        }
        const gpr init_a0 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0x38, tcb)));
        if (init_a0 != 0) {
            ctx->r4 = afa_fixup_relocated_vram(init_a0);
        }
        ctx->r31 = 0;
    }

    if (MEM_W(0x18, tcb) != 0) {
        set_cop1_cs(static_cast<uint32_t>(MEM_W(0x12C, tcb)));
        ctx->f20.u64 = LD(0x180, tcb);
        ctx->f22.u64 = LD(0x188, tcb);
        ctx->f24.u64 = LD(0x190, tcb);
        ctx->f26.u64 = LD(0x198, tcb);
        ctx->f28.u64 = LD(0x1A0, tcb);
        ctx->f30.u64 = LD(0x1A8, tcb);
    }

    if (!afa_is_game_vram_ptr(ctx->r29)) {
        const gpr boot_sp = static_cast<gpr>(static_cast<int32_t>(MEM_W(0xF0, tcb)));
        ctx->r29 = afa_is_game_vram_ptr(boot_sp) ? boot_sp : afa_vaddr(0x8027CB60u);
    }
}

static void afa_run_thread_until_yield(uint8_t* rdram, recomp_context* ctx, gpr tcb);

static gpr afa_resolve_thread_arg(uint8_t* rdram, gpr a0) {
    if (a0 == 0) {
        return afa_get_current_thread_tcb(rdram);
    }
    gpr tcb = afa_fixup_thread_tcb(a0);
    if (!afa_is_plausible_tcb(tcb)) {
        tcb = afa_main_thread_tcb();
    }
    return tcb;
}

// Map cart physical address to .z64 byte offset (game uses 0x1FC007FC in asm/3F350.s).
// 0x1FC00000 region: SDK PIF_ROM_START in lib/mm-decomp/include/PR/rcp.h; alias of cart ROM low pages.
static uint32_t afa_cart_phys_to_rom_offset(uint32_t phys) {
    phys &= 0x1FFFFFFFu;
    if (phys >= 0x1FC00000u) {
        return phys - 0x1FC00000u;
    }
    if (phys >= recomp::rom_base) {
        return phys - recomp::rom_base;
    }
    return UINT32_MAX;
}

static void afa_copy_words_from_802417E0(uint8_t* rdram, gpr dest_base) {
    (void)rdram;
    const gpr src = afa_vaddr(0x802417E0u);
    MEM_W(0, dest_base) = MEM_W(0, src);
    MEM_W(4, dest_base) = MEM_W(4, src);
    MEM_W(8, dest_base) = MEM_W(8, src);
    MEM_W(0xC, dest_base) = MEM_W(0xC, src);
}

// Game: asm/4B540.s — lw from D_A4800018 (SI_STATUS_REG @ 0xA4800018, undefined_syms_auto.txt).
RECOMP_FUNC void func_8024A710(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0; // (status & 3) == 0 → SI idle (asm .L8024A730).
}

// Game: asm/48FE0.s — poll SI then lw from (a0|0xA0000000) into *a1.
RECOMP_FUNC void func_80248040(uint8_t* rdram, recomp_context* ctx) {
    func_8024A710(rdram, ctx);
    if (ctx->r2 != 0) {
        ctx->r2 = static_cast<gpr>(static_cast<int32_t>(-1));
        return;
    }

    const uint32_t dev_k1 = static_cast<uint32_t>(ctx->r4) | 0xA0000000u;
    const uint32_t phys = dev_k1 & 0x1FFFFFFFu;
    const std::span<const uint8_t> rom = recomp::get_rom();
    const uint32_t off = afa_cart_phys_to_rom_offset(phys);
    if (!recomp::is_rom_loaded() || off == UINT32_MAX || off + 4 > rom.size()) {
        ctx->r2 = static_cast<gpr>(static_cast<int32_t>(-1));
        return;
    }

    const uint32_t word =
        (static_cast<uint32_t>(rom[off + 0]) << 24) |
        (static_cast<uint32_t>(rom[off + 1]) << 16) |
        (static_cast<uint32_t>(rom[off + 2]) << 8) |
        static_cast<uint32_t>(rom[off + 3]);
    MEM_W(0, ctx->r5) = static_cast<int32_t>(word);
    ctx->r2 = 0;
}

// Game: asm/48FE0.s — poll SI then sw to (a0|0xA0000000); cart MMIO, not RDRAM.
RECOMP_FUNC void func_80248090(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    func_8024A710(rdram, ctx);
    if (ctx->r2 != 0) {
        ctx->r2 = static_cast<gpr>(static_cast<int32_t>(-1));
        return;
    }
    // Value in a1 is PI/bus setup; host has no cart HW register file to update.
    ctx->r2 = 0;
}

// True for the runnable-thread list at D_802516D8; false for OSMesgQueue &mtQueue (queue+0).
static bool afa_is_run_queue_head_ptr(gpr list_head_ptr) {
    return static_cast<uint32_t>(list_head_ptr) ==
           static_cast<uint32_t>(afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u)));
}

// Game: asm/42750.s func_80241EFC — sorted insert; a0 = &D_802516D8, a1 = OSThread*.
// Walks from *a0 (sentinel D_802516D0 or thread chain); slt on +0x4; links prev->next = ins.
RECOMP_FUNC void func_80241EFC(uint8_t* rdram, recomp_context* ctx) {
    // a0 = list head (D_802516D8) or OSMesgQueue &mtQueue at queue+0 — asm/42750.s.
    const gpr list_head_ptr = afa_fixup_relocated_vram(ctx->r4);
    const gpr ins = afa_fixup_thread_tcb(ctx->r5);
    if (!afa_is_plausible_tcb(ins)) {
        return;
    }
    // Mesg-queue heads (e.g. D_8027C900) pass afa_is_wait_list_head_ptr (*head == D_802516D0) but
    // fail afa_is_scheduler_list_head_ptr — that helper rejects any plausible-Tcb-shaped address.
    if (!afa_is_run_queue_head_ptr(list_head_ptr) &&
        !afa_is_wait_list_head_ptr(rdram, list_head_ptr)) {
        return;
    }

    const int32_t ins_pri = static_cast<int32_t>(MEM_W(4, ins));

    // lib/mm-decomp/src/libultra/os/createmesgqueue.c: mq->mtQueue = (OSThread*)&__osThreadTail.next.
    // *(&mq->mtQueue) is always the tail link cell (D_802516D0); waiters hang off tail.next, not *mq->mtQueue.
    if (!afa_is_run_queue_head_ptr(list_head_ptr)) {
        gpr link = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head_ptr)));
        link = afa_fixup_relocated_vram(link);
        if (link == 0) {
            return;
        }
        gpr first = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, link)));
        first = afa_fixup_relocated_vram(first);
        if (first == 0) {
            MEM_W(0, link) = static_cast<int32_t>(static_cast<uint32_t>(ins));
            MEM_W(0, ins) = 0;
            MEM_W(8, ins) = static_cast<int32_t>(static_cast<uint32_t>(list_head_ptr));
            static int afa_mesg_ins_log = 0;
            if (afa_mesg_ins_log < 6) {
                ++afa_mesg_ins_log;
                recomp_boot_logf("[boot] afa: func_80241EFC mesg insert head=0x%08X tcb=0x%08X tail->next=0x%08X",
                                 static_cast<uint32_t>(list_head_ptr), static_cast<uint32_t>(ins),
                                 static_cast<uint32_t>(ins));
            }
            return;
        }
        gpr prev = link;
        gpr cur = first;
        int walk_guard = 0;
        while (true) {
            if (++walk_guard > 256) {
                recomp_boot_logf("[boot] afa: func_80241EFC mesg walk guard head=0x%08X ins=0x%08X",
                                 static_cast<uint32_t>(list_head_ptr), static_cast<uint32_t>(ins));
                break;
            }
            if (static_cast<int32_t>(MEM_W(4, cur)) < ins_pri) {
                break;
            }
            prev = cur;
            cur = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, cur)));
            if (cur == 0) {
                break;
            }
            cur = afa_fixup_relocated_vram(cur);
        }
        const gpr next = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, prev)));
        MEM_W(0, ins) = static_cast<int32_t>(static_cast<uint32_t>(next));
        MEM_W(0, prev) = static_cast<int32_t>(static_cast<uint32_t>(ins));
        MEM_W(8, ins) = static_cast<int32_t>(static_cast<uint32_t>(list_head_ptr));
        return;
    }

    gpr prev = list_head_ptr;
    gpr cur = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head_ptr)));
    cur = afa_fixup_relocated_vram(cur);
    if (cur == 0) {
        MEM_W(0, ins) = 0;
        MEM_W(0, prev) = static_cast<int32_t>(static_cast<uint32_t>(ins));
        MEM_W(8, ins) = static_cast<int32_t>(static_cast<uint32_t>(list_head_ptr));
        return;
    }

    if (static_cast<int32_t>(MEM_W(4, cur)) < ins_pri) {
        // Game: bnez slt -> .L80241F30 with prev still list_head_ptr (asm/42750.s 80241F10).
    } else {
        int walk_guard = 0;
        while (true) {
            if (++walk_guard > 256) {
                recomp_boot_logf("[boot] afa: func_80241EFC walk guard head=0x%08X ins=0x%08X",
                                 static_cast<uint32_t>(list_head_ptr), static_cast<uint32_t>(ins));
                break;
            }
            prev = cur;
            cur = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, cur)));
            if (cur == 0) {
                break;
            }
            cur = afa_fixup_relocated_vram(cur);
            if (static_cast<int32_t>(MEM_W(4, cur)) < ins_pri) {
                break;
            }
        }
    }

    const gpr next = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, prev)));
    MEM_W(0, ins) = static_cast<int32_t>(static_cast<uint32_t>(next));
    MEM_W(0, prev) = static_cast<int32_t>(static_cast<uint32_t>(ins));
    MEM_W(8, ins) = static_cast<int32_t>(static_cast<uint32_t>(list_head_ptr));
}

// Game: asm/381C0.s — init thread control block (a0=TCB, a1=priority, a2=entry, a3=arg).
RECOMP_FUNC void func_80237210(uint8_t* rdram, recomp_context* ctx) {
    const gpr tcb = afa_fixup_thread_tcb(ctx->r4);
    const gpr entry = ctx->r6;
    // Stack from caller sp+0x10 when set (func_8023E6B0); else main stack (asm/31B30.s D_8027CB60).
    gpr stack_top = afa_vaddr(0x8027CB60u);
    if (static_cast<uint32_t>(tcb) == 0x80284CA0u) {
        stack_top = ADD32(afa_vaddr(0x80284E50u), 0x1000); // asm/3F660.s t9 = D_80284E50 + 0x1000
    } else if (static_cast<uint32_t>(tcb) == 0x80280EB8u) {
        stack_top = afa_vaddr(0x8027AB60u); // D_8027AB60 — asm/31B30.s func_8023169C audio thread
    } else if (static_cast<uint32_t>(tcb) == 0x80280D08u) {
        stack_top = afa_vaddr(0x80280B60u); // D_80280B60 — asm/31B30.s func_8023169C VI thread
    } else if (static_cast<uint32_t>(tcb) == 0x80281068u) {
        stack_top = afa_vaddr(0x8027CB60u); // D_8027CB60 — asm/31B30.s main() osCreateThread
    } else if (static_cast<uint32_t>(tcb) == 0x802812C8u) {
        // D_80281218+0xB0 TCB; stack from caller sp+0x10 (asm/381C0.s) = D_80283518 — asm/31B30.s func_80230FF8
        stack_top = afa_vaddr(0x80283518u);
    }

    MEM_W(0x14, tcb) = ctx->r5; // a1 osPriority — asm/381C0.s ADEE0014
    // +0x4 from caller sp+0x14 (asm/381C0.s AF380004 / lw 0x3C(sp) after -0x28 prologue).
    gpr sched_counter = 0;
    if (afa_is_game_vram_ptr(ctx->r29)) {
        sched_counter = static_cast<gpr>(static_cast<int32_t>(MEM_W(0x14, ctx->r29)));
    }
    MEM_W(0x4, tcb) = static_cast<int32_t>(static_cast<uint32_t>(sched_counter));
    MEM_W(0, tcb) = 0;
    MEM_W(8, tcb) = 0;
    MEM_W(0x11C, tcb) = static_cast<int32_t>(static_cast<uint32_t>(entry));

    const gpr trampoline = afa_vaddr(0x802420D0u); // eret trampoline — asm/42750.s
    MEM_W(0x100, tcb) = static_cast<int32_t>(static_cast<uint32_t>(S32(SIGNED(trampoline) >> 31)));
    MEM_W(0x104, tcb) = static_cast<int32_t>(static_cast<uint32_t>(trampoline));

    MEM_W(0x118, tcb) = 0xFF03;
    MEM_W(0x128, tcb) = 0;
    MEM_W(0x12C, tcb) = 0x1000800;
    MEM_W(0x18, tcb) = 0;
    // Initial SP must match SD/LD at +0xF0 (afa_save_thread_context / afa_load_context_from_tcb).
    SD(static_cast<uint64_t>(static_cast<uint32_t>(stack_top)), 0xF0, tcb);
    // Thread entry argument (a3) — restored as $a0 on first dispatch (e.g. PI queue D_802516A0 for func_80248D30).
    MEM_W(0x38, tcb) = static_cast<int32_t>(static_cast<uint32_t>(afa_fixup_relocated_vram(ctx->r7)));
    // Do not downgrade mesg-blocked (8) or already-started (2/4) threads — asm/31B30.s may re-hit init after bad resume.
    const uint32_t prior_state = static_cast<uint32_t>(MEM_HU(0x10, tcb));
    if (prior_state != 8u && prior_state != 2u && prior_state != 4u) {
        MEM_H(0x10, tcb) = 1; // asm/381C0.s A5180010 — created, not yet on run queue
        afa_thread_cont_reset(tcb);
    }
    MEM_H(0x12, tcb) = 0;

    func_80241760(rdram, ctx);
    const gpr ie = ctx->r2;
    const gpr prev_tcb = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, afa_vaddr(0x802516DCu))));
    MEM_W(0xC, tcb) = static_cast<int32_t>(static_cast<uint32_t>(prev_tcb)); // asm/381C0.s AD6A000C
    ctx->r4 = ie;
    func_80241780(rdram, ctx);
    MEM_W(0, afa_vaddr(0x802516DCu)) = static_cast<int32_t>(static_cast<uint32_t>(tcb)); // asm/381C0.s AC3916DC

    static int afa_enqueue_log_count = 0;
    if (afa_enqueue_log_count < 8) {
        ++afa_enqueue_log_count;
        recomp_boot_logf("[boot] afa: func_80237210 init tcb=0x%08X +4=%d state=1 (enqueue via func_80237360)",
                         static_cast<uint32_t>(tcb), static_cast<int32_t>(sched_counter));
    }
}

// Game: asm/3F660.s / func_8023E6B0 — PI manager globals at D_802516A0 (recomp sw via 0x80A0-0x6680 lands in wrong RDRAM).
static void afa_pi_manager_fixup_globals(uint8_t* rdram, gpr callback_from_a1) {
    (void)rdram;
    gpr cb = afa_fixup_relocated_vram(callback_from_a1);
    if (!afa_is_game_vram_ptr(cb)) {
        cb = 0;
    }
    const gpr d_802516a0 = afa_vaddr(0x802516A0u);
    MEM_W(0, d_802516a0) = 1;
    MEM_W(4, d_802516a0) = static_cast<int32_t>(static_cast<uint32_t>(afa_vaddr(0x80284CA0u)));
    MEM_W(8, d_802516a0) = static_cast<int32_t>(static_cast<uint32_t>(cb));
    MEM_W(0xC, d_802516a0) = static_cast<int32_t>(static_cast<uint32_t>(afa_vaddr(0x80285E50u))); // D_802516AC — asm/3F660.s

    const gpr d_802516b0 = afa_vaddr(0x802516B0u);
    MEM_W(0, d_802516b0) = static_cast<int32_t>(static_cast<uint32_t>(afa_vaddr(0x80285F28u))); // asm/3F660.s
    MEM_W(4, d_802516b0) = static_cast<int32_t>(static_cast<uint32_t>(afa_vaddr(0x80248B70u)));
    MEM_W(8, d_802516b0) = static_cast<int32_t>(static_cast<uint32_t>(afa_vaddr(0x80248C50u)));
}

// lib/mm-decomp createmesgqueue.c: mq->mtQueue = (OSThread*)&__osThreadTail.next (D_802516D0).
static void afa_ensure_mesg_mtqueue(uint8_t* rdram, gpr mq) {
    (void)rdram;
    if (!afa_is_game_vram_ptr(mq)) {
        return;
    }
    gpr mt = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, mq)));
    mt = afa_fixup_relocated_vram(mt);
    const gpr tail = afa_vaddr(0x802516D0u);
    if (mt == 0 || afa_is_plausible_tcb(mt)) {
        MEM_W(0, mq) = static_cast<int32_t>(static_cast<uint32_t>(tail));
        if (static_cast<int32_t>(MEM_W(4, mq)) == 0) {
            MEM_W(4, mq) = static_cast<int32_t>(static_cast<uint32_t>(tail));
        }
    }
}

// asm/2CE30.s func_8022CA30 recv-loops on D_80281258 but never calls osCreateMesgQueue on it.
static void afa_init_event_mesg_queue(uint8_t* rdram) {
    (void)rdram;
    const gpr mq = afa_vaddr(0x80281258u);
    const gpr thread_tail = afa_vaddr(0x802516D0u);
    const gpr msg = afa_vaddr(0x80281290u); // asm/data/57D20.bss.s after D_80281258 (+0x38)
    MEM_W(0, mq) = static_cast<int32_t>(static_cast<uint32_t>(thread_tail));
    MEM_W(4, mq) = static_cast<int32_t>(static_cast<uint32_t>(thread_tail));
    MEM_W(8, mq) = 0;
    MEM_W(0xC, mq) = 0;
    MEM_W(0x10, mq) = 8;
    MEM_W(0x14, mq) = static_cast<int32_t>(static_cast<uint32_t>(msg));
}

// asm/2CE30.s func_8022CA30: recv on D_80281258 with no retail osCreateMesgQueue — re-init if mtQueue corrupt.
static void afa_ensure_event_mesg_queue(uint8_t* rdram, gpr mq) {
    mq = afa_fixup_relocated_vram(mq);
    if (static_cast<uint32_t>(mq) != 0x80281258u || !afa_is_game_vram_ptr(mq)) {
        return;
    }
    const bool needs_init = static_cast<int32_t>(MEM_W(0x10, mq)) <= 0 ||
                            static_cast<int32_t>(MEM_W(0x14, mq)) == 0 ||
                            !afa_is_wait_list_head_ptr(rdram, mq);
    if (!needs_init) {
        return;
    }
    afa_init_event_mesg_queue(rdram);
    static bool afa_evq_init_log = false;
    if (!afa_evq_init_log) {
        afa_evq_init_log = true;
        recomp_boot_log("[boot] afa: lazy osCreateMesgQueue D_80281258 (event thread, asm/2CE30.s)");
    }
}

static void afa_ensure_mesg_queue_ready(uint8_t* rdram, gpr mq) {
    mq = afa_fixup_relocated_vram(mq);
    if (!afa_is_game_vram_ptr(mq)) {
        return;
    }
    if (static_cast<uint32_t>(mq) == 0x80281258u) {
        afa_ensure_event_mesg_queue(rdram, mq);
        return;
    }
    if (!afa_is_wait_list_head_ptr(rdram, mq)) {
        afa_ensure_mesg_mtqueue(rdram, mq);
    }
}

// lib/mm-decomp createmesgqueue.c: wait list head is &mq->mtQueue; *mtQueue == __osThreadTail (D_802516D0).
static bool afa_is_mesg_queue_wait_head(uint8_t* rdram, gpr p) {
    p = afa_fixup_relocated_vram(p);
    if (static_cast<uint32_t>(p) == 0x80281258u) {
        afa_ensure_event_mesg_queue(rdram, p);
    }
    return afa_is_wait_list_head_ptr(rdram, p);
}

// Host replacement for func_802371E0 (asm/380D0.s): osCreateMesgQueue — lib/mm-decomp/src/libultra/os/createmesgqueue.c
RECOMP_FUNC void func_802371E0(uint8_t* rdram, recomp_context* ctx) {
    gpr mq = afa_fixup_relocated_vram(ctx->r4);
    const gpr msg = afa_fixup_relocated_vram(ctx->r5);
    const int32_t count = static_cast<int32_t>(ctx->r6);
    const gpr thread_tail = afa_vaddr(0x802516D0u); // __osThreadTail.next sentinel (same as recompiled func_802371E0)

    if (!afa_is_game_vram_ptr(mq)) {
        return;
    }

    MEM_W(0, mq) = static_cast<int32_t>(static_cast<uint32_t>(thread_tail));
    MEM_W(4, mq) = static_cast<int32_t>(static_cast<uint32_t>(thread_tail));
    // createmesgqueue.c: mq->mtQueue points at tail.next — must start empty for recv waiters.
    MEM_W(0, thread_tail) = 0;
    MEM_W(8, mq) = 0;
    MEM_W(0xC, mq) = 0;
    MEM_W(0x10, mq) = count;
    MEM_W(0x14, mq) = static_cast<int32_t>(static_cast<uint32_t>(msg));

    static int afa_create_mq_log = 0;
    if (afa_create_mq_log < 6) {
        ++afa_create_mq_log;
        recomp_boot_logf("[boot] afa: func_802371E0 osCreateMesgQueue mq=0x%08X msg=0x%08X count=%d",
                         static_cast<uint32_t>(mq), static_cast<uint32_t>(msg), count);
    }
}

// Host replacement for func_8023E6B0 (funcs_6.c): recomp stores OS globals via 0x80A0-* into wrong RDRAM.
RECOMP_FUNC void func_8023E6B0(uint8_t* rdram, recomp_context* ctx) {
    const gpr pri_max = ctx->r4;
    const gpr callback_a1 = afa_fixup_relocated_vram(ctx->r5);
    const gpr mesg_a2 = afa_fixup_relocated_vram(ctx->r6);
    const gpr old_sp = ctx->r29;
    const gpr old_ra = ctx->r31;

    if (MEM_W(0, afa_vaddr(0x802516A0u)) != 0) {
        return;
    }

    ctx->r29 = ADD32(old_sp, -0x30);
    MEM_W(0x1C, ctx->r29) = old_ra;

    ctx->r4 = callback_a1;
    ctx->r5 = mesg_a2;
    ctx->r6 = ctx->r7;
    func_802371E0(rdram, ctx); // asm/380D0.s osCreateMesgQueue (link, not func_map)

    ctx->r4 = afa_vaddr(0x80285E50u); // D_80285E50 — asm/3F660.s (not recomp -0x5E80 slip)
    ctx->r5 = afa_vaddr(0x80285E68u); // D_80285E68
    ctx->r6 = 1;
    func_802371E0(rdram, ctx);

    const gpr flag = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, afa_vaddr(0x80251A60u)))); // D_80251A60
    if (flag == 0) {
        func_80246BB0(rdram, ctx);
    }

    ctx->r4 = 8;
    ctx->r5 = afa_vaddr(0x80285E50u); // D_80285E50 — asm/3F660.s jal func_8023DF30
    ctx->r6 = static_cast<gpr>(static_cast<int32_t>(0x22222222u));
    func_8023DF30(rdram, ctx);

    func_80241760(rdram, ctx);
    const gpr saved_ie = ctx->r2;

    afa_pi_manager_fixup_globals(rdram, callback_a1);

    if (afa_is_game_vram_ptr(ctx->r29)) {
        MEM_W(0x14, ctx->r29) = static_cast<int32_t>(static_cast<uint32_t>(pri_max));
    }

    ctx->r4 = afa_vaddr(0x80284CA0u);
    ctx->r5 = 0;
    ctx->r6 = afa_vaddr(0x80248D30u);
    ctx->r7 = afa_vaddr(0x802516A0u);
    func_80237210(rdram, ctx);

    ctx->r4 = afa_vaddr(0x80284CA0u);
    func_80237360(rdram, ctx);

    ctx->r4 = saved_ie;
    func_80241780(rdram, ctx);

    ctx->r31 = MEM_W(0x1C, ctx->r29);
    ctx->r29 = old_sp;

    static int afa_pi_init_log = 0;
    if (afa_pi_init_log < 2) {
        ++afa_pi_init_log;
        recomp_boot_logf("[boot] afa: func_8023E6B0 PI globals fixed D_802516A8=0x%08X",
                         static_cast<uint32_t>(static_cast<int32_t>(MEM_W(8, afa_vaddr(0x802516A0u)))));
    }
}

static void afa_os_wake_recv_waiters(uint8_t* rdram, recomp_context* ctx, gpr queue);
static void afa_drain_pending_vi_retrace(uint8_t* rdram, recomp_context* ctx);
static void afa_start_thread_cpu(uint8_t* rdram, recomp_context* ctx, gpr tcb);
static gpr afa_mesg_queue_first_waiter(uint8_t* rdram, gpr queue);
static gpr afa_find_mesg_recv_waiter(uint8_t* rdram, gpr queue);
static void afa_ensure_mesg_mtqueue(uint8_t* rdram, gpr mq);

// Host replacement for func_802374B0 (asm/38310.s): osRecvMesg — full dequeue per libultra OSMesgQueue layout.
// Queue: +0 waitList, +4 (unused link), +8 validCount, +0xC readIndex, +0x10 msgCount, +0x14 msgArray.
RECOMP_FUNC void func_802374B0(uint8_t* rdram, recomp_context* ctx) {
    const gpr mesg_out = ctx->r5;
    const gpr block = ctx->r6;
    gpr queue = afa_fixup_relocated_vram(ctx->r4);

    func_80241760(rdram, ctx);
    const gpr saved_ie = ctx->r2;

    auto finish = [&](gpr ret) {
        ctx->r4 = saved_ie;
        func_80241780(rdram, ctx);
        ctx->r2 = ret;
    };

    afa_ensure_mesg_queue_ready(rdram, queue);

    if (!afa_is_game_vram_ptr(queue)) {
        static int afa_osrecv_empty_log = 0;
        if (afa_osrecv_empty_log < 4) {
            ++afa_osrecv_empty_log;
            recomp_boot_logf("[boot] afa: func_802374B0 idle (bad queue=0x%08X)", static_cast<uint32_t>(queue));
        }
        if (block == 0) {
            finish(static_cast<gpr>(static_cast<int32_t>(-1)));
            return;
        }
        finish(0);
        return;
    }

    // asm/38310.s L_802374E4..L_80237518: spin until validCount != 0 or non-blocking -1.
    for (;;) {
        const int32_t valid_now = static_cast<int32_t>(MEM_W(8, queue));
        if (valid_now > 0) {
            break;
        }
        if (block == 0) {
            finish(static_cast<gpr>(static_cast<int32_t>(-1)));
            return;
        }
        static int afa_osrecv_wait_log = 0;
        const bool log_recv_wait =
            afa_osrecv_wait_log < 8 ||
            static_cast<uint32_t>(queue) == 0x80281258u || static_cast<uint32_t>(queue) == 0x8027C900u;
        if (log_recv_wait) {
            ++afa_osrecv_wait_log;
            recomp_boot_logf("[boot] afa: func_802374B0 empty queue=0x%08X block=%d valid=%d",
                             static_cast<uint32_t>(queue), static_cast<int32_t>(block), valid_now);
        }
        // asm/38310.s L_80237500: sh 8 on current thread (+0x10), not on D_802516E0 cell.
        const gpr cur_tcb = afa_get_current_thread_tcb(rdram);
        MEM_H(0x10, cur_tcb) = static_cast<int16_t>(8);
        // asm/38310.s: pass &queue->mtqueue (queue+0), not the queue struct base.
        ctx->r4 = ADD32(queue, 0);
        func_80241DFC(rdram, ctx);
    }

    gpr valid_count = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, queue)));
    if (static_cast<int32_t>(valid_count) <= 0) {
        finish(static_cast<gpr>(static_cast<int32_t>(-1)));
        return;
    }

    const int32_t mc = static_cast<int32_t>(MEM_W(0x10, queue));
    if (mc <= 0) {
        static int afa_osrecv_bad_mc_log = 0;
        if (afa_osrecv_bad_mc_log < 4) {
            ++afa_osrecv_bad_mc_log;
            recomp_boot_logf("[boot] afa: func_802374B0 bad msgCount=%d queue=0x%08X valid=%d (drop valid, return -1)",
                             mc, static_cast<uint32_t>(queue), static_cast<int32_t>(valid_count));
        }
        // Never return success here: retail osRecvMesg assumes msgCount > 0 (recvmesg.c line 19).
        if (static_cast<int32_t>(valid_count) > 0) {
            MEM_W(8, queue) = 0;
        }
        finish(static_cast<gpr>(static_cast<int32_t>(-1)));
        return;
    }

    // lib/mm-decomp/src/libultra/os/recvmesg.c: always advance mq->first and decrement validCount (msg may be NULL).
    const int32_t ri = static_cast<int32_t>(MEM_W(0xC, queue));
    gpr msg_array = static_cast<gpr>(static_cast<int32_t>(MEM_W(0x14, queue)));
    msg_array = afa_fixup_relocated_vram(msg_array);
    gpr msg_slot = ADD32(msg_array, S32(ri << 2));
    gpr msg_ptr = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, msg_slot)));
    msg_ptr = afa_fixup_relocated_vram(msg_ptr);
    if (mesg_out != 0 && afa_is_game_vram_ptr(mesg_out)) {
        MEM_W(0, mesg_out) = static_cast<int32_t>(static_cast<uint32_t>(msg_ptr));
    }
    MEM_W(0xC, queue) = static_cast<int32_t>(static_cast<uint32_t>((ri + 1) % mc));
    MEM_W(8, queue) = static_cast<int32_t>(static_cast<uint32_t>(ADD32(valid_count, -1)));

    // lib/mm-decomp/src/libultra/os/recvmesg.c: if (mq->fullQueue->next != NULL) osStartThread(__osPopThread(&mq->fullQueue));
    const gpr full_head = ADD32(queue, 4);
    gpr full_waiter = afa_mesg_queue_first_waiter(rdram, full_head);
    if (full_waiter != 0 && afa_is_plausible_tcb(full_waiter)) {
        const gpr prev_a4 = ctx->r4;
        ctx->r4 = full_head;
        func_80241F44(rdram, ctx);
        ctx->r4 = full_waiter;
        func_80237360(rdram, ctx);
        ctx->r4 = prev_a4;
    }

    static int afa_osrecv_ok_log = 0;
    if (afa_osrecv_ok_log < 8) {
        ++afa_osrecv_ok_log;
        recomp_boot_logf("[boot] afa: func_802374B0 recv queue=0x%08X msg=0x%08X valid->%d",
                         static_cast<uint32_t>(queue), static_cast<uint32_t>(msg_ptr),
                         static_cast<int32_t>(ADD32(valid_count, -1)));
    }
    finish(0);
}

// Host replacement for func_80236B80 (asm/37B30.s): osSendMesg — enqueue + wake recv waiters on queue+0.
RECOMP_FUNC void func_80236B80(uint8_t* rdram, recomp_context* ctx) {
    const gpr msg = ctx->r5;
    const gpr block = ctx->r6;
    gpr queue = afa_fixup_relocated_vram(ctx->r4);

    func_80241760(rdram, ctx);
    const gpr saved_ie = ctx->r2;

    auto finish = [&](gpr ret) {
        ctx->r4 = saved_ie;
        func_80241780(rdram, ctx);
        ctx->r2 = ret;
    };

    afa_ensure_mesg_queue_ready(rdram, queue);

    if (!afa_is_game_vram_ptr(queue)) {
        static int afa_ossend_bad_log = 0;
        if (afa_ossend_bad_log < 4) {
            ++afa_ossend_bad_log;
            recomp_boot_logf("[boot] afa: func_80236B80 idle (bad queue=0x%08X)", static_cast<uint32_t>(queue));
        }
        if (block == 0) {
            finish(static_cast<gpr>(static_cast<int32_t>(-1)));
            return;
        }
        finish(0);
        return;
    }

    gpr valid_count = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, queue)));
    gpr msg_count = static_cast<gpr>(static_cast<int32_t>(MEM_W(0x10, queue)));
    const int32_t mc = static_cast<int32_t>(msg_count);
    if (mc <= 0) {
        static int afa_ossend_nomsg_log = 0;
        if (afa_ossend_nomsg_log < 4) {
            ++afa_ossend_nomsg_log;
            recomp_boot_logf("[boot] afa: func_80236B80 break msgCount=0 queue=0x%08X",
                             static_cast<uint32_t>(queue));
        }
        finish(static_cast<gpr>(static_cast<int32_t>(-1)));
        return;
    }

    while (static_cast<int32_t>(valid_count) >= mc) {
        if (block == 0) {
            finish(static_cast<gpr>(static_cast<int32_t>(-1)));
            return;
        }
        const gpr cur_tcb = afa_get_current_thread_tcb(rdram);
        MEM_H(0x10, cur_tcb) = static_cast<int16_t>(8);
        ctx->r4 = ADD32(queue, 4);
        func_80241DFC(rdram, ctx);
        valid_count = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, queue)));
    }

    const gpr read_idx = static_cast<gpr>(static_cast<int32_t>(MEM_W(0xC, queue)));
    gpr msg_array = static_cast<gpr>(static_cast<int32_t>(MEM_W(0x14, queue)));
    msg_array = afa_fixup_relocated_vram(msg_array);
    const int32_t ri = static_cast<int32_t>(read_idx);
    const int32_t vc = static_cast<int32_t>(valid_count);
    const int32_t slot = (ri + vc) % mc;
    gpr msg_slot = ADD32(msg_array, S32(slot << 2));
    MEM_W(0, msg_slot) = static_cast<int32_t>(static_cast<uint32_t>(msg));

    MEM_W(8, queue) = static_cast<int32_t>(static_cast<uint32_t>(ADD32(valid_count, 1)));

    // lib/mm-decomp/src/libultra/os/sendmesg.c: if (mq->mtQueue->next != NULL) osStartThread(__osPopThread(&mq->mtQueue));
    afa_ensure_mesg_mtqueue(rdram, queue);
    afa_os_wake_recv_waiters(rdram, ctx, queue);

    static int afa_ossend_ok_log = 0;
    if (afa_ossend_ok_log < 8) {
        ++afa_ossend_ok_log;
        recomp_boot_logf("[boot] afa: func_80236B80 send queue=0x%08X msg=0x%08X valid->%d",
                         static_cast<uint32_t>(queue), static_cast<uint32_t>(msg),
                         static_cast<int32_t>(ADD32(valid_count, 1)));
    }
    finish(0);
}

// Runnable on run queue (2) or CPU (4) — lib/mm-decomp/include/PR/os_thread.h OS_STATE_* bit values.
static bool afa_is_runnable_thread_state(uint8_t* rdram, gpr tcb) {
    (void)rdram;
    const uint32_t st = static_cast<uint32_t>(MEM_HU(0x10, tcb));
    return st == 2u || st == 4u;
}

// Thread in D_802516E0 executing recompiled code: treat as running even if +0x10 was clobbered (word store bug).
static bool afa_is_current_thread_active(uint8_t* rdram, gpr tcb) {
    if (tcb != afa_get_current_thread_tcb(rdram)) {
        return afa_is_runnable_thread_state(rdram, tcb);
    }
    const uint32_t st = static_cast<uint32_t>(MEM_HU(0x10, tcb));
    if (st == 0u) {
        MEM_H(0x10, tcb) = 4;
        return true;
    }
    return afa_is_runnable_thread_state(rdram, tcb);
}

// First runnable thread in queue (skip sentinel D_802516D0) — asm/3F7F0.s compares *D_802516D8.
static gpr afa_scheduler_peek_head_thread(uint8_t* rdram) {
    (void)rdram;
    const gpr list_head = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u));
    gpr node = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head)));
    node = afa_fixup_relocated_vram(node);
    int guard = 0;
    while (node != 0 && ++guard <= 256) {
        if (static_cast<uint32_t>(node) == 0x802516D0u) {
            node = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, node)));
            node = afa_fixup_relocated_vram(node);
            continue;
        }
        if (afa_is_plausible_tcb(node) && afa_is_runnable_thread_state(rdram, node)) {
            return node;
        }
        node = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, node)));
        node = afa_fixup_relocated_vram(node);
    }
    return 0;
}

// AFA does not call libultra osSpTaskStartGo; it builds OSTask structs and pokes SP via func_802207E8 (asm/20F50.s).
static void afa_copy_rsp_task_stack_args(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    gpr t6 = ADD32(ctx->r29, 0x50);
    const gpr t8 = ADD32(t6, 0x3C);
    gpr t9 = ctx->r29;
    while (t6 != t8) {
        gpr at = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, t6)));
        t6 = ADD32(t6, 0xC);
        t9 = ADD32(t9, 0xC);
        MEM_W(-0xC, t9) = static_cast<int32_t>(static_cast<uint32_t>(at));
        at = static_cast<gpr>(static_cast<int32_t>(MEM_W(-8, t6)));
        MEM_W(-8, t9) = static_cast<int32_t>(static_cast<uint32_t>(at));
        at = static_cast<gpr>(static_cast<int32_t>(MEM_W(-4, t6)));
        MEM_W(-4, t9) = static_cast<int32_t>(static_cast<uint32_t>(at));
    }
    const gpr at = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, t6)));
    MEM_W(0, t9) = static_cast<int32_t>(static_cast<uint32_t>(at));
}

static void afa_submit_rsp_task_ptr(uint8_t* rdram, gpr task_ptr) {
    task_ptr = afa_fixup_relocated_vram(task_ptr);
    if (!afa_is_game_vram_ptr(task_ptr)) {
        return;
    }
    static int afa_rsp_submit_count = 0;
    if (afa_rsp_submit_count < 16) {
        ++afa_rsp_submit_count;
        const uint32_t ty = static_cast<uint32_t>(MEM_W(0, task_ptr));
        recomp_boot_logf("[boot] afa: submit_rsp_task ptr=0x%08X type=%u", static_cast<uint32_t>(task_ptr), ty);
    }
    ultramodern::submit_rsp_task(rdram, task_ptr);
}

// Game: asm/20F50.s func_802207E8 — build OSTask, queue header 0x01020040 (M_GFXTASK), custom SP path.
static void afa_run_custom_rsp_task_submit(uint8_t* rdram, recomp_context* ctx, uint32_t task_header_word) {
    const gpr entry_sp = ctx->r29;
    const gpr saved_ra = ctx->r31;

    ctx->r29 = ADD32(entry_sp, -0x50);
    MEM_W(0x44, ctx->r29) = saved_ra;
    MEM_W(0x50, ctx->r29) = ctx->r4;
    MEM_W(0x54, ctx->r29) = ctx->r5;
    MEM_W(0x58, ctx->r29) = ctx->r6;
    MEM_W(0x5C, ctx->r29) = ctx->r7;

    afa_copy_rsp_task_stack_args(rdram, ctx);

    ctx->r7 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0xC, ctx->r29)));
    ctx->r6 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0x8, ctx->r29)));
    ctx->r5 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0x4, ctx->r29)));
    ctx->r4 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, ctx->r29)));
    get_function(static_cast<int32_t>(0x80222D4Cu))(rdram, ctx);

    const gpr queue = afa_vaddr(0x80273B90u);
    gpr slot = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, queue)));
    slot = afa_fixup_relocated_vram(slot);
    MEM_W(0, slot) = static_cast<int32_t>(task_header_word);
    MEM_W(0, queue) = static_cast<int32_t>(static_cast<uint32_t>(ADD32(slot, 8)));

    get_function(static_cast<int32_t>(0x80222E24u))(rdram, ctx);
    gpr task_ptr = afa_fixup_relocated_vram(ctx->r2);
    MEM_W(0x4C, ctx->r29) = static_cast<int32_t>(static_cast<uint32_t>(task_ptr));

    ctx->r4 = task_ptr;
    get_function(static_cast<int32_t>(0x802375F0u))(rdram, ctx);
    MEM_W(0x4, task_ptr) = static_cast<int32_t>(static_cast<uint32_t>(ctx->r2));

    ctx->r31 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0x44, ctx->r29)));
    ctx->r29 = entry_sp;

    afa_submit_rsp_task_ptr(rdram, task_ptr);
}

RECOMP_FUNC void func_802207E8(uint8_t* rdram, recomp_context* ctx) {
    // 0x01020040 — asm/20F50.s 80220858 (byte 0 == M_GFXTASK per PR/sptask.h).
    afa_run_custom_rsp_task_submit(rdram, ctx, 0x01020040u);
}

RECOMP_FUNC void func_802208B4(uint8_t* rdram, recomp_context* ctx) {
    // 0x01030040 — asm/20F50.s 80220924 (second gfx submit variant).
    afa_run_custom_rsp_task_submit(rdram, ctx, 0x01030040u);
}

// Game: asm/48040.s — if a0==0 use D_802516E0 else a0; return lw +0x4 (thread counter).
RECOMP_FUNC void func_80247490(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    const gpr tcb = afa_resolve_thread_arg(rdram, ctx->r4);
    ctx->r2 = MEM_W(4, tcb);
}

// Game: asm/3F7F0.s — thread wait/yield (updates TCB+4, may call func_80241DFC).
RECOMP_FUNC void func_8023E840(uint8_t* rdram, recomp_context* ctx) {
    func_80241760(rdram, ctx);
    const gpr saved_ie = ctx->r2;

    const gpr tcb = afa_resolve_thread_arg(rdram, ctx->r4);
    const gpr target = ctx->r5;
    if (MEM_W(4, tcb) != target) {
        MEM_W(4, tcb) = static_cast<int32_t>(static_cast<uint32_t>(target));
    }

    const gpr global_cur = afa_get_current_thread_tcb(rdram);
    if (tcb != global_cur) {
        if (static_cast<uint32_t>(MEM_HU(0x10, tcb)) == 1u) {
            const gpr prev_a0 = ctx->r4;
            const gpr prev_a1 = ctx->r5;
            gpr wait_head = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, tcb)));
            wait_head = afa_fixup_relocated_vram(wait_head);
            func_802417A0(rdram, ctx);
            if (afa_is_scheduler_list_head_ptr(wait_head)) {
                ctx->r4 = wait_head;
                ctx->r5 = tcb;
                func_80241EFC(rdram, ctx);
            }
            ctx->r4 = prev_a0;
            ctx->r5 = prev_a1;
        }
    }

    const gpr head_thread = afa_scheduler_peek_head_thread(rdram);
    static int afa_yield_log_count = 0;
    if (afa_yield_log_count < 8) {
        ++afa_yield_log_count;
        recomp_boot_logf("[boot] afa: func_8023E840 yield check cur=0x%08X head=0x%08X cur+4=%d head+4=%d",
                         static_cast<uint32_t>(global_cur), static_cast<uint32_t>(head_thread),
                         static_cast<int32_t>(MEM_W(4, global_cur)),
                         head_thread != 0 ? static_cast<int32_t>(MEM_W(4, head_thread)) : -1);
    }
    // asm/3F7F0.s: compare run-queue head TCB+4 vs current; a0/a1 only update the yield counter.
    if (head_thread != 0 && head_thread != global_cur && afa_is_plausible_tcb(head_thread) &&
        afa_is_plausible_tcb(global_cur) && afa_is_runnable_thread_state(rdram, head_thread) &&
        afa_is_current_thread_active(rdram, global_cur)) {
        const int32_t cur_pri = static_cast<int32_t>(MEM_W(4, global_cur));
        const int32_t head_pri = static_cast<int32_t>(MEM_W(4, head_thread));
        if (cur_pri < head_pri) {
            static int afa_preempt_count = 0;
            if (afa_preempt_count < 8) {
                ++afa_preempt_count;
                recomp_boot_logf("[boot] afa: func_8023E840 preempt -> func_80241DFC (head_pri=%d cur_pri=%d)",
                                 head_pri, cur_pri);
            }
            MEM_H(0x10, global_cur) = 2;
            const gpr prev_r4 = ctx->r4;
            const gpr prev_r5 = ctx->r5;
            ctx->r4 = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u)); // asm/3F7F0.s L_8023E8FC
            ctx->r5 = global_cur;
            func_80241DFC(rdram, ctx);
            ctx->r4 = prev_r4;
            ctx->r5 = prev_r5;
        }
    } else if (head_thread != 0 && head_thread != global_cur && afa_yield_log_count <= 8) {
        static int afa_yield_skip_log = 0;
        if (afa_yield_skip_log < 4) {
            ++afa_yield_skip_log;
            const uint32_t cur_st = static_cast<uint32_t>(MEM_HU(0x10, global_cur));
            const uint32_t head_st =
                afa_is_plausible_tcb(head_thread) ? static_cast<uint32_t>(MEM_HU(0x10, head_thread)) : 0u;
            recomp_boot_logf("[boot] afa: func_8023E840 no preempt (cur_st=%u head_st=%u cur+4=%d head+4=%d)",
                             cur_st, head_st, static_cast<int32_t>(MEM_W(4, global_cur)),
                             static_cast<int32_t>(MEM_W(4, head_thread)));
        }
    }

    ctx->r4 = saved_ie;
    func_80241780(rdram, ctx);

    // asm/31B30.s: func_8023169C ends with jal func_8023E840 then .L80231778 → pause_self.
    // First returns may be from other callers; tail return is usually #1..#3 during boot.
    static int afa_func_8023e840_ret_count = 0;
    if (afa_func_8023e840_ret_count < 5) {
        ++afa_func_8023e840_ret_count;
        recomp_boot_logf("[boot] afa: func_8023E840 return #%d cur_tcb=0x%08X", afa_func_8023e840_ret_count,
                         static_cast<uint32_t>(afa_get_current_thread_tcb(rdram)));
    }
}

// Game: asm/42750.s func_80241F44 — run queue pop at D_802516D8; mesg pop at &mq->mtQueue (asm/37B30.s).
static gpr afa_scheduler_pop_head(uint8_t* rdram, gpr list_head_ptr) {
    (void)rdram;
    const gpr head = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head_ptr)));
    if (head == 0) {
        return 0;
    }
    const gpr next = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, head)));
    MEM_W(0, list_head_ptr) = static_cast<int32_t>(static_cast<uint32_t>(next));
    return head;
}

RECOMP_FUNC void func_80241F44(uint8_t* rdram, recomp_context* ctx) {
    gpr list_head_ptr = afa_fixup_relocated_vram(ctx->r4);
    if (afa_is_run_queue_head_ptr(list_head_ptr)) {
        list_head_ptr = afa_fixup_list_head_ptr(list_head_ptr);
        ctx->r2 = afa_scheduler_pop_head(rdram, list_head_ptr);
        return;
    }

    // lib/mm-decomp sendmesg.c: __osPopThread(&mq->mtQueue) — asm/42750.s 80241F44 / 37B30.s 80236C94.
    const gpr link = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head_ptr)));
    gpr link_fixed = 0;
    gpr next_link = 0;
    if (link != 0) {
        link_fixed = afa_fixup_relocated_vram(link);
        next_link = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, link_fixed)));
    }
    MEM_W(0, list_head_ptr) = static_cast<int32_t>(static_cast<uint32_t>(next_link));
    ctx->r2 = link;
}

// First thread blocked on mq->mtQueue (sendmesg.c: mq->mtQueue->next != NULL).
static gpr afa_mesg_queue_first_waiter(uint8_t* rdram, gpr queue) {
    (void)rdram;
    gpr mt_field = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, queue)));
    mt_field = afa_fixup_relocated_vram(mt_field);
    if (mt_field == 0) {
        return 0;
    }
    const gpr tail_link = afa_vaddr(0x802516D0u); // &__osThreadTail.next — createmesgqueue.c
    if (static_cast<uint32_t>(mt_field) == static_cast<uint32_t>(tail_link)) {
        return afa_fixup_relocated_vram(
            static_cast<gpr>(static_cast<int32_t>(MEM_W(0, mt_field))));
    }
    if (afa_is_plausible_tcb(mt_field)) {
        return mt_field;
    }
    return 0;
}

// Walk __osThreadTail.next — match TCB+8 to &mq->mtQueue (recv block path in asm/38310.s).
static gpr afa_find_mesg_recv_waiter(uint8_t* rdram, gpr queue) {
    queue = afa_fixup_relocated_vram(queue);
    if (!afa_is_game_vram_ptr(queue)) {
        return 0;
    }
    afa_ensure_mesg_queue_ready(rdram, queue);
    afa_ensure_mesg_mtqueue(rdram, queue);
    const gpr tail = afa_vaddr(0x802516D0u);
    gpr node = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, tail)));
    int guard = 0;
    while (node != 0 && ++guard <= 256) {
        node = afa_fixup_thread_tcb(node);
        if (!afa_is_plausible_tcb(node)) {
            break;
        }
        if (static_cast<uint32_t>(MEM_HU(0x10, node)) == 8u) {
            gpr wait_list = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, node)));
            wait_list = afa_fixup_relocated_vram(wait_list);
            if (wait_list == queue) {
                return node;
            }
        }
        node = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, node)));
        node = afa_fixup_relocated_vram(node);
    }
    return 0;
}

// Remove one thread from __osThreadTail.next (used when waiter is not the list head).
static void afa_mesg_list_remove_waiter(uint8_t* rdram, gpr waiter) {
    (void)rdram;
    waiter = afa_fixup_thread_tcb(waiter);
    if (!afa_is_plausible_tcb(waiter)) {
        return;
    }
    const gpr tail = afa_vaddr(0x802516D0u);
    gpr prev = tail;
    gpr cur = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, tail)));
    int guard = 0;
    while (cur != 0 && ++guard <= 256) {
        cur = afa_fixup_thread_tcb(cur);
        if (cur == waiter) {
            const gpr next = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, cur)));
            MEM_W(0, prev) = static_cast<int32_t>(static_cast<uint32_t>(next));
            return;
        }
        prev = cur;
        cur = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, cur)));
    }
}

// lib/mm-decomp sendmesg.c + asm/37B30.s L_80236C84: pop + osStartThread; waiter must match this mq (TCB+8).
static void afa_os_wake_recv_waiters(uint8_t* rdram, recomp_context* ctx, gpr queue) {
    queue = afa_fixup_relocated_vram(queue);
    if (!afa_is_game_vram_ptr(queue)) {
        return;
    }
    afa_ensure_mesg_queue_ready(rdram, queue);
    afa_ensure_mesg_mtqueue(rdram, queue);

    gpr waiter = afa_find_mesg_recv_waiter(rdram, queue);
    if (waiter == 0 || !afa_is_plausible_tcb(waiter)) {
        static int afa_wake_miss_log = 0;
        if (afa_wake_miss_log < 4) {
            ++afa_wake_miss_log;
            const gpr tail = afa_vaddr(0x802516D0u);
            recomp_boot_logf("[boot] afa: send no recv waiter queue=0x%08X mt=0x%08X tail->next=0x%08X valid=%d",
                             static_cast<uint32_t>(queue),
                             static_cast<uint32_t>(MEM_W(0, queue)),
                             static_cast<uint32_t>(MEM_W(0, tail)),
                             static_cast<int32_t>(MEM_W(8, queue)));
        }
        return;
    }

    gpr wait_list = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, waiter)));
    wait_list = afa_fixup_relocated_vram(wait_list);
    if (wait_list != queue) {
        static int afa_wake_mismatch_log = 0;
        if (afa_wake_mismatch_log < 4) {
            ++afa_wake_mismatch_log;
            recomp_boot_logf("[boot] afa: wake skip tcb=0x%08X queue=0x%08X wait_head=0x%08X",
                             static_cast<uint32_t>(waiter), static_cast<uint32_t>(queue),
                             static_cast<uint32_t>(wait_list));
        }
        return;
    }

    static int afa_wake_log = 0;
    if (afa_wake_log < 8) {
        ++afa_wake_log;
        recomp_boot_logf("[boot] afa: wake recv waiter tcb=0x%08X queue=0x%08X",
                         static_cast<uint32_t>(waiter), static_cast<uint32_t>(queue));
    }

    // lib/mm-decomp createmesgqueue.c: every mq->mtQueue points at __osThreadTail.next — never __osPopThread
    // on a single queue or we may pop a waiter blocked on a different OSMesgQueue (asm/37B30.s L_80236C84).
    const gpr prev_a4 = ctx->r4;
    afa_mesg_list_remove_waiter(rdram, waiter);
    afa_ensure_mesg_mtqueue(rdram, queue);
    // func_80237360 state-8 path (asm/38310.s) re-inserts on the mesg list; leave runnable (state 2).
    MEM_H(0x10, waiter) = 2;
    MEM_W(8, waiter) = 0;
    ctx->r4 = waiter;
    func_80237360(rdram, ctx);
    ctx->r4 = prev_a4;
}

// Game: asm/42750.s func_80241DFC — save current thread context, re-enqueue if runnable, dispatch next.
static void afa_save_thread_context(uint8_t* rdram, recomp_context* ctx, gpr tcb) {
    (void)rdram;
    const uint32_t status = cop0_status_read(ctx) | 0x2u;
    MEM_W(0x118, tcb) = static_cast<int32_t>(status);

    SD(ctx->r16, 0x98, tcb);
    SD(ctx->r17, 0xA0, tcb);
    SD(ctx->r18, 0xA8, tcb);
    SD(ctx->r19, 0xB0, tcb);
    SD(ctx->r20, 0xB8, tcb);
    SD(ctx->r21, 0xC0, tcb);
    SD(ctx->r22, 0xC8, tcb);
    SD(ctx->r23, 0xD0, tcb);
    SD(ctx->r28, 0xE8, tcb);
    SD(ctx->r29, 0xF0, tcb);
    SD(ctx->r30, 0xF8, tcb);
    SD(ctx->r31, 0x100, tcb);
    // asm/42750.s func_80241DFC L_80241E48: sw $ra, 0x11C($a1) — eret PC in func_80241F54.
    const uint32_t ra = static_cast<uint32_t>(ctx->r31);
    if (ra != 0u && ra != 0x802420D0u && afa_is_game_vram_ptr(ctx->r31)) {
        MEM_W(0x11C, tcb) = static_cast<int32_t>(ra);
    }

    if (MEM_W(0x18, tcb) != 0) {
        const int c1cs = get_cop1_cs();
        SD(ctx->f20.u64, 0x180, tcb);
        SD(ctx->f22.u64, 0x188, tcb);
        SD(ctx->f24.u64, 0x190, tcb);
        SD(ctx->f26.u64, 0x198, tcb);
        SD(ctx->f28.u64, 0x1A0, tcb);
        SD(ctx->f30.u64, 0x1A8, tcb);
        MEM_W(0x12C, tcb) = c1cs;
    }

    SD(ctx->r1, 0x20, tcb);
    SD(ctx->r2, 0x28, tcb);
    SD(ctx->r3, 0x30, tcb);
    SD(ctx->r4, 0x38, tcb);
    SD(ctx->r5, 0x40, tcb);
    SD(ctx->r6, 0x48, tcb);
    SD(ctx->r7, 0x50, tcb);
    SD(ctx->r8, 0x58, tcb);
    SD(ctx->r9, 0x60, tcb);
    SD(ctx->r10, 0x68, tcb);
    SD(ctx->r11, 0x70, tcb);
    SD(ctx->r12, 0x78, tcb);
    SD(ctx->r13, 0x80, tcb);
    SD(ctx->r14, 0x88, tcb);
    SD(ctx->r15, 0x90, tcb);

    ctx->hi = LD(0x108, tcb);
    ctx->lo = LD(0x110, tcb);
}

static void afa_start_thread_cpu(uint8_t* rdram, recomp_context* ctx, gpr tcb) {
    auto& cont = afa_thread_cont_for(tcb);
    if (cont.jmp_active) {
        return;
    }
    cont.jmp_active = true;
    g_afa_active_recomp_ctx = ctx;
    if (setjmp(cont.jb) == 0) {
        afa_load_context_from_tcb(rdram, ctx, tcb);
        const uint32_t entry_pc = afa_resolve_thread_dispatch_pc(rdram, tcb);
        static int afa_dispatch_seq = 0;
        if (++afa_dispatch_seq <= 16) {
            recomp_boot_logf("[boot] afa: dispatch #%d thread PC=0x%08X sp=0x%08X tcb=0x%08X",
                             afa_dispatch_seq, entry_pc, static_cast<uint32_t>(ctx->r29),
                             static_cast<uint32_t>(tcb));
        }
        get_function(static_cast<int32_t>(entry_pc))(rdram, ctx);
        cont.jmp_active = false;
        cont.dfc_active = false;
        g_afa_active_recomp_ctx = nullptr;
    }
}

static void afa_dispatch_until_self(uint8_t* rdram, recomp_context* ctx, gpr self) {
    const gpr list_head_ptr = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u));
    const gpr cur_thread_ptr = afa_vaddr(0x802516E0u);

    int spin_guard = 0;
    for (;;) {
        afa_drain_pending_vi_retrace(rdram, ctx);

        gpr next = afa_scheduler_pop_head(rdram, list_head_ptr);
        next = afa_fixup_thread_tcb(next);
        if (!afa_is_plausible_tcb(next)) {
            next = afa_main_thread_tcb();
            static bool logged_empty_runq = false;
            if (!logged_empty_runq) {
                logged_empty_runq = true;
                recomp_boot_log("[boot] afa: func_80241DFC empty run queue, using main TCB");
            }
        }

        if (++spin_guard > 4096) {
            recomp_boot_logf("[boot] FATAL: func_80241DFC spin guard (self=0x%08X next=0x%08X)",
                             static_cast<uint32_t>(self), static_cast<uint32_t>(next));
            std::fprintf(stderr, "afa func_80241DFC: scheduler spin (no runnable progress)\n");
            std::fflush(stderr);
            std::exit(EXIT_FAILURE);
        }

        if (next == self) {
            MEM_W(0, cur_thread_ptr) = static_cast<int32_t>(static_cast<uint32_t>(self));
            MEM_H(0x10, self) = 4;
            afa_load_context_from_tcb(rdram, ctx, self);
            return;
        }

        MEM_W(0, cur_thread_ptr) = static_cast<int32_t>(static_cast<uint32_t>(next));
        MEM_H(0x10, next) = 4;

        auto& ncont = afa_thread_cont_for(next);
        if (!ncont.jmp_active) {
            afa_start_thread_cpu(rdram, ctx, next);
            continue;
        }

        // Thread was preempted inside func_80241DFC; run other threads then longjmp back to its jal caller.
        static int afa_handoff_log = 0;
        if (afa_handoff_log < 12) {
            ++afa_handoff_log;
            const uint32_t sp_lo =
                static_cast<uint32_t>(static_cast<uint64_t>(LD(0xF0, next)) & 0xFFFFFFFFu);
            recomp_boot_logf("[boot] afa: handoff to tcb=0x%08X (resume dfc) sp=0x%08X",
                             static_cast<uint32_t>(next), sp_lo);
        }
        afa_dispatch_until_self(rdram, ctx, next);
        afa_load_context_from_tcb(rdram, ctx, next);
        longjmp(ncont.dfc_jb, 1);
    }
}

RECOMP_FUNC void func_80241DFC(uint8_t* rdram, recomp_context* ctx) {
    static int afa_dfc_count = 0;
    if (afa_dfc_count < 16) {
        ++afa_dfc_count;
        recomp_boot_logf("[boot] afa: func_80241DFC #%d cur_tcb=0x%08X wait=0x%08X", afa_dfc_count,
                         static_cast<uint32_t>(afa_get_current_thread_tcb(rdram)),
                         static_cast<uint32_t>(ctx->r4));
    }
    gpr tcb = afa_get_current_thread_tcb(rdram);
    if (!afa_is_plausible_tcb(tcb)) {
        func_80241F54(rdram, ctx);
        return;
    }

    auto& cont = afa_thread_cont_for(tcb);
    if (setjmp(cont.dfc_jb) != 0) {
        cont.dfc_active = false;
        afa_load_context_from_tcb(rdram, ctx, tcb);
        return;
    }

    cont.dfc_active = true;
    afa_save_thread_context(rdram, ctx, tcb);

    gpr wait_head = afa_fixup_relocated_vram(ctx->r4);
    afa_ensure_mesg_queue_ready(rdram, wait_head);
    if (wait_head != 0 && afa_is_mesg_queue_wait_head(rdram, wait_head)) {
        const gpr prev_r4 = ctx->r4;
        const gpr prev_r5 = ctx->r5;
        ctx->r4 = wait_head;
        ctx->r5 = tcb;
        func_80241EFC(rdram, ctx);
        ctx->r4 = prev_r4;
        ctx->r5 = prev_r5;

        // Message posted before yield (e.g. host VI drain) — recvmesg.c re-checks validCount after wait.
        if (static_cast<int32_t>(MEM_W(8, wait_head)) > 0) {
            cont.dfc_active = false;
            cont.suspended = false;
            return;
        }
    }

    cont.suspended = true;
    afa_dispatch_until_self(rdram, ctx, tcb);
    cont.dfc_active = false;
    cont.suspended = false;
}

// Game: asm/38310.s — thread state machine; enqueue/dequeue on D_802516D8 / D_802516E0.
RECOMP_FUNC void func_80237360(uint8_t* rdram, recomp_context* ctx) {
    const gpr tcb_arg = afa_fixup_thread_tcb(ctx->r4);
    const gpr list_head = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u));
    const gpr cur_thread_ptr = afa_fixup_current_thread_ptr(afa_vaddr(0x802516E0u));

    func_80241760(rdram, ctx);
    const gpr saved_ie = ctx->r2;

    const uint32_t state = static_cast<uint32_t>(MEM_HU(0x10, tcb_arg));

    // asm/38310.s L_802373BC: mesg-wait thread (state 8) re-queued after osSendMesg/osStopThread paths.
    if (state == 8u) {
        gpr wait_list = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, tcb_arg)));
        wait_list = afa_fixup_relocated_vram(wait_list);
        const gpr runq = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u));
        const gpr prev_r4 = ctx->r4;
        const gpr prev_r5 = ctx->r5;
        if (wait_list == 0 || wait_list == runq) {
            MEM_H(0x10, tcb_arg) = 2;
            ctx->r4 = runq;
            ctx->r5 = tcb_arg;
            func_80241EFC(rdram, ctx);
        } else {
            MEM_H(0x10, tcb_arg) = 8;
            ctx->r4 = wait_list;
            ctx->r5 = tcb_arg;
            func_80241EFC(rdram, ctx);
            gpr wl = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, tcb_arg)));
            wl = afa_fixup_relocated_vram(wl);
            ctx->r4 = wl;
            func_80241F44(rdram, ctx);
            gpr popped = afa_fixup_thread_tcb(ctx->r2);
            ctx->r4 = runq;
            ctx->r5 = popped;
            func_80241EFC(rdram, ctx);
            ctx->r4 = runq;
            ctx->r5 = tcb_arg;
            func_80241EFC(rdram, ctx);
        }
        ctx->r4 = prev_r4;
        ctx->r5 = prev_r5;
        // asm/38310.s L_8023743C: may dispatch if no current thread or higher-priority head.
    } else if (state == 1u) {
        // Thread may still be on a mesg wait list if func_80237210 was re-called while state was 8.
        gpr wait_list = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, tcb_arg)));
        wait_list = afa_fixup_relocated_vram(wait_list);
        if (wait_list != 0 && wait_list != list_head && !afa_is_plausible_tcb(wait_list)) {
            goto done;
        }
        MEM_H(0x10, tcb_arg) = 2;
        const gpr prev_r4 = ctx->r4;
        const gpr prev_r5 = ctx->r5;
        ctx->r4 = list_head;
        ctx->r5 = tcb_arg;
        func_80241EFC(rdram, ctx);
        ctx->r4 = prev_r4;
        ctx->r5 = prev_r5;
        static int afa_start_log_count = 0;
        if (afa_start_log_count < 8) {
            ++afa_start_log_count;
            recomp_boot_logf("[boot] afa: func_80237360 start/enqueue tcb=0x%08X +4=%d queue=0x%08X",
                             static_cast<uint32_t>(tcb_arg), static_cast<int32_t>(MEM_W(4, tcb_arg)),
                             static_cast<uint32_t>(afa_scheduler_peek_head_thread(rdram)));
        }
        // Fall through to asm/38310.s L_8023743C (do not goto done).
    } else {
        gpr wait_list = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, tcb_arg)));
        wait_list = afa_fixup_relocated_vram(wait_list);
        // TCB+8 holds a list-head pointer or 0; a TCB address here corrupts func_80241EFC.
        if (afa_is_plausible_tcb(wait_list)) {
            wait_list = 0;
        }
        if (wait_list == 0) {
            MEM_H(0x10, tcb_arg) = 2;
            const gpr prev_r4 = ctx->r4;
            const gpr prev_r5 = ctx->r5;
            ctx->r4 = list_head;
            ctx->r5 = tcb_arg;
            func_80241EFC(rdram, ctx);
            ctx->r4 = prev_r4;
            ctx->r5 = prev_r5;
            goto done;
        }
        if (wait_list == list_head) {
            MEM_H(0x10, tcb_arg) = 2;
            const gpr prev_r4 = ctx->r4;
            const gpr prev_r5 = ctx->r5;
            ctx->r4 = list_head;
            ctx->r5 = tcb_arg;
            func_80241EFC(rdram, ctx);
            ctx->r4 = prev_r4;
            ctx->r5 = prev_r5;
            goto done;
        }
        MEM_H(0x10, tcb_arg) = 8;
        const gpr prev_r4 = ctx->r4;
        ctx->r4 = wait_list;
        func_80241F44(rdram, ctx);
        const gpr popped = static_cast<gpr>(ctx->r2);
        ctx->r4 = prev_r4;
        if (popped != 0) {
            ctx->r4 = wait_list;
            ctx->r5 = popped;
            func_80241EFC(rdram, ctx);
        }
        ctx->r4 = list_head;
        ctx->r5 = tcb_arg;
        func_80241EFC(rdram, ctx);
        goto done;
    }

    // asm/38310.s L_8023745C: if D_802516E0 != 0 and cur+4 < arg+4, sh 2 on cur then jal func_80241DFC (a0 = &D_802516D8).
    if (MEM_W(0, cur_thread_ptr) != 0) {
        const gpr cur_tcb = afa_fixup_thread_tcb(static_cast<gpr>(static_cast<int32_t>(MEM_W(0, cur_thread_ptr))));
        if (afa_is_plausible_tcb(cur_tcb) && afa_is_plausible_tcb(tcb_arg) && cur_tcb != tcb_arg &&
            afa_is_current_thread_active(rdram, cur_tcb)) {
            const int32_t cur_pri = static_cast<int32_t>(MEM_W(4, cur_tcb));
            const int32_t arg_pri = static_cast<int32_t>(MEM_W(4, tcb_arg));
            if (cur_pri < arg_pri) {
                static int afa_start_preempt_log = 0;
                if (afa_start_preempt_log < 8) {
                    ++afa_start_preempt_log;
                    recomp_boot_logf("[boot] afa: func_80237360 preempt -> func_80241DFC (new+4=%d cur+4=%d)",
                                     arg_pri, cur_pri);
                }
                MEM_H(0x10, cur_tcb) = 2;
                const gpr prev_r4 = ctx->r4;
                const gpr prev_r5 = ctx->r5;
                ctx->r4 = list_head;
                ctx->r5 = cur_tcb;
                func_80241DFC(rdram, ctx);
                ctx->r4 = prev_r4;
                ctx->r5 = prev_r5;
                goto done;
            }
        }
    }

    if (MEM_W(0, cur_thread_ptr) == 0) {
        const gpr prev_r4 = ctx->r4;
        ctx->r4 = 0;
        func_80241DFC(rdram, ctx);
        ctx->r4 = prev_r4;
    }

done:
    ctx->r4 = saved_ie;
    func_80241780(rdram, ctx);
}

static void afa_run_thread_until_yield(uint8_t* rdram, recomp_context* ctx, gpr tcb) {
    afa_start_thread_cpu(rdram, ctx, tcb);
}

// Trampolines: recompiled thread bodies have no boot logs (asm/31B30.s thread table).
RECOMP_FUNC void afa_func_8023169C_bootlog(uint8_t* rdram, recomp_context* ctx) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        recomp_boot_log("[boot] afa: func_8023169C entered (game thread, asm/31B30.s)");
    }
    func_8023169C(rdram, ctx);
}

RECOMP_FUNC void afa_func_80231584_bootlog(uint8_t* rdram, recomp_context* ctx) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        recomp_boot_log("[boot] afa: func_80231584 entered (audio thread, asm/31B30.s)");
    }
    func_80231584(rdram, ctx);
}

RECOMP_FUNC void afa_func_80231630_bootlog(uint8_t* rdram, recomp_context* ctx) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        recomp_boot_log("[boot] afa: func_80231630 entered (VI thread, asm/31B30.s)");
    }
    func_80231630(rdram, ctx);
}

RECOMP_FUNC void afa_func_80248D30_bootlog(uint8_t* rdram, recomp_context* ctx) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        recomp_boot_log("[boot] afa: func_80248D30 entered (PI DMA thread, asm/3F660.s)");
    }
    func_80248D30(rdram, ctx);
}

RECOMP_FUNC void afa_func_8023DCE8_bootlog(uint8_t* rdram, recomp_context* ctx) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        recomp_boot_log("[boot] afa: func_8023DCE8 entered (VI mgr thread, asm/3EA90.s / func_8023DB60 pri=0xFE)");
    }
    func_8023DCE8(rdram, ctx);
}

RECOMP_FUNC void afa_func_8022CA30_bootlog(uint8_t* rdram, recomp_context* ctx) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        recomp_boot_log("[boot] afa: func_8022CA30 entered (event dispatcher, asm/2CE30.s / func_80230FF8)");
    }
    afa_ensure_event_mesg_queue(rdram, afa_vaddr(0x80281258u));
    func_8022CA30(rdram, ctx);
}

// Game: asm/42750.s — context switch tail; N64Recomp stub body is empty (funcs_17.c).
RECOMP_FUNC void func_80241F54(uint8_t* rdram, recomp_context* ctx) {
    const gpr list_head_ptr = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u));
    const gpr cur_thread_ptr = afa_vaddr(0x802516E0u);

    for (;;) {
        afa_drain_pending_vi_retrace(rdram, ctx);

        gpr thread = afa_scheduler_pop_head(rdram, list_head_ptr);
        thread = afa_fixup_thread_tcb(thread);
        if (!afa_is_plausible_tcb(thread)) {
            thread = afa_main_thread_tcb();
            static bool logged_bad_queue = false;
            if (!logged_bad_queue) {
                recomp_boot_log("[boot] afa: func_80241F54 — empty run queue, using main TCB (0x80281068)");
                logged_bad_queue = true;
            }
        }

        MEM_W(0, cur_thread_ptr) = static_cast<int32_t>(static_cast<uint32_t>(thread));
        MEM_H(0x10, thread) = 4;

        afa_run_thread_until_yield(rdram, ctx, thread);
    }
}

// *D_80251774 — asm/data/4C050.data.s (.word sub-buffer); N64Recomp uses 0x80A0 band.
static gpr afa_load_80251774_target(uint8_t* rdram) {
    (void)rdram;
    const gpr d_80251774 = afa_vaddr(0x80251774u);
    gpr p = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, d_80251774)));
    return afa_fixup_relocated_vram(p);
}

// Game: asm/3EE70.s — store slot pointer into *D_80251774 node (+0x8 chain); called from func_8022C838.
RECOMP_FUNC void func_8023DEC0(uint8_t* rdram, recomp_context* ctx) {
    const gpr old_sp = ctx->r29;
    const gpr old_ra = ctx->r31;
    const gpr old_s0 = ctx->r16;

    ctx->r29 = ADD32(old_sp, -0x28);
    MEM_W(0x1C, ctx->r29) = old_ra;
    MEM_W(0x18, ctx->r29) = old_s0;
    MEM_W(0x28, ctx->r29) = ctx->r4;

    func_80241760(rdram, ctx);
    const gpr saved_ie = ctx->r2;

    const gpr t7 = afa_load_80251774_target(rdram);
    if (afa_is_game_vram_ptr(t7)) {
        const gpr slot = afa_fixup_relocated_vram(MEM_W(0x28, ctx->r29));
        if (afa_is_game_vram_ptr(slot)) {
            MEM_W(8, t7) = static_cast<int32_t>(static_cast<uint32_t>(slot));
        }
    }

    const gpr t9 = afa_load_80251774_target(rdram);
    if (afa_is_game_vram_ptr(t9)) {
        MEM_H(0, t9) = 1;
    }

    const gpr t0 = afa_load_80251774_target(rdram);
    if (afa_is_game_vram_ptr(t0)) {
        gpr t1 = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, t0)));
        t1 = afa_fixup_relocated_vram(t1);
        if (afa_is_game_vram_ptr(t1)) {
            const gpr t2 = MEM_W(4, t1);
            MEM_W(0xC, t0) = t2;
        }
    }

    ctx->r4 = saved_ie;
    func_80241780(rdram, ctx);

    ctx->r31 = MEM_W(0x1C, ctx->r29);
    ctx->r16 = MEM_W(0x18, ctx->r29);
    ctx->r29 = old_sp;
}

// Game: asm/43090.s — audio/OS table setup; uses D_80251710/70/74/78 and polls SI @ D_A4400010.
RECOMP_FUNC void func_802420E0(uint8_t* rdram, recomp_context* ctx) {
    const gpr buf = afa_vaddr(0x80251710u);
    const gpr d_80251770 = afa_vaddr(0x80251770u);
    const gpr d_80251774 = afa_vaddr(0x80251774u);
    const gpr d_80251778 = afa_vaddr(0x80251778u);

    const gpr prev_r4 = ctx->r4;
    const gpr prev_r5 = ctx->r5;
    ctx->r4 = buf;
    ctx->r5 = 0x60;
    func_802481C0(rdram, ctx);
    ctx->r4 = prev_r4;
    ctx->r5 = prev_r5;

    MEM_W(0, d_80251770) = static_cast<int32_t>(static_cast<uint32_t>(buf));
    const gpr sub = ADD32(buf, 0x30);
    MEM_W(0, d_80251774) = static_cast<int32_t>(static_cast<uint32_t>(sub));
    MEM_H(0x32, buf) = 1;

    const gpr t0 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, d_80251770)));
    MEM_H(2, t0) = 1;

    // osTvType @ D_80000300 — asm/43090.s branches (0 / 2 / else).
    const uint32_t os_type = static_cast<uint32_t>(MEM_W(0, afa_vaddr(0x80000300u)));

    gpr table_ptr = 0;
    uint32_t magic = 0;
    if (os_type == 0u) {
        table_ptr = afa_vaddr(0x80251A80u);
        magic = 0x02F5B2D2u;
    } else if (os_type == 2u) {
        table_ptr = afa_vaddr(0x80251AD0u);
        magic = 0x02E6025Cu;
    } else {
        table_ptr = afa_vaddr(0x80251B20u);
        magic = 0x02E6D354u;
    }

    MEM_W(0, d_80251774) = static_cast<int32_t>(static_cast<uint32_t>(sub));
    MEM_W(8, sub) = static_cast<int32_t>(static_cast<uint32_t>(table_ptr));
    MEM_W(0, d_80251778) = static_cast<int32_t>(magic);

    MEM_H(0, sub) = 0x20;

    const gpr t4 = afa_load_80251774_target(rdram);
    const gpr t5 = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, t4)));
    if (afa_is_game_vram_ptr(t5)) {
        const gpr t7 = MEM_W(4, t5);
        MEM_W(0xC, t4) = t7;
    }

    // Skip SI busy-wait @ D_A4400010 and func_802474C0 (cart MMIO; host has no SI FIFO).
    (void)rdram;
}

// Game: asm/48040.s — OS message-queue init; D_80251A70 head must be 0x80251A70 (not 0x809F9D50 in datasyms).
RECOMP_FUNC void func_80247090(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    const gpr d_80285f60 = afa_vaddr(0x80285F60u);
    const gpr d_80285f64 = afa_vaddr(0x80285F64u);
    const gpr d_80285f68 = afa_vaddr(0x80285F68u);
    const gpr d_80285f6c = afa_vaddr(0x80285F6Cu);
    const gpr list_head = afa_vaddr(0x80251A70u); // .word D_80285F40 — asm/data/4C050.data.s

    MEM_W(0, d_80285f64) = 0;
    MEM_W(0, d_80285f60) = 0;
    MEM_W(0, d_80285f68) = 0;
    MEM_W(0, d_80285f6c) = 0;

    gpr node = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head)));
    if (!afa_is_game_vram_ptr(node)) {
        node = afa_vaddr(0x80285F40u); // D_80285F40 — asm/data/57D20.bss.s
        MEM_W(0, list_head) = static_cast<int32_t>(static_cast<uint32_t>(node));
    }

    MEM_W(4, node) = static_cast<int32_t>(static_cast<uint32_t>(node));

    gpr t9 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head)));
    const gpr t0 = MEM_W(4, t9);
    MEM_W(0, t9) = t0;

    gpr t1 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head)));
    MEM_W(0x10, t1) = 0;
    MEM_W(0x14, t1) = 0;

    gpr t4 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head)));
    const gpr t6 = MEM_W(0x10, t4);
    const gpr t7 = MEM_W(0x14, t4);
    MEM_W(8, t4) = t6;
    MEM_W(0xC, t4) = t7;

    gpr t5 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head)));
    MEM_W(0x18, t5) = 0;

    gpr t8 = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head)));
    MEM_W(0x1C, t8) = 0;
}

// Game: asm/1000.s entrypoint — N64Recomp datasyms slip (main_BSS_START, D_80282B60); see AFA_PORT.md § datasyms.
RECOMP_FUNC void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx) {
    gpr bss = afa_vaddr(0x80256D70u); // main_BSS_START — asm/1000.s
    uint32_t bytes_left = 0x306C0u;
    while (bytes_left != 0) {
        MEM_W(0, bss) = 0;
        MEM_W(4, bss) = 0;
        bytes_left -= 8;
        bss = ADD32(bss, 8);
    }

    ctx->r29 = afa_vaddr(0x80282B60u); // D_80282B60 — asm/1000.s (not 0x80276E90 from datasyms)

    recomp_boot_log("[boot] afa: entrypoint -> main (0x80231150)");
    recomp_rom_main(rdram, ctx);

    // asm/31B30.s main returns after thread enqueue; hardware continues in the scheduler (func_80241F54).
    recomp_boot_log("[boot] afa: main returned — dispatch run queue (func_80241F54)");
    func_80241F54(rdram, ctx);
    recomp_boot_log("[boot] afa: func_80241F54 returned (unexpected)");
}

// Game: asm/3F350.s — poll PI (D_A4600010), lw D_80000308, cart lw into *a1 (same role as func_80248040).
RECOMP_FUNC void func_8023E640(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    const gpr kseg0 = afa_vaddr(0x80000000u);
    const uint32_t pi_base = static_cast<uint32_t>(MEM_W(0x308, kseg0)); // config/undefined_syms_auto.txt D_80000308

    const uint32_t dev_k1 = (pi_base | static_cast<uint32_t>(ctx->r4)) | 0xA0000000u;
    const uint32_t phys = dev_k1 & 0x1FFFFFFFu;
    const std::span<const uint8_t> rom = recomp::get_rom();
    const uint32_t off = afa_cart_phys_to_rom_offset(phys);
    if (!recomp::is_rom_loaded() || off == UINT32_MAX || off + 4 > rom.size()) {
        ctx->r2 = static_cast<gpr>(static_cast<int32_t>(-1));
        return;
    }

    const uint32_t word =
        (static_cast<uint32_t>(rom[off + 0]) << 24) |
        (static_cast<uint32_t>(rom[off + 1]) << 16) |
        (static_cast<uint32_t>(rom[off + 2]) << 8) |
        static_cast<uint32_t>(rom[off + 3]);
    MEM_W(0, ctx->r5) = static_cast<int32_t>(word);
    ctx->r2 = 0;
}

// Game: asm/3F350.s — early boot (COP0, cart PIO, vectors). Recompiled body hits PI @ 0xA460 / IO @ 0x80A0 / AI @ 0xA500.
RECOMP_FUNC void func_8023E3A0(uint8_t* rdram, recomp_context* ctx) {
    const gpr old_sp = ctx->r29;
    const gpr old_ra = ctx->r31;
    const gpr old_s0 = ctx->r16;

    ctx->r29 = ADD32(old_sp, -0x40);
    MEM_W(0x1C, ctx->r29) = old_ra;
    MEM_W(0x18, ctx->r29) = old_s0;
    MEM_W(0x38, ctx->r29) = 0;

    MEM_W(0x4C90, afa_vaddr(0x80284C90u)) = 1; // D_80284C90 (asm/3F350.s)

    func_80248020(rdram, ctx);
    ctx->r16 = ctx->r2;
    ctx->r4 = ctx->r16 | 0x20000000u;
    func_80248010(rdram, ctx);

    ctx->r4 = 0x01000800u;
    func_80248030(rdram, ctx);

    ctx->r4 = 0x1FC007FCu;
    ctx->r5 = ADD32(ctx->r29, 0x3C);
    do {
        func_80248040(rdram, ctx);
    } while (ctx->r2 != 0);

    gpr word = MEM_W(0x3C, ctx->r29);
    ctx->r4 = 0x1FC007FCu;
    ctx->r5 = word | 0x8u;
    do {
        func_80248090(rdram, ctx);
    } while (ctx->r2 != 0);

    afa_copy_words_from_802417E0(rdram, afa_vaddr(0x80000000u));
    afa_copy_words_from_802417E0(rdram, afa_vaddr(0x80000080u));
    afa_copy_words_from_802417E0(rdram, afa_vaddr(0x80000100u));
    afa_copy_words_from_802417E0(rdram, afa_vaddr(0x80000180u));

    func_8023CE80(rdram, ctx);

    ctx->r4 = afa_vaddr(0x80000000u);
    ctx->r5 = 0x190;
    func_802480E0(rdram, ctx);
    func_80248160(rdram, ctx);

    ctx->r4 = 4;
    ctx->r5 = ADD32(ctx->r29, 0x38);
    func_8023E640(rdram, ctx);

    const gpr seg8025 = afa_vaddr(0x80250000u);
    word = MEM_W(0x38, ctx->r29);
    const gpr masked = word & static_cast<gpr>(0xFFFFFFF0u);
    if (masked != 0) {
        MEM_W(0x1680, seg8025) = 0;
        MEM_W(0x1684, seg8025) = masked;
    }

    ctx->r4 = MEM_W(0x1680, seg8025);
    ctx->r5 = MEM_W(0x1684, seg8025);
    ctx->r6 = 0;
    ctx->r7 = 3;
    func_8023D228(rdram, ctx);
    MEM_W(0x20, ctx->r29) = ctx->r2;
    MEM_W(0x24, ctx->r29) = ctx->r3;

    ctx->r5 = MEM_W(0x24, ctx->r29);
    ctx->r4 = MEM_W(0x20, ctx->r29);
    ctx->r6 = 0;
    ctx->r7 = 4;
    func_8023D128(rdram, ctx);
    MEM_W(0x1680, seg8025) = ctx->r2;
    MEM_W(0x1684, seg8025) = ctx->r3;

    const gpr kseg0 = afa_vaddr(0x80000000u);
    if (MEM_W(0x30C, kseg0) == 0) {
        ctx->r4 = ADD32(kseg0, 0x31C);
        ctx->r5 = 0x40;
        func_802481C0(rdram, ctx);
    }

    // Skip PI @ D_A4600010 and AI @ D_A5000508 polls; no controller path yet.
    MEM_W(0x1690, seg8025) = 0; // D_80251690

    ctx->r31 = MEM_W(0x1C, ctx->r29);
    ctx->r16 = MEM_W(0x18, ctx->r29);
    ctx->r29 = old_sp;
}

static uint8_t* g_afa_rdram = nullptr;
static std::atomic<bool> g_afa_vi_retrace_pending{false};

// Game-thread only: one deferred VI retrace post per scheduler dispatch.
// Post one message if a thread is blocked in osRecvMesg on mq with validCount==0 (lib/mm-decomp recvmesg.c).
static void afa_try_send_if_recv_blocked(uint8_t* rdram, recomp_context* ctx, gpr mq, gpr msg) {
    mq = afa_fixup_relocated_vram(mq);
    if (!afa_is_game_vram_ptr(mq)) {
        return;
    }
    afa_ensure_mesg_queue_ready(rdram, mq);
    if (afa_find_mesg_recv_waiter(rdram, mq) == 0) {
        return;
    }
    if (static_cast<int32_t>(MEM_W(8, mq)) > 0) {
        return;
    }
    const gpr prev_r4 = ctx->r4;
    const gpr prev_r5 = ctx->r5;
    const gpr prev_r6 = ctx->r6;
    ctx->r4 = mq;
    ctx->r5 = msg;
    ctx->r6 = 0;
    func_80236B80(rdram, ctx);
    ctx->r4 = prev_r4;
    ctx->r5 = prev_r5;
    ctx->r6 = prev_r6;
}

// asm/31B30.s L_80231778: idle loop only calls func_8023E840 — it does not dispatch D_802516D8.
// After osSendMesg wakes a thread (func_80237360), run it here or it stays blocked with validCount > 0.
static void afa_dispatch_run_queue_burst(uint8_t* rdram, recomp_context* ctx, int max_threads) {
    const gpr self = afa_main_thread_tcb();
    const gpr list_head_ptr = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u));
    const gpr cur_thread_ptr = afa_vaddr(0x802516E0u);

    for (int i = 0; i < max_threads; ++i) {
        gpr head = afa_scheduler_peek_head_thread(rdram);
        head = afa_fixup_thread_tcb(head);
        if (head == 0 || head == self || !afa_is_plausible_tcb(head)) {
            break;
        }

        gpr next = afa_scheduler_pop_head(rdram, list_head_ptr);
        next = afa_fixup_thread_tcb(next);
        if (next == 0 || next == self || !afa_is_plausible_tcb(next)) {
            break;
        }

        static int afa_pause_dispatch_log = 0;
        if (afa_pause_dispatch_log < 12) {
            ++afa_pause_dispatch_log;
            recomp_boot_logf("[boot] afa: pause_self dispatch tcb=0x%08X pri=%d",
                             static_cast<uint32_t>(next), static_cast<int32_t>(MEM_W(4, next)));
        }

        MEM_W(0, cur_thread_ptr) = static_cast<int32_t>(static_cast<uint32_t>(next));
        MEM_H(0x10, next) = 4;
        afa_start_thread_cpu(rdram, ctx, next);
    }
}

// Retail posts while game thread yields (asm/31B30.s pause loop). Unblock mesg-wait threads toward gfx/audio.
static void afa_pause_nudge_blocked_threads(uint8_t* rdram, recomp_context* ctx) {
    g_afa_vi_retrace_pending.store(true, std::memory_order_release);
    afa_drain_pending_vi_retrace(rdram, ctx);

    // asm/2CE30.s func_8022CA30 — kick event 0x29A
    afa_try_send_if_recv_blocked(rdram, ctx, afa_vaddr(0x80281258u), 0x29A);

    // asm/31B30.s func_8023169C — D_80280C30 -> D_8027C900 VI retrace (count 1)
    afa_try_send_if_recv_blocked(rdram, ctx, afa_vaddr(0x8027C900u), 0);

    // asm/31B30.s func_80231584 — audio thread recv on .L80280C70
    afa_try_send_if_recv_blocked(rdram, ctx, afa_vaddr(0x80280C70u), 0);

    // asm/3EA90.s func_8023DCE8 — VI mgr recv; event type 0xD handled in same loop
    afa_try_send_if_recv_blocked(rdram, ctx, afa_vaddr(0x80278ED0u), 0xD);

    // asm/3F660.s PI DMA thread — D_80274FE0
    afa_try_send_if_recv_blocked(rdram, ctx, afa_vaddr(0x80274FE0u), 0);
}

static void afa_drain_pending_vi_retrace(uint8_t* rdram, recomp_context* ctx) {
    if (!g_afa_vi_retrace_pending.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    const gpr mq = afa_vaddr(0x8027C900u);
    if (static_cast<int32_t>(MEM_W(0x10, mq)) <= 0) {
        return;
    }
    const gpr prev_r4 = ctx->r4;
    const gpr prev_r5 = ctx->r5;
    const gpr prev_r6 = ctx->r6;
    ctx->r4 = mq;
    ctx->r5 = 0;
    ctx->r6 = 0; // OS_MESG_NOBLOCK
    func_80236B80(rdram, ctx);
    ctx->r4 = prev_r4;
    ctx->r5 = prev_r5;
    ctx->r6 = prev_r6;
}

// Host VI tick: asm/31B30.s func_8023169C creates D_8027C900 (VI retrace OSMesgQueue, count 1).
// Retail posts via OS_EVENT_VI (mm-decomp vimgr.c). Do not call func_80236B80 here — that runs the
// game scheduler (func_80237360/func_80241DFC) and ultramodern forbids MQ access off the game thread
// (ultramodern/src/mesgqueue.cpp osSendMesg: "if (!ultramodern::is_game_thread()) enqueue_external…").
void afa_host_vi_retrace_pulse_impl() {
    uint8_t* rdram = g_afa_rdram;
    if (rdram == nullptr || !ultramodern::is_game_started()) {
        return;
    }
    const gpr mq = afa_vaddr(0x8027C900u);
    if (static_cast<int32_t>(MEM_W(0x10, mq)) <= 0) {
        return;
    }
    g_afa_vi_retrace_pending.store(true, std::memory_order_release);
}

// N64Recomp emits pause_self for self-branches (funcs_17.c L_80231778 after func_8023E840).
// Default ultramodern pause_self waits on external_messages; AFA osSendMesg uses func_80236B80 + game TCBs.
// Retail: asm/31B30.s idle is jal func_8023E840 then spin — we loop yield + host VI drain instead.
extern "C" void pause_self(uint8_t* rdram) {
    recomp_context* ctx = g_afa_active_recomp_ctx;
    if (ctx == nullptr) {
        recomp_boot_log("[boot] afa: pause_self without active ctx — spinning");
        for (;;) {
            std::this_thread::yield();
        }
    }

    static bool logged_idle = false;
    if (!logged_idle) {
        logged_idle = true;
        recomp_boot_log("[boot] afa: pause_self -> retail yield loop (func_8023E840 + VI drain)");
    }

    static int afa_pause_ticks = 0;
    for (;;) {
        afa_drain_pending_vi_retrace(rdram, ctx);

        // Unblock mesg-wait threads — retail IRQ/VI/event posts while the game thread spins.
        if (afa_pause_ticks == 0 || (afa_pause_ticks % 15) == 0) {
            afa_pause_nudge_blocked_threads(rdram, ctx);
            afa_dispatch_run_queue_burst(rdram, ctx, 8);
        }

        const gpr prev_r4 = ctx->r4;
        const gpr prev_r5 = ctx->r5;
        ctx->r4 = 0; // asm/31B30.s L_80231770: or $a0, $zero, $zero
        ctx->r5 = 0; // or $a1, $zero, $zero
        func_8023E840(rdram, ctx);
        ctx->r4 = prev_r4;
        ctx->r5 = prev_r5;
        ++afa_pause_ticks;
        std::this_thread::yield();
    }
}

} // extern "C"

namespace zelda64 {

void afa_host_vi_retrace_pulse() {
    afa_host_vi_retrace_pulse_impl();
}

void afa_on_init_after_overlays(uint8_t* rdram, recomp_context* ctx) {
    g_afa_rdram = rdram;
    (void)ctx;
    std::fprintf(stderr, "[AeroAssault64 boot] afa: DMA patch VRAM redirect (80248B70/80248C50)\n");
    std::fflush(stderr);
    recomp::overlays::add_loaded_function(0x80248B70, func_80248B70);
    recomp::overlays::add_loaded_function(0x80248C50, func_80248C50);
    recomp::overlays::add_loaded_function(0x80200050, recomp_entrypoint);
    recomp::overlays::add_loaded_function(0x80231150, recomp_rom_main);
    recomp::overlays::add_loaded_function(0x8023169C, afa_func_8023169C_bootlog);
    recomp::overlays::add_loaded_function(0x80248D30, afa_func_80248D30_bootlog);
    recomp::overlays::add_loaded_function(0x80231584, afa_func_80231584_bootlog);
    recomp::overlays::add_loaded_function(0x80231630, afa_func_80231630_bootlog);
    recomp::overlays::add_loaded_function(0x8023DCE8, afa_func_8023DCE8_bootlog);
    recomp::overlays::add_loaded_function(0x8022CA30, afa_func_8022CA30_bootlog);
    recomp::overlays::add_loaded_function(0x80237210, func_80237210);
    recomp::overlays::add_loaded_function(0x80237360, func_80237360);
    recomp::overlays::add_loaded_function(0x80241DFC, func_80241DFC);
    recomp::overlays::add_loaded_function(0x80241F54, func_80241F54);
    recomp::overlays::add_loaded_function(0x802371E0, func_802371E0);
    recomp::overlays::add_loaded_function(0x802374B0, func_802374B0);
    recomp::overlays::add_loaded_function(0x80236B80, func_80236B80);
    recomp::overlays::add_loaded_function(0x802207E8, func_802207E8);
    recomp::overlays::add_loaded_function(0x802208B4, func_802208B4);
    recomp::overlays::add_loaded_function(0x8023E840, func_8023E840);
    recomp::overlays::add_loaded_function(0x8023E6B0, func_8023E6B0);
    recomp_boot_log("[boot] afa: registered entrypoint + main + thread entries in func_map");
}

} // namespace zelda64

#endif
