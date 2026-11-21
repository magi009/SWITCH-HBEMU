// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Único archivo de common/ que incluye <switch.h>. No incluir headers de Eden aquí.

#include <bit>

#include <switch.h>

#include "common/horizon/horizon_os.h"

namespace Common::Horizon {

std::uint64_t TotalMemorySize() {
    u64 value = 0;
    svcGetInfo(&value, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    return value;
}

std::uint64_t UsedMemorySize() {
    u64 value = 0;
    svcGetInfo(&value, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    return value;
}

void SetCurrentThreadPriority(int horizon_priority) {
    svcSetThreadPriority(threadGetCurHandle(), static_cast<u32>(horizon_priority));
}

int CoreCount() {
    u64 mask = 0;
    if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)) || mask == 0) {
        return 3; // lo mínimo que un juego tiene garantizado
    }
    return std::popcount(mask);
}

} // namespace Common::Horizon
