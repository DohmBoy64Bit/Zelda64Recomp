# Aero Fighters Assault — full retail runtime port (tracker)

This file backs the **Docs/Workflow.md** checklist row *AFA fork — full retail runtime*. Paths are under **`lib/Zelda64Recomp/`** unless noted.

## Full static recomp (CPU + RSP) — one PowerShell pass

After **WSL** **`make strict-verify`** (ELF), with **ROM** + filled **RSP** TOMLs under **`lib/Zelda64Recomp/`**:

**Repo root (PowerShell 7+):** **`.\tools\phase6_full_recomp_afa.ps1`**

- Runs **`tools\phase6_link_recompiledfuncs.ps1`** (unless **`-SkipJunction`**), **`tools\phase5_run_aero_n64recomp.ps1`** (**`-SkipN64Recomp`** to skip), **`tools\phase6_rsprecomp_afa.ps1`** (**`-SkipRSP`** / **`-CopyTools`** / **`-RomPath`** forwarded).
- **`-WhatIf`** lists steps only.

Outputs: repo-root **`RecompiledFuncs/*.c`** (per **`config/aerofighters_assault.n64recomp.toml`**) and **`lib/Zelda64Recomp/rsp/aspMain.cpp`**, **`rsp/njpgdspMain.cpp`**. This is **not** the **PatchesLib** pipeline — for **`patches.elf`** + **`patches.toml`** + **`AEROASSAULT64_AFA_RETAIL_PIPELINES`**, see **§2** below.

## Execution order (do this when ROM analysis is far enough along)

### 1. RSP (RSPRecomp → `rsp/*.cpp`)

**Splat vs RSPRecomp:** **`config/splat.yaml`** and **`Docs/Workflow.md`** (Phase 2–3) document **MIPS** layout — **`ipl3`**, **`entry`**, **`main`**, **`data`**, **`rodata`**, **`bss`** in **`roms/afa.n64.us.z64`**. **RSPRecomp** needs a **different** triple per microcode: **`text_offset`** and **`text_size`** are the **file byte offset and length of that microcode’s text in the same cart ROM** named by **`rom_file_path`**. Splat does **not** output those fields; you derive them from ROM/Ghidra (e.g. following where the game loads **F3D / JPEG** RSP text into IMEM). **`config/afa_rsp/*.template.toml`** and engine-root **`aspMain.afa.us.toml`** / **`njpgdspMain.afa.us.toml`** carry committed USA values (see TOML comments and **`tools/rsprecomp_*.py`** used to derive them from the same ROM SHA1 as **`config/splat.yaml`**); re-validate with Listing/DMA when in doubt.

**Heuristic aids (Ghidra, PyGhidra — same `support/pyghidraRun.bat` as `Phase2_Closeout_Report.py`):** **`../../tools/ghidra/RSPRecomp_Confirm_Findings.py`** xrefs emulator VRAM seeds and matches an IMEM bootstrap prefix in **`.rom`** (**`EXPECTED_TEXT_OFFSET_IN_ROM` / `EXPECTED_TEXT_SIZE`** default to **aspMain** **`config/afa_rsp/aspMain.afa.us.template.toml`** **0x4DAB0** / **0x1000**; set both **`None`** in-script for **njpgdsp**-only dumps or another ROM); optional **`IMEM_BOOTSTRAP_NJPG_BE_U32`** + **`EXPECTED_NJPG_*`** match **`njpgdspMain.afa.us.template.toml`**; **false positive** at ROM **0x4BE20** = MIPS **`func_8024AE70`** in **`asm/4BE20.s`**, not RSP text; with **`capstone`** installed, each hit prints **MIPS32 BE** disassembly — same engine as host **`python ../../tools/rsp_rom_capstone_mips.py`** (**`--afa-usa-hints`** / **`--offset`**, **`requirements.txt`**); **`../../tools/ghidra/RSPRecomp_AFA_AllInOne.py`** runs the Phase-6 script bundle in one pass (**`tools/ghidra/README.txt`**); **`../../tools/ghidra/Find_RSP_Microcode_ROM_Hints.py`** lists **`.ram` → `.rom`** reference hotspots and **`lui`/`addiu`** ROM pointers (confirm in Listing / DMA / **`OSTask`**). **`../../tools/ghidra/RSP_LibUltra_And_IMEM_Scan.py`** scans **symbol names**, **instruction immediates** (e.g. **`0x04001000`**, **`0x04001080`**, PI/DMA hints), **ASCII** substrings such as **`osSpTask`** / **`OSTask`**, and **Paradigm-style `uv*` debug / scheduler strings** (when the retail binary omits SDK names). On AFA USA, the libultra ASCII lines are often empty while **`uvGfx` / `uvSc` / `uvDMA`**-style rodata still anchors the graphics path — use those xrefs toward DMA and microcode loads. **`../../tools/ghidra/RSP_Scheduler_String_Xref_Trace.py`** walks **incoming xrefs** from those rodata seeds into **functions**, then prints each function’s **operand references into `.rom`** plus **`lui`+`addiu`/`ori`** pairs that resolve to **`.rom`** (automates the “finish tracing” step toward **`text_offset`** candidates — still verify in Listing). **`../../tools/ghidra/RSP_IMEM_Load_And_Helper_Call_Trace.py`** finds **`0x04000000`–`0x04001FFF`** immediates (SP DMEM/IMEM per N64brew) and optional **`jal`** windows to a helper you name (**`HELPER_ENTRY_VRAM`**) so you can read **a0–a3** at the call that passes **IMEM + src + len**-style arguments. **`../../tools/ghidra/RSP_List_Jal_Callees_From_Function.py`** lists **`jal`** callee **entry** addresses from a driver function body (set **`SOURCE_FUNCTION_ENTRY_VRAM`**) to fill **`HELPER_ENTRY_VRAM`** without manual navigation. **`../../tools/ghidra/RSP_Jal_Call_Sites_Disasm_From_Caller.py`** prints **disassembly before each `jal`** from that caller in one run (optional **`ONLY_CALLEE_ENTRIES`** filter). **`../../tools/ghidra/RSP_Jal_Arg_Register_Slice.py`** prints a **heuristic last-def** summary for **`a0`–`a3`** (and **`lw`** bases) at each **`jal`** to **`TARGET_CALLEE_ENTRIES`** (linear window — wrong after untaken branches). **`../../tools/ghidra/RSP_Function_Return_Reg_Slice.py`** prints **`v0`/`v1`** last-def before each **`jr ra`** in a callee you name (**`TARGET_FUNCTION_ENTRY_VRAM`**, e.g. **0x8023D820** when the arg slice’s **`jal`** hint says **`FUN_8023d820`** feeds **`or s2,v0,zero`** on AFA USA). **`../../tools/ghidra/RSP_RAM_Context_Field_Xrefs.py`** lists **incoming xrefs** (with **`insn=LOAD`/`insn=STORE`** tags when the ref source is an instruction), **BE u32** values at **`BASE_VRAM + FIELD_OFFSETS`** (e.g. **0x802839B0** and **`+0x8` / `+0xC`**) and flags pointers into **`.rom`** (same **`rom_file_offset`** idea as **`Find_RSP_Microcode_ROM_Hints.py`**); when **`RUN_CONSTANT_BASE_MEMOPS`** / **`RUN_STORE_XREF_SCAN`** are True in-script, it also runs the **constant-base `lw`/`sw`** walk and **`sw` xref** pass (**`MEMOP_SCAN_MODE`**, **`JAL_KNOWN_V0_BY_CALLEE_ENTRY`**) matching **`RSP_RAM_Constant_Base_Memops.py`**. **`../../tools/ghidra/RSP_RAM_Constant_Base_Memops.py`** scans **`lw`/`sw`** for matching effective addresses when the base register is a tracked **`lui`/`addiu`** constant in-function (**`JAL_KNOWN_V0_BY_CALLEE_ENTRY`** for **`jal`** returns). Set **`MEMOP_RUN`** to **`stores`**, **`stores_xref`** (xref **`sw`** to each EA), **`loads`**, or **`all`**.

**Pilotwings 64 (Paradigm cross-title, not AFA):** [Pilotwings64Decomp](https://github.com/gcsmith/Pilotwings64Decomp) and [Pilotwings64Recomp](https://github.com/gcsmith/Pilotwings64Recomp) document a full USA split and RSPRecomp inputs. Their published [aspMain.us.toml](https://raw.githubusercontent.com/gcsmith/Pilotwings64Recomp/main/aspMain.us.toml) is only a **schema / field-shape** example (**`text_offset`**, **`text_size`**, **`text_address`**, **`extra_indirect_branch_targets`**) for the **Pilotwings** cart — **do not** paste those hex offsets into AFA TOMLs. Repo note: **`../../Docs/RepoInjests/Pilotwings/README.txt`**.

1. **`text_offset`**, **`text_size`**, **`text_address`** (and **`extra_indirect_branch_targets`** for **aspMain** when RSPRecomp/runtime requires them — compare upstream [aspMain.us.rev1.toml](https://raw.githubusercontent.com/Mr-Wiseguy/Zelda64Recomp/master/aspMain.us.rev1.toml)) live in **`../../config/afa_rsp/*.template.toml`** and mirrored **`lib/Zelda64Recomp/aspMain.afa.us.toml`**, **`njpgdspMain.afa.us.toml`** (see TOML comments + **`tools/rsprecomp_*.py`**). Re-run those scripts or Ghidra/DMA proof if your ROM SHA1 differs from **`config/splat.yaml`**.
2. Place **`afa.n64.us.z64`** next to those TOMLs (or adjust **`rom_file_path`** to match where the byteswapped USA ROM lives) — same idea as **`mm.us.rev1.rom_uncompressed.z64`** for MM in upstream **`BUILDING.md`** §3–4 (when that doc is present in your engine tree).
3. From **engine root**, run **`RSPRecomp.exe`** the same way as upstream:
   - **`./RSPRecomp aspMain.us.rev1.toml`** → your **`./RSPRecomp aspMain.afa.us.toml`** (Windows: **`RSPRecomp.exe …`** per upstream **`BUILDING.md`** §4), or from repo root **`pwsh tools/phase6_rsprecomp_afa.ps1`** (**`-RomPath`** / **`-CopyTools`** as needed; script refuses **`text_offset`/`text_size` = `0x0`** unless **`-SkipOffsetGuard`**).
4. **CMake (`lib/Zelda64Recomp/CMakeLists.txt`):** `SOURCES` always lists **`rsp/aspMain.cpp`** and **`rsp/njpgdspMain.cpp`**. Only when **`_AERO_PATCH_RSP_STUBS`** is **ON** does CMake remove those paths and link **`tools/phase6_no_mm_engine/rsp_*_stub.cpp`** if either generated file is missing; if **both** **`rsp/*.cpp`** exist, real RSP is linked even in stub patch mode. When **`_AERO_PATCH_RSP_STUBS`** is **OFF** (stock MM configure, or **`AEROASSAULT64_AFA_PRODUCT` + `AEROASSAULT64_AFA_RETAIL_PIPELINES`** without **`NO_MM_ROM`**), that substitution block does not run — **both** **`rsp/*.cpp`** must be present or the build fails. See:

```304:320:lib/Zelda64Recomp/CMakeLists.txt
if(_AERO_PATCH_RSP_STUBS)
    set(_AERO_RSP_ASP "${CMAKE_SOURCE_DIR}/rsp/aspMain.cpp")
    set(_AERO_RSP_NJPG "${CMAKE_SOURCE_DIR}/rsp/njpgdspMain.cpp")
    if(EXISTS "${_AERO_RSP_ASP}" AND EXISTS "${_AERO_RSP_NJPG}")
        message(STATUS "Aero stub mode: found rsp/aspMain.cpp and rsp/njpgdspMain.cpp — linking RSPRecomp output (stubs skipped). See BUILDING.md section 4.")
    else()
        list(REMOVE_ITEM SOURCES
            ${_AERO_RSP_ASP}
            ${_AERO_RSP_NJPG}
        )
        list(APPEND SOURCES
            ${AEROASSAULT64_STUB_DIR}/rsp_aspMain_stub.cpp
            ${AEROASSAULT64_STUB_DIR}/rsp_njpgdsp_stub.cpp
        )
        message(STATUS "Aero stub mode: RSP stubs (missing rsp/*.cpp). For AFA, add RSPRecomp outputs or keep stubs; see config/afa_rsp/README.txt.")
    endif()
endif()
```

**MSVC vs RSPRecomp output:** If **`rsp/aspMain.cpp`** / **`rsp/njpgdspMain.cpp`** fail to compile (**C2094** undefined label **`L_…`**, or **C2106** **`0 = RSP_ADD32`** for **`addiu $zero`**), the usual cause is an **`text_offset`/`text_size`** window that does not cover every indirect branch RSPRecomp records ( **`extra_indirect_branch_targets`** in the TOML only adds **switch** cases — it does not create missing **labels**). Until the cart slice matches the full static graph, configure the engine with **`AEROASSAULT64_AFA_RSP_FORCE_STUBS=ON`** together with **`AEROASSAULT64_AFA_PRODUCT`** so CMake links **`tools/phase6_no_mm_engine/rsp_*_stub.cpp`** instead of **`rsp/*.cpp`** — you still get **`Zelda64Recompiled.exe`** for launcher / ROM-slot / bring-up, but **not** full hardware-accurate RSP. Example:

```text
cmake -S lib/Zelda64Recomp -B build-engine-vs2022 -G "Visual Studio 17 2022" -A x64 ^
  -DAEROASSAULT64_AFA_PRODUCT=ON -DAEROASSAULT64_AFA_RSP_FORCE_STUBS=ON
cmake --build build-engine-vs2022 --config Release --target Zelda64Recompiled
```

Or **`.\tools\phase6_engine_cmake_vs2022.ps1 -Mode All -AfaProduct -AfaRspForceStubs`** (**`build-engine-vs2022/`**), or **`.\tools\phase6_engine_cmake.ps1 -Mode All -AfaProduct -AfaRspForceStubs`** (**`build-engine/`**, Ninja). Re-run CMake with **`AFA_RSP_FORCE_STUBS` OFF** once **`RSPRecomp.exe`** output is MSVC-clean.

### 2. Patches (`patches/patches.elf` → `N64Recomp patches.toml` → `RecompiledPatches/`)

1. Under **`lib/Zelda64Recomp/patches/`**, maintain a **`Makefile`** (or equivalent) that builds **`patches/patches.elf`** from MIPS patch objects for **AFA**, not MM.
2. Add **AFA** symbol TOMLs (upstream pattern: **`Zelda64RecompSyms/mm.us.rev1.*.toml`** — you need **`afa…`** equivalents or a single custom set) so **`patches.toml`** `[input]` **`func_reference_syms_file`** / **`data_reference_syms_files`** resolve.
3. Copy **`../../config/afa_engine/patches.toml.template`** to **`lib/Zelda64Recomp/patches.toml`** and edit paths to your AFA **`patches.elf`** and syms files (upstream reference: [patches.toml](https://raw.githubusercontent.com/Mr-Wiseguy/Zelda64Recomp/master/patches.toml)).
4. Run **`./N64Recomp patches.toml`** from **engine root** (same as upstream **`CMakeLists.txt`** custom command in the **non-stub** `PatchesLib` branch).
5. **CMake:** **`_AERO_PATCH_RSP_STUBS`** stays **ON** for **`AEROASSAULT64_NO_MM_ROM`** or for **`AEROASSAULT64_AFA_PRODUCT`** **without** **`AEROASSAULT64_AFA_RETAIL_PIPELINES`** (`CMakeLists.txt` lines defining **`_AERO_PATCH_RSP_STUBS`**). For **AFA USA compile-time hooks** (`#if AEROASSAULT64_AFA_PRODUCT`) **and** the full upstream-style **`patches.elf` / `patches.toml`** **`PatchesLib`** path, configure with **`AEROASSAULT64_AFA_PRODUCT=ON`** and **`AEROASSAULT64_AFA_RETAIL_PIPELINES=ON`** (repo root **`CMakeLists.txt`**, **`tools/phase6_engine_cmake*.ps1 -AfaProduct -AfaRetailPipelines`**, or presets **`engine-superbuild-*-afa-product-retail`**). See **`AFA_PORT.md`** and **`lib/Zelda64Recomp/CMakeLists.txt`** **`option(AEROASSAULT64_AFA_RETAIL_PIPELINES …)`**.

### 3. Game / UI

- Grow **`zelda64::game_warps`** in **`src/game/scene_table.cpp`** (currently empty under **`AEROASSAULT64_AFA_PRODUCT`**) as you define AFA areas / warps.
- Replace MM-specific **`src/game/*.cpp`** and **`launcher.rml`** (under engine **`assets/`**, **`BUILDING.md`** §6) incrementally; this file’s checklist below tracks scope.

## References (upstream / SDK)

- **Engine build:** **`BUILDING.md`** (MM flow: N64Recomp, RSPRecomp, CMake).
- **RSPRecomp TOML keys** (same schema as MM): upstream **`aspMain.us.rev1.toml`** / **`njpgdspMain.us.rev1.toml`** — [aspMain](https://raw.githubusercontent.com/Mr-Wiseguy/Zelda64Recomp/master/aspMain.us.rev1.toml), [njpgdspMain](https://raw.githubusercontent.com/Mr-Wiseguy/Zelda64Recomp/master/njpgdspMain.us.rev1.toml).
- **Patches pipeline:** upstream **`patches.toml`** — [patches.toml](https://raw.githubusercontent.com/Mr-Wiseguy/Zelda64Recomp/master/patches.toml) (`elf_path`, `func_reference_syms_file`, `data_reference_syms_files`, `output_binary_path`, `N64Recomp patches.toml` step in **`CMakeLists.txt`** when **`_AERO_PATCH_RSP_STUBS`** is OFF).
- **ROM hash / `GameEntry`:** **`lib/N64ModernRuntime/librecomp/src/recomp.cpp`** (`XXH3_64bits`, `check_hash`); **`src/main/main.cpp`** `make_supported_games()`.

## Repo-root templates (copy into engine when values are known)

| Artifact | Template | Action |
|----------|----------|--------|
| RSP aspMain | **`../../config/afa_rsp/aspMain.afa.us.template.toml`** | **`text_offset`/`text_size`/`text_address`** committed (see file + **`tools/rsprecomp_*.py`**); copy to engine root; run **`RSPRecomp.exe`** per **`BUILDING.md`** §4. |
| RSP njpgdsp | **`../../config/afa_rsp/njpgdspMain.afa.us.template.toml`** | Same. |
| Patches recomp | **`../../config/afa_engine/patches.toml.template`** | Point `elf_path` at AFA **`patches/patches.elf`**; add **`Zelda64RecompSyms/`** (or equivalent) for **AFA**; wire **`patches/`** **`Makefile`** for MIPS objects. |
| Assets | **`../../config/afa_assets/README.txt`** | Mirror **`BUILDING.md`** §6 layout under engine **`assets/`** for AFA RML/textures. |

## PIN — fix `afa.n64.us.datasyms.toml` (regenerate N64Recomp)

Wrong **data** VRAM in **`lib/Zelda64Recomp/Zelda64RecompSyms/afa.n64.us.datasyms.toml`** forces host stubs in **`src/game/afa_game_hooks.cpp`**. Correct these (game truth: **`asm/`**, **`config/symbol_addrs.txt`**, **`config/undefined_syms_auto.txt`**) then re-run **`tools/phase5_run_aero_n64recomp.ps1`** (or **`phase6_full_recomp_afa.ps1`**) so **`RecompiledFuncs/`** matches retail addresses.

| Symbol | Correct VRAM | Wrong in datasyms (today) | Symptom |
|--------|--------------|---------------------------|---------|
| `D_802516D8` | `0x802516D8` | `0x809F99B8` | Scheduler run-queue head; idle/spin without dispatch |
| `D_802516E0` | `0x802516E0` | `0x809F99C0` | Current-thread pointer; `func_80237360` skips `func_80241F54` |
| `D_80280EB8` | `0x80280EB8` | `0x802751E8` | Boot main TCB (`func_8023169C` — **`asm/31B30.s`**) vs separate `D_80281068` |
| `main_BSS_START` | `0x80256D70` | `0x809FF050` | Entry BSS clear targets wrong RAM (**`asm/1000.s`**) |
| `D_80282B60` | `0x80282B60` | `0x80276E90` | Entry stack pointer before `main` |
| `D_80251A70` | `0x80251A70` | `0x809F9D50` | Message-queue head (`.word D_80285F40` — **`asm/data/4C050.data.s`**); **`func_80247090`** AV |
| *(slab)* | `0x80251680`–`~0x80251E80` | `0x809F9960`–`~0x809F9E60` | **+0x7A82E0** on every symbol in this block (`D_80251774`, …); **`func_802420E0`** AV / bogus **`rdram`** in VS |

**Durable pipeline (repo root, after `build/aerofighters_assault.elf` exists):**

1. **`pwsh tools/phase6_afa_generate_syms.ps1`** — `N64Recomp --dump-context` then **`tools/afa_fixup_datasyms_vram.py`** (rewrites **`Zelda64RecompSyms/afa.n64.us.datasyms*.toml`** `vram` from symbol names like **`D_802516D8`**).
2. **`pwsh tools/phase5_run_aero_n64recomp.ps1`** — CPU recomp, then **`tools/afa_fixup_recompiled_vram.py`** (fixes **`lui 0x80A0` / `lw -0x65AC`** style slips in **`RecompiledFuncs/*.c`**).
3. Rebuild engine (**`cmake --build … --target Zelda64Recompiled`**).

Host stubs in **`afa_game_hooks.cpp`** remain for MMIO (**`func_8023E3A0`**, SI **`0xA440`**, cart PIO) until those paths are ported; scheduler/entrypoint stubs can be trimmed after verifying a clean boot on regenerated **`RecompiledFuncs`**.

Long-term ELF fix: reconcile splat **`main`** BSS VMA (**`config/splat.yaml`**, **`Docs/Workflow.md` Phase 3**) so the linked ELF stops emitting the **`0x809F99xx`** band; then steps 1–2 should become no-ops.

## In-tree progress (this fork)

- [x] **Launcher / `GameEntry` order** — **`primary_supported_game_id()`**, AFA first when **`AEROASSAULT64_AFA_PRODUCT`** + **`WITH_AFA_USA`** (`main.cpp`, `ui_launcher.cpp`, `include/zelda_game.h`).
- [x] **`scene_table.cpp`** — MM **`game_warps`** table omitted when **`AEROASSAULT64_AFA_PRODUCT`** (empty vector until AFA data exists).
- [x] **Embedded MM mod** — **`mm_recomp_dpad_builtin`** not registered when **`AEROASSAULT64_AFA_PRODUCT`** (`main.cpp`).
- [x] **One-shot CPU + RSP** — **`tools/phase6_full_recomp_afa.ps1`** (**`make phase6-full-recomp-afa-win`**) chains junction + **`phase5_run_aero_n64recomp.ps1`** + **`phase6_rsprecomp_afa.ps1`** (see tracker opening section).
- [x] **MSVC RSP stub override** — **`AEROASSAULT64_AFA_RSP_FORCE_STUBS`** (**`lib/Zelda64Recomp/CMakeLists.txt`** **`option`**) + **`-AfaRspForceStubs`** on **`tools/phase6_engine_cmake.ps1`** / **`phase6_engine_cmake_vs2022.ps1`** when generated **`rsp/*.cpp`** does not yet compile (**§1** MSVC paragraph above).
- [~] **`patches/`** + **`patches.toml`** + **`RecompiledPatches/`** — **`Zelda64RecompSyms/afa.n64.us.*`** via **`tools/phase6_afa_generate_syms.ps1`**; **`patches/afa/README.txt`** tracks MIPS patch port (MM **`patches/*.c`** not usable). **`patches.elf`** + N64Recomp still blocked until AFA patch sources exist.
- [ ] **`src/game/`** — replace MM-specific **`config`**, **`debug`**, **`quicksaving`**, **`recomp_*_api`**, **`rom_decompression`**, etc., with AFA behavior as recomp exports stabilize.
- [ ] **`src/ui/`** — **`launcher.rml`** / assets strings (repo **`assets/`** may be submodule or external); align with **`mm_rom_valid`** model binding rename when RML is forked.
- [x] **CMake** — **`AEROASSAULT64_AFA_RETAIL_PIPELINES`** with **`AFA_PRODUCT`** enables real **`PatchesLib`** while keeping **`#if AEROASSAULT64_AFA_PRODUCT`** (`CMakeLists.txt`, repo root **`CMakeLists.txt`**, **`CMakePresets.json`**, **`tools/phase6_engine_cmake*.ps1`**).
