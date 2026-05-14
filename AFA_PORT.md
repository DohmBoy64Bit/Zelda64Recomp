# Aero Fighters Assault — full retail runtime port (tracker)

This file backs the **Docs/Workflow.md** checklist row *AFA fork — full retail runtime*. Paths are under **`lib/Zelda64Recomp/`** unless noted.

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
- [ ] **Mods** — AFA **`mod_game_id`** manifests, optional embedded mod for dpad/features.
