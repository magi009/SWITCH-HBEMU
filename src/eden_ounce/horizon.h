// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Capa de plataforma Horizon (libounce) para el frontend.
//
// libnx/libounce y Eden NO pueden compartir una unidad de compilación:
//   - libnx define `struct Service` y Eden un `namespace Service`;
//   - ambos definen `u128` con tipos distintos;
//   - libnx define macros como BIT() que rompen oaknut.
// Por eso solo horizon.cpp y launcher.cpp incluyen <switch.h>; el resto del
// frontend (y todo Eden) habla con la consola únicamente a través de este header,
// que solo usa tipos estándar.

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace Ounce::Hos {

// ------------------------------------------------------------------ applet
void SetNoSuspend();     ///< Seguir emulando aunque el applet pierda el foco.
bool MainLoop();         ///< appletMainLoop(): false si el sistema pide cerrar.
bool IsDocked();         ///< true en modo TV/dock.
bool IsInFocus();
void SleepNs(std::int64_t ns);
/// Inicializa los sockets BSD de libounce (los usa la red emulada y su socket de
/// interrupción). Devuelve false si no hay servicio de red; el emulador sigue igual.
bool InitNetwork();
void ExitNetwork();
std::uint64_t MonotonicNs();

// ------------------------------------------------------------------ fallos
/// Archivo donde se escriben los fallos (sdmc:/switch/eden/log/crash.txt).
/// Un fallo de CPU (acceso inválido, instrucción ilegal...) se escribe solo, con
/// PC/LR/FAR como desplazamiento dentro de eden.nro, para ubicarlo con addr2line.
void InstallCrashHandler();
/// Añade una línea de texto a crash.txt (excepciones de C++, std::terminate...).
void WriteCrashNote(const char* text);

// ------------------------------------------------------------------ vídeo
void* DefaultWindow();   ///< NWindow* por defecto (para VK_NN_vi_surface).
bool GetWindowSize(std::uint32_t& width, std::uint32_t& height);

// ------------------------------------------------------------------ entrada
/// Bits de botón con los mismos valores que HidNpadButton (comprobado en horizon.cpp).
namespace Button {
constexpr std::uint64_t A = 1ULL << 0, B = 1ULL << 1, X = 1ULL << 2, Y = 1ULL << 3;
constexpr std::uint64_t StickL = 1ULL << 4, StickR = 1ULL << 5;
constexpr std::uint64_t L = 1ULL << 6, R = 1ULL << 7, ZL = 1ULL << 8, ZR = 1ULL << 9;
constexpr std::uint64_t Plus = 1ULL << 10, Minus = 1ULL << 11;
constexpr std::uint64_t Left = 1ULL << 12, Up = 1ULL << 13, Right = 1ULL << 14, Down = 1ULL << 15;
constexpr std::uint64_t LeftSL = 1ULL << 24, LeftSR = 1ULL << 25;
constexpr std::uint64_t RightSL = 1ULL << 26, RightSR = 1ULL << 27;
constexpr std::uint64_t AnySL = LeftSL | RightSL, AnySR = LeftSR | RightSR;
} // namespace Button

struct PadSnapshot {
    bool connected = false;
    std::uint64_t buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0; ///< -1..1, Y positivo = arriba
};
constexpr int MaxPads = 8;

void InitPads();
void PollPads(std::array<PadSnapshot, MaxPads>& out);

// ------------------------------------------------------------------ UI de texto (launcher.cpp)
/// Selector de juegos sobre la consola de libounce. Devuelve "" si el usuario sale.
/// Libera el NWindow al terminar, para que Vulkan pueda usarlo.
std::string PickGame(const std::string& games_dir);
/// Muestra un error a pantalla completa hasta que se pulse +.
void ShowFatal(const std::string& message);

} // namespace Ounce::Hos
