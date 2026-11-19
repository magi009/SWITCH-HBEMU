// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>

#include "core/frontend/emu_window.h"

namespace Core {
class System;
}

namespace InputCommon {
class InputSubsystem;
}

namespace EdenOunce {

/// Ventana de emulación sobre el NWindow por defecto de libounce.
/// Vulkan (OunceVK) crea la superficie con VK_NN_vi_surface a partir de render_surface.
class EmuWindowOunce final : public Core::Frontend::EmuWindow {
public:
    EmuWindowOunce(InputCommon::InputSubsystem& input_subsystem, Core::System& system);
    ~EmuWindowOunce() override;

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override;
    bool IsShown() const override;

    /// Bucle principal: entrada, modo dock/portátil y eventos del applet.
    /// Devuelve false cuando hay que cerrar (HOME > cerrar, o + y - a la vez).
    bool PumpEvents();

    void RequestClose() { open = false; }
    bool IsOpen() const { return open; }

private:
    void UpdateLayout();

    InputCommon::InputSubsystem& input_subsystem;
    Core::System& system;
    std::atomic_bool open{true};
    bool last_docked = false;
    u32 width = 0;
    u32 height = 0;
};

} // namespace EdenOunce
