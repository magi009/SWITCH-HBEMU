// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Puente mínimo hacia Horizon (libounce) para el código común de Eden.
//
// libnx y Eden no pueden compartir una unidad de compilación (libnx define
// `struct Service`, `u128` y macros como BIT()). Este header solo usa tipos
// estándar; la implementación (horizon_os.cpp) es la única que incluye <switch.h>.

#pragma once

#ifdef __SWITCH2__

#include <cstdint>

namespace Common::Horizon {

/// Memoria total concedida al proceso (svcGetInfo TotalMemorySize).
std::uint64_t TotalMemorySize();

/// Memoria ya usada por el proceso (svcGetInfo UsedMemorySize).
std::uint64_t UsedMemorySize();

/// Cambia la prioridad del hilo actual. Horizon: número menor = más prioridad.
void SetCurrentThreadPriority(int horizon_priority);

/// Núcleos en los que puede correr el proceso.
int CoreCount();

} // namespace Common::Horizon

#endif // __SWITCH2__
