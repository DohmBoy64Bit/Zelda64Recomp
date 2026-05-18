// AFA retail: PatchesLib RECOMP_PATCH bodies link at patches.ld RAMBASE (0x80801000).
// The game still dispatches PI DMA through retail VRAM (func_8023E760 table — asm/3F660.s).
// CPU N64Recomp stubs register empty bodies at 0x80248B70 / 0x80248C50 in func_map during load_overlays;
// re-point after init() so jal targets hit PatchesLib (required_patches_afa.c → recomp_load_overlays).
//
// func_8024A710 / func_80248040: direct jal from RecompiledFuncs (e.g. funcs_8.c) — add_loaded_function
// does not replace those calls; provide host bodies here (link /FORCE:MULTIPLE vs RecompiledFuncs).

#include "aero_build_config.h"

#if AEROASSAULT64_AFA_PRODUCT && AEROASSAULT64_AFA_RETAIL_PIPELINES

#include <cstdint>
#include <cstdio>
#include <span>

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
extern "C" void func_802371E0(uint8_t* rdram, recomp_context* ctx); // asm/380D0.s — not in func_map
extern "C" void func_8023DF30(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_80246BB0(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_8023DCE8(uint8_t* rdram, recomp_context* ctx); // asm/3EA90.s thread entry (pri 0xFE)

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

// Game: asm/42750.s func_80241EFC — sorted insert; a0 = &D_802516D8, a1 = OSThread*.
// Walks from *a0 (sentinel D_802516D0 or thread chain); slt on +0x4; links prev->next = ins.
RECOMP_FUNC void func_80241EFC(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    const gpr list_head_ptr = afa_fixup_list_head_ptr(ctx->r4);
    const gpr ins = afa_fixup_thread_tcb(ctx->r5);
    if (!afa_is_game_vram_ptr(list_head_ptr) || !afa_is_game_vram_ptr(ins)) {
        return;
    }

    const int32_t ins_pri = static_cast<int32_t>(MEM_W(4, ins));
    gpr prev = list_head_ptr;
    gpr cur = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head_ptr)));
    if (cur == 0) {
        return;
    }
    cur = afa_fixup_relocated_vram(cur);

    if (static_cast<int32_t>(MEM_W(4, cur)) < ins_pri) {
        // Game: bnez slt -> .L80241F30 with prev still list_head_ptr (asm/42750.s 80241F10).
    } else {
        while (true) {
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
    // Initial SP must match SD/LD at +0xF0 (afa_save_thread_context / afa_restore_thread_and_run).
    SD(static_cast<uint64_t>(static_cast<uint32_t>(stack_top)), 0xF0, tcb);
    // Thread entry argument (a3) — restored as $a0 on first dispatch (e.g. PI queue D_802516A0 for func_80248D30).
    MEM_W(0x38, tcb) = static_cast<int32_t>(static_cast<uint32_t>(afa_fixup_relocated_vram(ctx->r7)));
    MEM_H(0x10, tcb) = 1; // asm/381C0.s A5180010 — created, not yet on run queue
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
        ctx->r4 = queue;
        func_80241DFC(rdram, ctx);
        return;
    }

    gpr valid_count = static_cast<gpr>(static_cast<int32_t>(MEM_W(8, queue)));
    if (valid_count == 0) {
        static int afa_osrecv_wait_log = 0;
        if (afa_osrecv_wait_log < 4) {
            ++afa_osrecv_wait_log;
            recomp_boot_logf("[boot] afa: func_802374B0 empty queue=0x%08X block=%d",
                             static_cast<uint32_t>(queue), static_cast<int32_t>(block));
        }
        if (block == 0) {
            finish(static_cast<gpr>(static_cast<int32_t>(-1)));
            return;
        }
        // asm/38310.s L_80237500: mark recv wait on D_802516E0, yield.
        const gpr evt = afa_fixup_datasyms_809f99xx(afa_vaddr(0x802516E0u));
        if (afa_is_game_vram_ptr(evt)) {
            MEM_H(0x10, evt) = static_cast<int16_t>(8);
        }
        ctx->r4 = queue;
        func_80241DFC(rdram, ctx);
        return;
    }

    gpr msg_ptr = 0;
    if (mesg_out != 0 && afa_is_game_vram_ptr(mesg_out)) {
        const gpr read_idx = static_cast<gpr>(static_cast<int32_t>(MEM_W(0xC, queue)));
        gpr msg_array = static_cast<gpr>(static_cast<int32_t>(MEM_W(0x14, queue)));
        msg_array = afa_fixup_relocated_vram(msg_array);
        const gpr msg_count = static_cast<gpr>(static_cast<int32_t>(MEM_W(0x10, queue)));
        const int32_t mc = static_cast<int32_t>(msg_count);
        if (mc <= 0) {
            recomp_boot_logf("[boot] afa: func_802374B0 break msgCount=0 queue=0x%08X valid=%d",
                            static_cast<uint32_t>(queue), static_cast<int32_t>(valid_count));
            finish(0);
            return;
        }
        const int32_t ri = static_cast<int32_t>(read_idx);
        gpr msg_slot = ADD32(msg_array, S32(ri << 2));
        msg_ptr = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, msg_slot)));
        msg_ptr = afa_fixup_relocated_vram(msg_ptr);
        MEM_W(0, mesg_out) = static_cast<int32_t>(static_cast<uint32_t>(msg_ptr));
        const int32_t new_read = (ri + 1) % mc;
        MEM_W(0xC, queue) = static_cast<int32_t>(static_cast<uint32_t>(new_read));
    }

    MEM_W(8, queue) = static_cast<int32_t>(static_cast<uint32_t>(ADD32(valid_count, -1)));

    gpr wait_list = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, queue)));
    wait_list = afa_fixup_relocated_vram(wait_list);
    if (wait_list != 0 && afa_is_game_vram_ptr(wait_list)) {
        gpr waiter = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, wait_list)));
        waiter = afa_fixup_relocated_vram(waiter);
        if (waiter != 0) {
            ctx->r4 = ADD32(queue, 4);
            func_80241F44(rdram, ctx);
            gpr awakened = afa_fixup_relocated_vram(ctx->r2);
            ctx->r4 = awakened;
            func_80237360(rdram, ctx);
        }
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

// First runnable thread in queue (skip sentinel D_802516D0) — asm/3F7F0.s compares *D_802516D8.
static gpr afa_scheduler_peek_head_thread(uint8_t* rdram) {
    (void)rdram;
    const gpr list_head = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u));
    gpr node = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, list_head)));
    node = afa_fixup_relocated_vram(node);
    if (node == 0) {
        return 0;
    }
    if (static_cast<uint32_t>(node) == 0x802516D0u) {
        node = static_cast<gpr>(static_cast<int32_t>(MEM_W(0, node)));
        node = afa_fixup_relocated_vram(node);
    }
    return node;
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
            ctx->r4 = MEM_W(8, tcb);
            ctx->r5 = tcb;
            func_802417A0(rdram, ctx);
            ctx->r4 = MEM_W(8, tcb);
            ctx->r5 = tcb;
            func_80241EFC(rdram, ctx);
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
    if (head_thread != 0 && afa_is_plausible_tcb(head_thread) && afa_is_plausible_tcb(global_cur) &&
        head_thread != global_cur) {
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
            ctx->r4 = 0; // func_80241DFC reads D_802516E0, not list head (asm/42750.s).
            func_80241DFC(rdram, ctx);
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

// Game: asm/42750.s func_80241F44 — v0 = *a0; *a0 = (*v0)->next; a0 is &D_802516D8.
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
    const gpr list_head_ptr = afa_fixup_list_head_ptr(ctx->r4);
    ctx->r2 = afa_scheduler_pop_head(rdram, list_head_ptr);
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
    MEM_W(0x11C, tcb) = static_cast<int32_t>(static_cast<uint32_t>(ctx->r31));

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

RECOMP_FUNC void func_80241DFC(uint8_t* rdram, recomp_context* ctx) {
    static int afa_dfc_count = 0;
    if (afa_dfc_count < 8) {
        ++afa_dfc_count;
        recomp_boot_logf("[boot] afa: func_80241DFC #%d cur_tcb=0x%08X", afa_dfc_count,
                         static_cast<uint32_t>(afa_get_current_thread_tcb(rdram)));
    }
    (void)ctx->r4; // asm passes D_802516D8 on preempt path; save uses D_802516E0 current thread.
    gpr tcb = afa_get_current_thread_tcb(rdram);
    if (!afa_is_plausible_tcb(tcb)) {
        func_80241F54(rdram, ctx);
        return;
    }

    afa_save_thread_context(rdram, ctx, tcb);

    if (static_cast<uint32_t>(MEM_HU(0x10, tcb)) == 2u) {
        const gpr list_head = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u));
        const gpr prev_r4 = ctx->r4;
        const gpr prev_r5 = ctx->r5;
        ctx->r4 = list_head;
        ctx->r5 = tcb;
        func_80241EFC(rdram, ctx);
        ctx->r4 = prev_r4;
        ctx->r5 = prev_r5;
    }

    func_80241F54(rdram, ctx);
}

// Game: asm/38310.s — thread state machine; enqueue/dequeue on D_802516D8 / D_802516E0.
RECOMP_FUNC void func_80237360(uint8_t* rdram, recomp_context* ctx) {
    const gpr tcb_arg = afa_fixup_thread_tcb(ctx->r4);
    const gpr list_head = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u));
    const gpr cur_thread_ptr = afa_fixup_current_thread_ptr(afa_vaddr(0x802516E0u));

    func_80241760(rdram, ctx);
    const gpr saved_ie = ctx->r2;

    const uint32_t state = static_cast<uint32_t>(MEM_HU(0x10, tcb_arg));

    if (state == 1u) {
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
        goto done;
    }

    if (state != 8u) {
        const gpr wait_list = MEM_W(8, tcb_arg);
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

    if (MEM_W(0, cur_thread_ptr) != 0) {
        const gpr cur_tcb = afa_fixup_thread_tcb(static_cast<gpr>(static_cast<int32_t>(MEM_W(0, cur_thread_ptr))));
        if (afa_is_plausible_tcb(cur_tcb) && afa_is_plausible_tcb(tcb_arg)) {
            const int32_t cur_pri = static_cast<int32_t>(MEM_W(4, cur_tcb));
            const int32_t arg_pri = static_cast<int32_t>(MEM_W(4, tcb_arg));
            if (cur_pri < arg_pri) {
                MEM_H(0x10, cur_tcb) = 2;
                func_80241DFC(rdram, ctx);
                goto done;
            }
        }
    }

    if (MEM_W(0, cur_thread_ptr) == 0) {
        func_80241F54(rdram, ctx);
    }

done:
    ctx->r4 = saved_ie;
    func_80241780(rdram, ctx);
}

// Scheduler dispatch: func_map (load_overlays) then static section table (recomp_overlays.inl).
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
    default:
        return nullptr;
    }
}

// Game: asm/42750.s func_80241F54 — restore TCB and dispatch (eret → thread PC at +0x11C).
static void afa_restore_thread_and_run(uint8_t* rdram, recomp_context* ctx, gpr tcb) {
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

    // Never context-switched: +0x20..+0x30 still zero from func_80237210; use init SP/arg not stale GPR slots.
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

    uint32_t entry_pc = static_cast<uint32_t>(MEM_W(0x11C, tcb));
    if (!afa_is_game_vram_ptr(afa_vaddr(entry_pc))) {
        entry_pc = 0x8023169Cu; // func_8023169C — asm/31B30.s main thread entry
    }

    if (!afa_is_game_vram_ptr(ctx->r29)) {
        const gpr boot_sp = static_cast<gpr>(static_cast<int32_t>(MEM_W(0xF0, tcb)));
        ctx->r29 = afa_is_game_vram_ptr(boot_sp) ? boot_sp : afa_vaddr(0x8027CB60u);
    }

    static int afa_dispatch_seq = 0;
    ++afa_dispatch_seq;
    if (afa_dispatch_seq <= 12) {
        recomp_boot_logf("[boot] afa: dispatch #%d thread PC=0x%08X sp=0x%08X tcb=0x%08X", afa_dispatch_seq,
                         entry_pc, static_cast<uint32_t>(ctx->r29), static_cast<uint32_t>(tcb));
    }
    recomp_func_t* const entry = afa_lookup_recomp_func(entry_pc);
    if (entry == nullptr) {
        recomp_boot_logf("[boot] FATAL: afa_lookup_recomp_func miss at 0x%08X (see stderr)", entry_pc);
        std::fprintf(stderr, "Failed to find function at 0x%08X\n", entry_pc);
        std::fflush(stderr);
        std::exit(EXIT_FAILURE);
    }
    entry(rdram, ctx);
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

// Game: asm/42750.s — context switch tail; N64Recomp stub body is empty (funcs_17.c).
RECOMP_FUNC void func_80241F54(uint8_t* rdram, recomp_context* ctx) {
    const gpr list_head_ptr = afa_fixup_list_head_ptr(afa_vaddr(0x802516D8u));
    const gpr cur_thread_ptr = afa_vaddr(0x802516E0u);

    // Game: jal func_80241F44 then sw v0 -> D_802516E0; restore from k0 = v0 (asm/42750.s 80241F54).
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
    MEM_W(0x10, thread) = 4; // sh 4 @ +0x10 — running (asm/42750.s)

    afa_restore_thread_and_run(rdram, ctx, thread);
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

} // extern "C"

namespace zelda64 {

void afa_on_init_after_overlays(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
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
    recomp_boot_log("[boot] afa: registered entrypoint + main + thread entries in func_map");
}

} // namespace zelda64

#endif
