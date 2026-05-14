# Aero Fighters Assault — full retail runtime port (tracker)

This file backs the **Docs/Workflow.md** checklist row *AFA fork — full retail runtime*. Paths are under **`lib/Zelda64Recomp/`** unless noted.

## Execution order (do this when ROM analysis is far enough along)

### 1. RSP (RSPRecomp → `rsp/*.cpp`)

**Splat vs RSPRecomp:** **`config/splat.yaml`** and **`Docs/Workflow.md`** (Phase 2–3) document **MIPS** layout — **`ipl3`**, **`entry`**, **`main`**, **`data`**, **`rodata`**, **`bss`** in **`roms/afa.n64.us.z64`**. **RSPRecomp** needs a **different** triple per microcode: **`text_offset`** and **`text_size`** are the **file byte offset and length of that microcode’s text in the same cart ROM** named by **`rom_file_path`**. Splat does **not** output those fields; you derive them from ROM/Ghidra (e.g. following where the game loads **F3D / JPEG** RSP text into IMEM). **`config/afa_rsp/*.template.toml` in git still use `0x0` for offset/size** until someone commits the real numbers (from your analysis) into those files or into engine-root **`aspMain.afa.us.toml`** / **`njpgdspMain.afa.us.toml`**. **Heuristic aid:** run **`../../tools/ghidra/Find_RSP_Microcode_ROM_Hints.py`** in Ghidra (PyGhidra, same launch as **`Phase2_Closeout_Report.py`**) — it lists **`.ram` → `.rom`** reference hotspots and **`lui`/`addiu`** ROM pointers; you still confirm candidates in the Listing (DMA / **`OSTask`** context) before editing TOMLs.

1. Fill **`text_offset`**, **`text_size`**, **`text_address`** (and **`extra_indirect_branch_targets`** for **aspMain** when you have them — compare upstream [aspMain.us.rev1.toml](https://raw.githubusercontent.com/Mr-Wiseguy/Zelda64Recomp/master/aspMain.us.rev1.toml)) in:
   - **`../../config/afa_rsp/aspMain.afa.us.template.toml`**
   - **`../../config/afa_rsp/njpgdspMain.afa.us.template.toml`**
2. Copy the filled files into **`lib/Zelda64Recomp/`** (engine root), e.g. **`aspMain.afa.us.toml`**, **`njpgdspMain.afa.us.toml`**.
3. Place **`afa.n64.us.z64`** next to those TOMLs (or adjust **`rom_file_path`** to match where the byteswapped USA ROM lives) — same idea as **`mm.us.rev1.rom_uncompressed.z64`** for MM in upstream **`BUILDING.md`** §3–4 (when that doc is present in your engine tree).
4. From **engine root**, run **`RSPRecomp.exe`** the same way as upstream:
   - **`./RSPRecomp aspMain.us.rev1.toml`** → your **`./RSPRecomp aspMain.afa.us.toml`** (Windows: **`RSPRecomp.exe …`** per upstream **`BUILDING.md`** §4), or from repo root **`pwsh tools/phase6_rsprecomp_afa.ps1`** (**`-RomPath`** / **`-CopyTools`** as needed; script refuses **`text_offset`/`text_size` = `0x0`** unless **`-SkipOffsetGuard`**).
5. **CMake (`lib/Zelda64Recomp/CMakeLists.txt`):** `SOURCES` always lists **`rsp/aspMain.cpp`** and **`rsp/njpgdspMain.cpp`**. Only when **`_AERO_PATCH_RSP_STUBS`** is **ON** does CMake remove those paths and link **`tools/phase6_no_mm_engine/rsp_*_stub.cpp`** if either generated file is missing; if **both** **`rsp/*.cpp`** exist, real RSP is linked even in stub patch mode. When **`_AERO_PATCH_RSP_STUBS`** is **OFF** (stock MM configure, or **`AEROASSAULT64_AFA_PRODUCT` + `AEROASSAULT64_AFA_RETAIL_PIPELINES`** without **`NO_MM_ROM`**), that substitution block does not run — **both** **`rsp/*.cpp`** must be present or the build fails. See:

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
| RSP aspMain | **`../../config/afa_rsp/aspMain.afa.us.template.toml`** | Fill `text_offset` / `text_size` / `text_address` from Ghidra+splat; copy to engine root; run **`RSPRecomp.exe`** per **`BUILDING.md`** §4. |
| RSP njpgdsp | **`../../config/afa_rsp/njpgdspMain.afa.us.template.toml`** | Same. |
| Patches recomp | **`../../config/afa_engine/patches.toml.template`** | Point `elf_path` at AFA **`patches/patches.elf`**; add **`Zelda64RecompSyms/`** (or equivalent) for **AFA**; wire **`patches/`** **`Makefile`** for MIPS objects. |
| Assets | **`../../config/afa_assets/README.txt`** | Mirror **`BUILDING.md`** §6 layout under engine **`assets/`** for AFA RML/textures. |

## In-tree progress (this fork)

- [x] **Launcher / `GameEntry` order** — **`primary_supported_game_id()`**, AFA first when **`AEROASSAULT64_AFA_PRODUCT`** + **`WITH_AFA_USA`** (`main.cpp`, `ui_launcher.cpp`, `include/zelda_game.h`).
- [x] **`scene_table.cpp`** — MM **`game_warps`** table omitted when **`AEROASSAULT64_AFA_PRODUCT`** (empty vector until AFA data exists).
- [x] **Embedded MM mod** — **`mm_recomp_dpad_builtin`** not registered when **`AEROASSAULT64_AFA_PRODUCT`** (`main.cpp`).
- [ ] **RSP** — real TOMLs + **`rsp/*.cpp`** from **RSPRecomp** (turn off stub path or supply both **`rsp/*.cpp`**).
- [ ] **`patches/`** + **`patches.toml`** + **`RecompiledPatches/`** — full pipeline vs stubs (`CMakeLists.txt` **`PatchesLib`** branch).
- [ ] **`src/game/`** — replace MM-specific **`config`**, **`debug`**, **`quicksaving`**, **`recomp_*_api`**, **`rom_decompression`**, etc., with AFA behavior as recomp exports stabilize.
- [ ] **`src/ui/`** — **`launcher.rml`** / assets strings (repo **`assets/`** may be submodule or external); align with **`mm_rom_valid`** model binding rename when RML is forked.
- [x] **CMake** — **`AEROASSAULT64_AFA_RETAIL_PIPELINES`** with **`AFA_PRODUCT`** enables real **`PatchesLib`** while keeping **`#if AEROASSAULT64_AFA_PRODUCT`** (`CMakeLists.txt`, repo root **`CMakeLists.txt`**, **`CMakePresets.json`**, **`tools/phase6_engine_cmake*.ps1`**).
