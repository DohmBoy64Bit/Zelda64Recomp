AFA retail MIPS patches (build with: make AFA_PATCHES=1 in lib/Zelda64Recomp/patches/).

  required_patches_afa.c — RECOMP_PATCH func_80248B70 / func_80248C50 (PI DMA @ 0xA460, asm/49B20.s)
  to call recomp_load_overlays (same role as MM patches/required_patches.c Overlay_Load).

Ghidra: tools/ghidra/Find_AFA_Boot_Dma_Patch_Candidates.py (boot chain + PI xref list).

Pipeline:

  pwsh tools/phase6_afa_generate_syms.ps1
  copy config/afa_engine/patches.toml.template -> lib/Zelda64Recomp/patches.toml
  cd lib/Zelda64Recomp/patches && make AFA_PATCHES=1
  cd lib/Zelda64Recomp && ./N64Recomp.exe patches.toml
  cmake -DAEROASSAULT64_AFA_PRODUCT=ON -DAEROASSAULT64_AFA_RETAIL_PIPELINES=ON
