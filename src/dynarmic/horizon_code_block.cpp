// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Único archivo de Dynarmic que incluye libounce/kaiHEN. Ver horizon_code_block.h.

#include "dynarmic/backend/arm64/horizon_code_block.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <new>

#include <switch.h>
#include <kaihen.h>

extern "C" void eden_ounce_breadcrumb(const char* text) __attribute__((weak));

namespace Dynarmic::Backend::Arm64 {

namespace {
void Crumb(const char* text) {
    if (eden_ounce_breadcrumb) {
        eden_ounce_breadcrumb(text);
    }
}
} // namespace

static_assert(sizeof(HorizonCodeBlock::Range) == sizeof(KaihenJitRange));
static_assert(offsetof(HorizonCodeBlock::Range, offset) == offsetof(KaihenJitRange, offset));
static_assert(offsetof(HorizonCodeBlock::Range, size) == offsetof(KaihenJitRange, size));

struct HorizonCodeBlock::Impl {
    Jit jit{};
};

HorizonCodeBlock::HorizonCodeBlock(std::size_t size)
        : m_impl{std::make_unique<Impl>()}, m_size{size} {
    const Result rc = jitCreate(&m_impl->jit, size);
    if (R_FAILED(rc)) {
        char text[96];
        std::snprintf(text, sizeof(text), "JIT kaiHEN: jitCreate(%zu MiB) falló, Result 0x%X",
                      size >> 20, rc);
        Crumb(text);
        throw std::bad_alloc{};
    }
    {
        char text[96];
        std::snprintf(text, sizeof(text), "JIT kaiHEN: bloque de %zu MiB creado", size >> 20);
        Crumb(text);
    }
    m_w = static_cast<std::uint32_t*>(jitGetRwAddr(&m_impl->jit));
    m_x = static_cast<std::uint32_t*>(jitGetRxAddr(&m_impl->jit));
    m_dirty.reserve(64);
}

HorizonCodeBlock::~HorizonCodeBlock() {
    jitClose(&m_impl->jit);
}

void HorizonCodeBlock::invalidate_all() {
    m_dirty.clear();
    m_dirty.push_back(Range{0, m_size});
    Publish();
}

void HorizonCodeBlock::Publish() {
    if (m_dirty.empty()) {
        return;
    }
    // kaiHEN funde rangos adyacentes, pero ordenar y unir solapes aquí reduce
    // los bytes por IPC cuando un mismo bloque se re-enlaza varias veces.
    std::sort(m_dirty.begin(), m_dirty.end(),
              [](const Range& a, const Range& b) { return a.offset < b.offset; });
    std::size_t out = 0;
    for (std::size_t i = 1; i < m_dirty.size(); ++i) {
        Range& last = m_dirty[out];
        const Range& cur = m_dirty[i];
        if (cur.offset <= last.offset + last.size) {
            last.size = std::max(last.offset + last.size, cur.offset + cur.size) - last.offset;
        } else {
            m_dirty[++out] = cur;
        }
    }
    m_dirty.resize(out + 1);

    static bool first_publish = true;
    if (first_publish) {
        first_publish = false;
        Crumb("JIT kaiHEN: primera publicación de código");
    }
    const Result rc = kaihenJitPublishRanges(
        &m_impl->jit, reinterpret_cast<const KaihenJitRange*>(m_dirty.data()), m_dirty.size());
    if (R_FAILED(rc)) {
        // Sin publicación el invitado ejecutaría código viejo: mejor abortar ya.
        diagAbortWithResult(rc);
    }
    m_dirty.clear();
}

}  // namespace Dynarmic::Backend::Arm64
