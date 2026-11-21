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

// ------------------------------------------------------------------ pantalla de carga
namespace Ounce::Hos {
namespace {

// UTF-8 -> página de códigos 437 (la fuente de la consola de libnx), para las tildes.
std::string ToCp437(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x80) {
            out += static_cast<char>(c);
            continue;
        }
        if ((c & 0xE0) == 0xC0 && i + 1 < in.size()) {
            const unsigned cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(in[++i]) & 0x3F);
            switch (cp) {
            case 0xE1: out += '\xA0'; break; // á
            case 0xE9: out += '\x82'; break; // é
            case 0xED: out += '\xA1'; break; // í
            case 0xF3: out += '\xA2'; break; // ó
            case 0xFA: out += '\xA3'; break; // ú
            case 0xF1: out += '\xA4'; break; // ñ
            case 0xD1: out += '\xA5'; break; // Ñ
            case 0xFC: out += '\x81'; break; // ü
            case 0xC9: out += '\x90'; break; // É
            case 0xBF: out += '\xA8'; break; // ¿
            case 0xA1: out += '\xAD'; break; // ¡
            default: out += '?'; break;
            }
            continue;
        }
        // Secuencias de 3-4 bytes (™, ®...): se saltan.
        while (i + 1 < in.size() && (static_cast<unsigned char>(in[i + 1]) & 0xC0) == 0x80) {
            ++i;
        }
    }
    return out;
}

struct Canvas {
    std::uint32_t* px;
    std::uint32_t w, h;
    bool bgra;

    std::uint32_t Color(std::uint32_t rgb) const {
        const std::uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        return bgra ? (0xFF000000u | (r << 16) | (g << 8) | b) : (0xFF000000u | (b << 16) | (g << 8) | r);
    }
    void Rect(int x, int y, int rw, int rh, std::uint32_t rgb) const {
        const std::uint32_t c = Color(rgb);
        for (int j = std::max(0, y); j < std::min<int>(h, y + rh); ++j) {
            for (int i = std::max(0, x); i < std::min<int>(w, x + rw); ++i) {
                px[size_t(j) * w + i] = c;
            }
        }
    }
    // Glifos de 16x16 de la fuente de libnx: fila r = palabra r, bit 15-i = columna i.
    void Text(int x, int y, const std::string& utf8, int scale, std::uint32_t rgb) const {
        const auto* font = static_cast<const std::uint16_t*>(consoleGetDefault()->font.gfx);
        const std::uint32_t c = Color(rgb);
        const std::string text = ToCp437(utf8);
        for (size_t n = 0; n < text.size(); ++n) {
            const std::uint16_t* glyph = font + 16 * static_cast<unsigned char>(text[n]);
            const int gx = x + int(n) * 16 * scale;
            for (int r = 0; r < 16; ++r) {
                for (int i = 0; i < 16; ++i) {
                    if (!(glyph[r] & (0x8000 >> i))) {
                        continue;
                    }
                    for (int sy = 0; sy < scale; ++sy) {
                        const int py = y + r * scale + sy;
                        if (py < 0 || py >= int(h)) continue;
                        for (int sx = 0; sx < scale; ++sx) {
                            const int pxx = gx + i * scale + sx;
                            if (pxx >= 0 && pxx < int(w)) px[size_t(py) * w + pxx] = c;
                        }
                    }
                }
            }
        }
    }
    static int TextWidth(const std::string& utf8, int scale) {
        return int(ToCp437(utf8).size()) * 16 * scale;
    }
    void Centered(int y, const std::string& text, int scale, std::uint32_t rgb) const {
        Text((int(w) - TextWidth(text, scale)) / 2, y, text, scale, rgb);
    }
};

} // namespace

void PaintLoadingScreen(std::uint32_t* pixels, std::uint32_t width, std::uint32_t height,
                        bool bgra, const std::string& title, const std::string& status,
                        float progress, std::uint64_t tick) {
    const Canvas cv{pixels, width, height, bgra};
    const float k = float(height) / 720.0f; // tamaños pensados para 720p
    const int big = std::max(1, int(4 * k + 0.5f));
    const int mid = std::max(1, int(2 * k + 0.5f));

    cv.Rect(0, 0, int(width), int(height), 0x0F1216);

    const int title_y = int(height * 0.30f);
    const std::string dots(1 + (tick / 8) % 3, '.');
    const std::string head = "Cargando NX";
    // Puntos animados fuera del centrado, para que el texto no baile.
    const int head_x = (int(width) - Canvas::TextWidth(head, big)) / 2;
    cv.Text(head_x, title_y, head + dots, big, 0xFFFFFF);

    std::string game = title;
    const size_t max_chars = size_t(width / (16 * mid)) - 4;
    if (game.size() > max_chars) game = game.substr(0, max_chars - 3) + "...";
    cv.Centered(title_y + 16 * big + int(24 * k), game, mid, 0x00C3E3);

    const int bar_w = int(width * 0.6f);
    const int bar_h = std::max(6, int(14 * k));
    const int bar_x = (int(width) - bar_w) / 2;
    const int bar_y = int(height * 0.62f);
    cv.Rect(bar_x, bar_y, bar_w, bar_h, 0x2A3038);
    if (progress >= 0.0f) {
        const float p = std::min(progress, 1.0f);
        cv.Rect(bar_x, bar_y, int(bar_w * p), bar_h, 0x00C3E3);
    } else {
        const int seg = bar_w / 4;
        const int pos = int((tick * 12) % size_t(bar_w + seg)) - seg;
        cv.Rect(std::max(bar_x, bar_x + pos), bar_y,
                std::min(seg, bar_w - std::max(0, pos)) + std::min(0, pos), bar_h, 0x00C3E3);
    }
    cv.Centered(bar_y + bar_h + int(20 * k), status, mid, 0xB8C0CC);
    cv.Centered(int(height) - int(48 * k), "Eden para Switch 2", std::max(1, mid - 1), 0x5A6470);
}

} // namespace Ounce::Hos
