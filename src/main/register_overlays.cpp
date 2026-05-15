#include "ovl_patches.hpp"

#include "aero_build_config.h"

// RecompiledFuncs compiles the game main as recomp_rom_main (see CMakeLists REcompiledFuncs).
// This TU includes recomp_overlays.inl, which must take the address of recomp_rom_main, not host ::main.
#if AEROASSAULT64_AFA_PRODUCT
#define main recomp_rom_main
#endif

#include "../../RecompiledFuncs/recomp_overlays.inl"

#include "librecomp/overlays.hpp"

void zelda64::register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections {
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays {
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
}
