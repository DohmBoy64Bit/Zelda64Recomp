#include "recomp_ui.h"
#include "zelda_config.h"
#include "zelda_game.h"
#include "zelda_support.h"
#include "librecomp/game.hpp"
#include "librecomp/boot_log.hpp"
#include "ultramodern/ultramodern.hpp"
#include "RmlUi/Core.h"
#include "nfd.h"
#include <cstdio>
#include <filesystem>
#include <string>

static std::string version_string;

Rml::DataModelHandle model_handle;
// RML model key remains mm_rom_valid (launcher.rml); value tracks primary_supported_game_id() ROM validity.
bool mm_rom_valid = false;

extern std::vector<recomp::GameEntry> supported_games;

void select_rom() {
    nfdnchar_t* native_path = nullptr;
    zelda64::open_file_dialog([](bool success, const std::filesystem::path& path) {
        if (success) {
            // librecomp/game.hpp: select_rom takes std::u8string& (mutable); primary_supported_game_id() is const&.
            std::u8string game_id = zelda64::primary_supported_game_id();
            recomp::RomValidationError rom_error = recomp::select_rom(path, game_id);
            switch (rom_error) {
                case recomp::RomValidationError::Good:
                    mm_rom_valid = true;
                    model_handle.DirtyVariable("mm_rom_valid");
                    break;
                case recomp::RomValidationError::FailedToOpen:
                    recompui::message_box("Failed to open ROM file.");
                    break;
                case recomp::RomValidationError::NotARom:
                    recompui::message_box("This is not a valid ROM file.");
                    break;
                case recomp::RomValidationError::IncorrectRom:
                    recompui::message_box("This ROM is not the correct game.");
                    break;
                case recomp::RomValidationError::NotYet:
                    recompui::message_box("This game isn't supported yet.");
                    break;
                case recomp::RomValidationError::IncorrectVersion:
                    recompui::message_box(
                            "This ROM is the correct game, but the wrong version.\nThis project requires the NTSC-U N64 version of the game.");
                    break;
                case recomp::RomValidationError::OtherError:
                    recompui::message_box("An unknown error has occurred.");
                    break;
            }
        }
    });
}

recompui::ContextId launcher_context;

recompui::ContextId recompui::get_launcher_context_id() {
	return launcher_context;
}

class LauncherMenu : public recompui::MenuController {
public:
    LauncherMenu() {
        std::u8string game_id = zelda64::primary_supported_game_id();
        mm_rom_valid = recomp::is_rom_valid(game_id);
    }
    ~LauncherMenu() override {

    }
    void load_document() override {
		launcher_context = recompui::create_context(zelda64::get_asset_path("launcher.rml"));
    }
    void register_events(recompui::UiEventListenerInstancer& listener) override {
        recompui::register_event(listener, "select_rom",
            [](const std::string& param, Rml::Event& event) {
                select_rom();
            }
        );
        recompui::register_event(listener, "rom_selected",
            [](const std::string& param, Rml::Event& event) {
                mm_rom_valid = true;
                model_handle.DirtyVariable("mm_rom_valid");
            }
        );
        recompui::register_event(listener, "start_game",
            [](const std::string& param, Rml::Event& event) {
                (void)param;
                (void)event;
                recomp_boot_log("[boot] launcher: start_game");
                recomp::start_game(zelda64::primary_supported_game_id());
                recomp_boot_log("[boot] launcher: start_game returned");
                recompui::hide_all_contexts();
                recomp_boot_log("[boot] launcher: hide_all_contexts done");
            }
        );
        recompui::register_event(listener, "open_controls",
            [](const std::string& param, Rml::Event& event) {
                recompui::set_config_tab(recompui::ConfigTab::Controls);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "open_settings",
            [](const std::string& param, Rml::Event& event) {
                recompui::set_config_tab(recompui::ConfigTab::General);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "open_mods",
            [](const std::string &param, Rml::Event &event) {
                recompui::set_config_tab(recompui::ConfigTab::Mods);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "exit_game",
            [](const std::string& param, Rml::Event& event) {
                ultramodern::quit();
            }
        );
    }
    void make_bindings(Rml::Context* context) override {
        Rml::DataModelConstructor constructor = context->CreateDataModel("launcher_model");

        constructor.Bind("mm_rom_valid", &mm_rom_valid);

        version_string = recomp::get_project_version().to_string();
        constructor.Bind("version_number", &version_string);

        model_handle = constructor.GetModelHandle();
    }
};

std::unique_ptr<recompui::MenuController> recompui::create_launcher_menu() {
    return std::make_unique<LauncherMenu>();
}

#include "aero_static_init_probe.h"
AERO_STATIC_INIT_PROBE(ui_launcher);
