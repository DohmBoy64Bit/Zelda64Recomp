# Aero Fighters Assault — full retail runtime port (tracker)

This file backs the **Docs/Workflow.md** checklist row *AFA fork — full retail runtime*. Paths are under **`lib/Zelda64Recomp/`** unless noted.

## Execution order (do this when ROM analysis is far enough along)

### 1. RSP (RSPRecomp → `rsp/*.cpp`)

1. From **Ghidra + splat / ROM**, fill **`text_offset`**, **`text_size`**, **`text_address`** in the repo templates:
   - **`../../config/afa_rsp/aspMain.afa.us.template.toml`**
   - **`../../config/afa_rsp/njpgdspMain.afa.us.template.toml`**
2. Copy the filled files into **`lib/Zelda64Recomp/`** (engine root), e.g. **`aspMain.afa.us.toml`**, **`njpgdspMain.afa.us.toml`**.
3. Place **`afa.n64.us.z64`** next to those TOMLs (or adjust **`rom_file_path`** to match where the byteswapped USA ROM lives) — same idea as **`mm.us.rev1.rom_uncompressed.z64`** for MM in **`BUILDING.md`** §3–4.
4. From **engine root**, run **`RSPRecomp.exe`** the same way as upstream:
   - **`./RSPRecomp aspMain.us.rev1.toml`** → your **`./RSPRecomp aspMain.afa.us.toml`** (Windows: **`RSPRecomp.exe …`** per **`BUILDING.md`** §4).
5. Confirm **`lib/Zelda64Recomp/rsp/aspMain.cpp`** and **`rsp/njpgdspMain.cpp`** exist (gitignored upstream). CMake then links real RSP when both files exist **even in stub mode** (see **`CMakeLists.txt`** `rsp/*.cpp` / stub branch).

### 2. Patches (`patches/patches.elf` → `N64Recomp patches.toml` → `RecompiledPatches/`)

1. Under **`lib/Zelda64Recomp/patches/`**, maintain a **`Makefile`** (or equivalent) that builds **`patches/patches.elf`** from MIPS patch objects for **AFA**, not MM.
2. Add **AFA** symbol TOMLs (upstream pattern: **`Zelda64RecompSyms/mm.us.rev1.*.toml`** — you need **`afa…`** equivalents or a single custom set) so **`patches.toml`** `[input]` **`func_reference_syms_file`** / **`data_reference_syms_files`** resolve.
3. Copy **`../../config/afa_engine/patches.toml.template`** to **`lib/Zelda64Recomp/patches.toml`** and edit paths to your AFA **`patches.elf`** and syms files (upstream reference: [patches.toml](https://raw.githubusercontent.com/Mr-Wiseguy/Zelda64Recomp/master/patches.toml)).
4. Run **`./N64Recomp patches.toml`** from **engine root** (same as upstream **`CMakeLists.txt`** custom command in the **non-stub** `PatchesLib` branch).
5. **CMake today:** **`_AERO_PATCH_RSP_STUBS`** is **ON** when **`AEROASSAULT64_NO_MM_ROM`** **or** **`AEROASSAULT64_AFA_PRODUCT`** is **ON** (`CMakeLists.txt`). That forces **stub `PatchesLib`** and **skips** the **`patches.elf` / `patches.toml`** pipeline. To use the **full** upstream-style patches path, configure with **both** stub flags **OFF** for that build (you lose **`#if AEROASSAULT64_AFA_PRODUCT`** compile-time hooks unless we add a separate CMake option later — track as follow-up).

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
- [ ] **CMake (optional)** — allow **`AEROASSAULT64_AFA_PRODUCT=ON`** with **real** **`PatchesLib`** / **`patches.toml`** (today: stub path OR full path — not both; see **Execution order §2**).
