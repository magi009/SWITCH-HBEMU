// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Implementación de horizon.h. Este archivo incluye <switch.h> y NINGÚN header de Eden.

#include <switch.h>

#include "eden_ounce/horizon.h"

namespace Ounce::Hos {

static_assert(Button::A == HidNpadButton_A && Button::B == HidNpadButton_B);
static_assert(Button::X == HidNpadButton_X && Button::Y == HidNpadButton_Y);
static_assert(Button::StickL == HidNpadButton_StickL && Button::StickR == HidNpadButton_StickR);
static_assert(Button::L == HidNpadButton_L && Button::R == HidNpadButton_R);
static_assert(Button::ZL == HidNpadButton_ZL && Button::ZR == HidNpadButton_ZR);
static_assert(Button::Plus == HidNpadButton_Plus && Button::Minus == HidNpadButton_Minus);
static_assert(Button::Left == HidNpadButton_Left && Button::Up == HidNpadButton_Up);
static_assert(Button::Right == HidNpadButton_Right && Button::Down == HidNpadButton_Down);
static_assert(Button::AnySL == HidNpadButton_AnySL && Button::AnySR == HidNpadButton_AnySR);

void SetNoSuspend() {
    appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
}

bool MainLoop() {
    return appletMainLoop();
}

bool IsDocked() {
    return appletGetOperationMode() == AppletOperationMode_Console;
}

bool IsInFocus() {
    return appletGetFocusState() == AppletFocusState_InFocus;
}

bool InitNetwork() {
    return R_SUCCEEDED(socketInitializeDefault());
}

void ExitNetwork() {
    socketExit();
}

void SleepNs(std::int64_t ns) {
    svcSleepThread(ns);
}

std::uint64_t MonotonicNs() {
    return armTicksToNs(armGetSystemTick());
}

void* DefaultWindow() {
    return nwindowGetDefault();
}

bool GetWindowSize(std::uint32_t& width, std::uint32_t& height) {
    u32 w = 0;
    u32 h = 0;
    if (R_FAILED(nwindowGetDimensions(nwindowGetDefault(), &w, &h)) || w == 0 || h == 0) {
        return false;
    }
    width = w;
    height = h;
    return true;
}

namespace {
std::array<PadState, MaxPads> g_pads{};

float Normalize(s32 v) {
    return static_cast<float>(v) / static_cast<float>(JOYSTICK_MAX);
}
} // namespace

void InitPads() {
    padConfigureInput(MaxPads, HidNpadStyleSet_NpadStandard);
    // Mando 0 = portátil + jugador 1 (como padInitializeDefault); el resto, uno por jugador.
    padInitializeDefault(&g_pads[0]);
    for (int i = 1; i < MaxPads; ++i) {
        padInitialize(&g_pads[i], static_cast<HidNpadIdType>(HidNpadIdType_No1 + i));
    }
}

void PollPads(std::array<PadSnapshot, MaxPads>& out) {
    for (int i = 0; i < MaxPads; ++i) {
        PadState& pad = g_pads[i];
        padUpdate(&pad);
        PadSnapshot& snap = out[i];
        snap.connected = padIsConnected(&pad);
        if (!snap.connected) {
            continue;
        }
        snap.buttons = padGetButtons(&pad);
        const HidAnalogStickState l = padGetStickPos(&pad, 0);
        const HidAnalogStickState r = padGetStickPos(&pad, 1);
        snap.lx = Normalize(l.x);
        snap.ly = Normalize(l.y);
        snap.rx = Normalize(r.x);
        snap.ry = Normalize(r.y);
    }
}

} // namespace Ounce::Hos
