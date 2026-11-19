// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Mandos reales (vía horizon.h) -> VirtualGamepad de Eden. Sin <switch.h>.

#include <array>

#include "eden_ounce/horizon.h"
#include "eden_ounce/pad_input.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "input_common/main.h"

namespace EdenOunce {
namespace {

namespace Btn = Ounce::Hos::Button;
using VB = InputCommon::VirtualGamepad::VirtualButton;
using VS = InputCommon::VirtualGamepad::VirtualStick;

struct ButtonMap {
    std::uint64_t hid;
    VB virt;
};

// Mismo orden que EmulatedController::LoadVirtualGamepadParams.
constexpr std::array<ButtonMap, 18> kButtons{{
    {Btn::A, VB::ButtonA},          {Btn::B, VB::ButtonB},
    {Btn::X, VB::ButtonX},          {Btn::Y, VB::ButtonY},
    {Btn::StickL, VB::StickL},      {Btn::StickR, VB::StickR},
    {Btn::L, VB::TriggerL},         {Btn::R, VB::TriggerR},
    {Btn::ZL, VB::TriggerZL},       {Btn::ZR, VB::TriggerZR},
    {Btn::Plus, VB::ButtonPlus},    {Btn::Minus, VB::ButtonMinus},
    {Btn::Left, VB::ButtonLeft},    {Btn::Up, VB::ButtonUp},
    {Btn::Right, VB::ButtonRight},  {Btn::Down, VB::ButtonDown},
    {Btn::AnySL, VB::ButtonSL},     {Btn::AnySR, VB::ButtonSR},
}};

// Índice de Eden para el mando portátil (NpadIdType::Handheld).
constexpr std::size_t kHandheldIndex = 8;

std::array<Ounce::Hos::PadSnapshot, Ounce::Hos::MaxPads> g_snap{};
std::array<std::uint64_t, Ounce::Hos::MaxPads> g_last_buttons{};
std::uint64_t g_exit_combo_start = 0;

void Feed(InputCommon::VirtualGamepad& gamepad, std::size_t player,
          const Ounce::Hos::PadSnapshot& pad, std::uint64_t changed) {
    for (const auto& map : kButtons) {
        if (changed & map.hid) {
            gamepad.SetButtonState(player, map.virt, (pad.buttons & map.hid) != 0);
        }
    }
    gamepad.SetStickPosition(player, VS::Left, pad.lx, pad.ly);
    gamepad.SetStickPosition(player, VS::Right, pad.rx, pad.ry);
}

} // namespace

void InitializePads() {
    Ounce::Hos::InitPads();
}

bool PollPads(InputCommon::InputSubsystem& input_subsystem) {
    auto* gamepad = input_subsystem.GetVirtualGamepad();
    Ounce::Hos::PollPads(g_snap);
    bool exit_requested = false;

    for (std::size_t i = 0; i < g_snap.size(); ++i) {
        const auto& pad = g_snap[i];
        if (!pad.connected) {
            continue;
        }
        const std::uint64_t changed = pad.buttons ^ g_last_buttons[i];
        g_last_buttons[i] = pad.buttons;

        Feed(*gamepad, i, pad, changed);
        if (i != 0) {
            continue;
        }
        // El jugador 1 también maneja el mando "portátil" del juego, para que
        // funcione igual en modo portátil y en dock.
        Feed(*gamepad, kHandheldIndex, pad, changed);

        // Atajo de salida: + y - a la vez durante 1 s.
        constexpr std::uint64_t combo = Btn::Plus | Btn::Minus;
        if ((pad.buttons & combo) == combo) {
            const std::uint64_t now = Ounce::Hos::MonotonicNs();
            if (g_exit_combo_start == 0) {
                g_exit_combo_start = now;
            } else if (now - g_exit_combo_start > 1'000'000'000ULL) {
                exit_requested = true;
            }
        } else {
            g_exit_combo_start = 0;
        }
    }
    return exit_requested;
}

} // namespace EdenOunce
