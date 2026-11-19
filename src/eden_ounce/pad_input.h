// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace InputCommon {
class InputSubsystem;
}

namespace EdenOunce {

/// Configura HID de libounce para hasta 8 mandos.
void InitializePads();

/// Lee los mandos reales y los vuelca en el VirtualGamepad de Eden
/// (jugador N del juego = mando N de la consola).
/// Devuelve true si se pulsó el atajo de salida (+ y - a la vez durante 1 s).
bool PollPads(InputCommon::InputSubsystem& input_subsystem);

} // namespace EdenOunce
