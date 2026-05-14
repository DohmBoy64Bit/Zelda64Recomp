#ifndef __ZELDA_GAME_H__
#define __ZELDA_GAME_H__

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace zelda64 {
    void quicksave_save();
    void quicksave_load();
    std::vector<uint8_t> decompress_mm(std::span<const uint8_t> compressed_rom);

    // supported_games[0] after main.cpp make_supported_games() ordering (AFA first when
    // AEROASSAULT64_AFA_PRODUCT && AEROASSAULT64_WITH_AFA_USA — see aero_build_config.h).
    const std::u8string& primary_supported_game_id();
};

#endif
