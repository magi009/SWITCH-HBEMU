// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Sin <switch.h>: todo lo de la consola pasa por horizon.h.

#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/frontend/graphics_context.h"
#include "core/hle/service/am/applet_manager.h"
#include "eden_ounce/emu_window_ounce.h"
#include "eden_ounce/horizon.h"
#include "eden_ounce/pad_input.h"
#include "input_common/main.h"

namespace EdenOunce {
namespace {

// Vulkan no necesita contexto compartido; Eden solo pide uno "vacío".
class DummyContext final : public Core::Frontend::GraphicsContext {};

} // namespace

EmuWindowOunce::EmuWindowOunce(InputCommon::InputSubsystem& input_subsystem_, Core::System& system_)
    : input_subsystem{input_subsystem_}, system{system_} {
    input_subsystem.Initialize();
    InitializePads();

    window_info.type = Core::Frontend::WindowSystemType::NintendoSwitch;
    window_info.render_surface = Ounce::Hos::DefaultWindow();
    window_info.render_surface_scale = 1.0f;

    last_docked = Ounce::Hos::IsDocked();
    UpdateLayout();
    LOG_INFO(Frontend, "Ventana Ounce lista: {}x{} ({})", width, height,
             last_docked ? "dock" : "portatil");
}

EmuWindowOunce::~EmuWindowOunce() {
    input_subsystem.Shutdown();
}

std::unique_ptr<Core::Frontend::GraphicsContext> EmuWindowOunce::CreateSharedContext() const {
    return std::make_unique<DummyContext>();
}

bool EmuWindowOunce::IsShown() const {
    return Ounce::Hos::IsInFocus();
}

void EmuWindowOunce::UpdateLayout() {
    u32 w = 0;
    u32 h = 0;
    if (!Ounce::Hos::GetWindowSize(w, h)) {
        w = last_docked ? Layout::ScreenDocked::Width : Layout::ScreenUndocked::Width;
        h = last_docked ? Layout::ScreenDocked::Height : Layout::ScreenUndocked::Height;
    }
    width = w;
    height = h;
    UpdateCurrentFramebufferLayout(width, height);
}

bool EmuWindowOunce::PumpEvents() {
    if (!Ounce::Hos::MainLoop()) {
        open = false;
        return false;
    }

    if (PollPads(input_subsystem)) {
        LOG_INFO(Frontend, "Atajo de salida (+ y -) detectado");
        open = false;
        return false;
    }

    const bool docked = Ounce::Hos::IsDocked();
    if (docked != last_docked) {
        last_docked = docked;
        // El juego emulado también cambia de modo, como en una consola real.
        Settings::values.use_docked_mode.SetValue(docked ? Settings::ConsoleMode::Docked
                                                         : Settings::ConsoleMode::Handheld);
        if (system.IsPoweredOn()) {
            system.GetAppletManager().OperationModeChanged();
        }
        UpdateLayout();
        LOG_INFO(Frontend, "Modo cambiado a {}", docked ? "dock" : "portatil");
    }
    return open;
}

} // namespace EdenOunce
