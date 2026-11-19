// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Bloque de código JIT para Switch 2 (libounce + kaiHEN).
//
// En Horizon un juego no puede mapear memoria RWX ni llamar a las syscalls de
// code memory. kaiHEN resuelve el JIT con el proceso RO: jitCreate() devuelve
// una vista RX estable (la que se ejecuta) y un búfer de staging RW que NO es
// un alias físico de la RX. Lo escrito en staging solo se vuelve ejecutable al
// publicarlo (kaihenJitPublishRanges), que copia los rangos a través del alias
// que retiene RO, limpia la caché de datos e invalida la caché de
// instrucciones del hilo que llama.
//
// Esta clase imita la interfaz de oaknut::CodeBlock que usa el backend arm64
// de Dynarmic, separando wptr() (staging) de xptr() (RX):
//   - invalidate(x, n) no toca cachés: anota el rango como sucio.
//   - protect() publica todos los rangos sucios en una sola petición IPC.
//   - unprotect() no hace nada: el staging siempre es escribible.
//
// Cada AddressSpace pertenece a un núcleo emulado y solo su propio hilo emite
// y parchea código, así que nunca se modifica un bloque que otro hilo esté
// ejecutando (requisito de kaiHEN).
//
// IMPORTANTE: este header NO incluye <switch.h>. libnx define macros (BIT, ...)
// y tipos globales (u128, ...) que chocan con oaknut y Dynarmic; todo lo de
// libounce vive en horizon_code_block.cpp.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Dynarmic::Backend::Arm64 {

class HorizonCodeBlock {
public:
    explicit HorizonCodeBlock(std::size_t size);
    ~HorizonCodeBlock();

    HorizonCodeBlock(const HorizonCodeBlock&) = delete;
    HorizonCodeBlock& operator=(const HorizonCodeBlock&) = delete;
    HorizonCodeBlock(HorizonCodeBlock&&) = delete;
    HorizonCodeBlock& operator=(HorizonCodeBlock&&) = delete;

    /// Dirección ejecutable (la que Dynarmic usa como CodePtr).
    std::uint32_t* xptr() const { return m_x; }
    /// Dirección escribible (staging).
    std::uint32_t* wptr() const { return m_w; }
    /// Compatibilidad con oaknut::CodeBlock::ptr(): devuelve la vista RX.
    std::uint32_t* ptr() const { return m_x; }

    void unprotect() {}
    void protect() { Publish(); }

    /// Marca como sucio [xmem, xmem + size) (direcciones de la vista RX).
    void invalidate(std::uint32_t* xmem, std::size_t size) {
        const auto begin = reinterpret_cast<std::uintptr_t>(xmem);
        const auto base = reinterpret_cast<std::uintptr_t>(m_x);
        if (size == 0 || begin < base || begin + size > base + m_size) {
            return;
        }
        m_dirty.push_back(Range{begin - base, size});
    }

    /// Publica el búfer completo. Caro (copia m_size bytes): evitarlo en caliente.
    void invalidate_all();

    /// Mismo layout que KaihenJitRange (comprobado con static_assert en el .cpp).
    struct Range {
        std::uint64_t offset;
        std::uint64_t size;
    };

private:
    void Publish();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::size_t m_size;
    std::uint32_t* m_w = nullptr;
    std::uint32_t* m_x = nullptr;
    std::vector<Range> m_dirty;
};

}  // namespace Dynarmic::Backend::Arm64
