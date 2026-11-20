// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Implementación de horizon.h. Este archivo incluye <switch.h> y NINGÚN header de Eden.

#include <cstdio>
#include <ctime>

#include <switch.h>

#include "eden_ounce/horizon.h"

// ---------------------------------------------------------------- fallos de CPU
// libnx llama a __libnx_exception_handler (en su propia pila) cuando un hilo de este
// proceso provoca una excepción de CPU. Escribimos lo esencial y dejamos que el
// proceso termine (vuelve al hbmenu).
extern "C" {
alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
extern char __start__; // base de eden.nro (crt0 de libnx)
}

namespace {
constexpr const char* kCrashPath = "sdmc:/switch/eden/log/crash.txt";

const char* DescribeException(u32 desc) {
    switch (desc) {
    case ThreadExceptionDesc_InstructionAbort: return "InstructionAbort";
    case ThreadExceptionDesc_MisalignedPC: return "MisalignedPC";
    case ThreadExceptionDesc_MisalignedSP: return "MisalignedSP";
    case ThreadExceptionDesc_SError: return "SError";
    case ThreadExceptionDesc_BadSVC: return "BadSVC";
    case ThreadExceptionDesc_Trap: return "Trap (instrucción ilegal / brk)";
    case ThreadExceptionDesc_Other: return "Other (data abort u otro)";
    default: return "?";
    }
}
u64 NroOffset(u64 addr) {
    const u64 base = reinterpret_cast<u64>(&__start__);
    return addr >= base ? addr - base : 0;
}

// Pila de llamadas siguiendo los frame pointers (solo direcciones dentro de eden.nro).
void DumpBacktrace(FILE* f, u64 fp) {
    const u64 base = reinterpret_cast<u64>(&__start__);
    std::fprintf(f, "Llamadas:\n");
    for (int depth = 0; depth < 32 && fp != 0 && (fp & 0xF) == 0; ++depth) {
        const u64* frame = reinterpret_cast<const u64*>(fp);
        const u64 ret = frame[1];
        if (ret < base) {
            break;
        }
        std::fprintf(f, "  #%02d eden.nro+0x%lX\n", depth, ret - base);
        if (frame[0] <= fp) {
            break;
        }
        fp = frame[0];
    }
    std::fprintf(f, "\n");
}
} // namespace

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    FILE* f = std::fopen(kCrashPath, "a");
    if (!f) {
        return;
    }
    const u64 base = reinterpret_cast<u64>(&__start__);
    const auto off = [base](u64 a) { return a >= base ? a - base : 0; };
    std::fprintf(f, "=== Fallo de CPU (%s, desc=0x%X) ===\n", DescribeException(ctx->error_desc),
                 ctx->error_desc);
    std::fprintf(f, "base eden.nro = 0x%016lX\n", base);
    std::fprintf(f, "PC  = 0x%016lX  (eden.nro+0x%lX)\n", ctx->pc.x, off(ctx->pc.x));
    std::fprintf(f, "LR  = 0x%016lX  (eden.nro+0x%lX)\n", ctx->lr.x, off(ctx->lr.x));
    std::fprintf(f, "SP  = 0x%016lX  FP = 0x%016lX\n", ctx->sp.x, ctx->fp.x);
    std::fprintf(f, "FAR = 0x%016lX  ESR = 0x%08X  PSTATE = 0x%08X\n", ctx->far.x, ctx->esr,
                 ctx->pstate);
    for (int i = 0; i < 29; ++i) {
        std::fprintf(f, "X%-2d = 0x%016lX%s", i, ctx->cpu_gprs[i].x, (i % 2) ? "\n" : "   ");
    }
    DumpBacktrace(f, ctx->fp.x);
    std::fclose(f);
}

// ---------------------------------------------------------------- salidas "silenciosas"
// abort(), assert() de newlib, diagAbortWithResult, fatalThrow y svcBreak terminan el
// proceso sin pasar por el handler de excepciones ni por std::terminate (por ejemplo,
// un assert dentro del driver Vulkan o un fallo del JIT de kaiHEN). Con -Wl,--wrap
// (ver CMakeLists.txt) pasan antes por aquí y dejan rastro en crash.txt.
namespace {
void NoteSilentExit(const char* what, const char* detail) {
    FILE* f = std::fopen(kCrashPath, "a");
    if (!f) {
        return;
    }
    std::fprintf(f, "=== %s ===\n%s\n", what, detail ? detail : "");
    std::fprintf(f, "llamado desde eden.nro+0x%lX\n",
                 NroOffset(reinterpret_cast<u64>(__builtin_return_address(0))));
    DumpBacktrace(f, reinterpret_cast<u64>(__builtin_frame_address(0)));
    std::fclose(f);
}
} // namespace

extern "C" {
[[noreturn]] void __real_abort(void);
void __real___assert_func(const char* file, int line, const char* func, const char* expr);
[[noreturn]] void __real_diagAbortWithResult(Result res);
[[noreturn]] void __real_fatalThrow(Result err);
Result __real_svcBreak(u32 reason, uintptr_t address, uintptr_t size);

[[noreturn]] void __wrap_abort(void) {
    NoteSilentExit("abort()", nullptr);
    __real_abort();
}

void __wrap___assert_func(const char* file, int line, const char* func, const char* expr) {
    char detail[512];
    std::snprintf(detail, sizeof(detail), "assert(%s) en %s:%d (%s)", expr ? expr : "?",
                  file ? file : "?", line, func ? func : "?");
    NoteSilentExit("assert fallido", detail);
    __real___assert_func(file, line, func, expr);
}

[[noreturn]] void __wrap_diagAbortWithResult(Result res) {
    char detail[96];
    std::snprintf(detail, sizeof(detail), "Result 0x%X (módulo %u, descripción %u)", res,
                  R_MODULE(res), R_DESCRIPTION(res));
    NoteSilentExit("diagAbortWithResult", detail);
    __real_diagAbortWithResult(res);
}

[[noreturn]] void __wrap_fatalThrow(Result err) {
    char detail[96];
    std::snprintf(detail, sizeof(detail), "Result 0x%X (módulo %u, descripción %u)", err,
                  R_MODULE(err), R_DESCRIPTION(err));
    NoteSilentExit("fatalThrow", detail);
    __real_fatalThrow(err);
}

Result __wrap_svcBreak(u32 reason, uintptr_t address, uintptr_t size) {
    char detail[96];
    std::snprintf(detail, sizeof(detail), "reason=0x%X address=0x%lX size=0x%lX", reason,
                  static_cast<unsigned long>(address), static_cast<unsigned long>(size));
    NoteSilentExit("svcBreak", detail);
    return __real_svcBreak(reason, address, size);
}
} // extern "C"

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

void InstallCrashHandler() {
    // El handler ya está enlazado (símbolo fuerte); aquí solo dejamos una marca de sesión.
    FILE* f = std::fopen(kCrashPath, "a");
    if (f) {
        std::fprintf(f, "--- sesión iniciada (t=%ld) ---\n", static_cast<long>(std::time(nullptr)));
        std::fclose(f);
    }
}

void WriteCrashNote(const char* text) {
    FILE* f = std::fopen(kCrashPath, "a");
    if (f) {
        std::fprintf(f, "%s\n", text);
        std::fclose(f);
    }
}

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
