// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Selector de juegos y pantalla de error en modo texto. Incluye <switch.h> y
// NINGÚN header de Eden (ver horizon.h).

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

#include <switch.h>

#include "eden_ounce/horizon.h"

namespace fs = std::filesystem;

namespace Ounce::Hos {
namespace {

bool HasGameExtension(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".nsp" || ext == ".xci" || ext == ".nca" || ext == ".nro" || ext == ".nso";
}

std::vector<fs::path> ListGames(const std::string& dir) {
    std::vector<fs::path> games;
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && HasGameExtension(entry.path())) {
            games.push_back(entry.path());
        }
    }
    std::sort(games.begin(), games.end());
    return games;
}

} // namespace

std::string PickGame(const std::string& games_dir) {
    // La consola usa el NWindow por defecto: hay que cerrarla antes de crear la superficie Vulkan.
    PrintConsole* console = consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    const auto games = ListGames(games_dir);
    std::size_t selected = 0;
    std::string result;
    bool redraw = true;

    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);

        if (redraw) {
            consoleClear();
            std::printf("\x1b[1;1HEden para Switch 2 (eden-ounce) - experimental\n");
            std::printf("Juegos en %s\n\n", games_dir.c_str());
            if (games.empty()) {
                std::printf("No hay juegos. Copia tus dumps (.nsp/.xci/.nca/.nro)\n");
                std::printf("a esa carpeta y vuelve a abrir Eden.\n");
            }
            const std::size_t first = selected > 30 ? selected - 30 : 0;
            for (std::size_t i = first; i < games.size() && i < first + 36; ++i) {
                std::printf("%s %s\n", i == selected ? ">" : " ",
                            games[i].filename().string().c_str());
            }
            std::printf("\x1b[44;1HA: iniciar   +: salir");
            consoleUpdate(console);
            redraw = false;
        }

        if (down & HidNpadButton_Plus) {
            break;
        }
        if (!games.empty()) {
            if (down & HidNpadButton_AnyDown) {
                selected = (selected + 1) % games.size();
                redraw = true;
            } else if (down & HidNpadButton_AnyUp) {
                selected = (selected + games.size() - 1) % games.size();
                redraw = true;
            } else if (down & HidNpadButton_A) {
                result = games[selected].string();
                break;
            }
        }
    }
    consoleExit(console);
    return result;
}

void ShowFatal(const std::string& message) {
    PrintConsole* console = consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    std::printf("\x1b[1;1HEden no pudo arrancar:\n\n%s\n\nPulsa + para salir.", message.c_str());
    consoleUpdate(console);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        svcSleepThread(16'000'000);
    }
    consoleExit(console);
}

} // namespace Ounce::Hos
