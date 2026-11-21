// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// eden-ounce: frontend nativo de Eden para Switch 2 (KaiHEN + libounce).
//
// Arranque:
//   1. hbloader lanza eden.nro (con o sin una ruta en argv[1]).
//   2. Si no hay ruta, un selector de texto lista sdmc:/switch/eden/games.
//   3. Se configura Eden para esta plataforma (Dynarmic sin fastmem, Vulkan/OunceVK,
//      4 GiB de DRAM emulada) y se lanza el juego.
//
// Lo que el usuario necesita en la SD (dumpeado de SU consola, Eden no lo trae):
//   sdmc:/switch/eden/keys/prod.keys  (+ title.keys para juegos con ticket)
//   sdmc:/switch/eden/nand/system/... (firmware dumpeado, opcional pero recomendado)

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/cpu_manager.h"
#include "core/perf_stats.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "video_core/renderer_base.h"
#include "eden_ounce/emu_window_ounce.h"
#include "eden_ounce/horizon.h"
#include "input_common/main.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kGamesDir = "sdmc:/switch/eden/games";

/// Valores por defecto para esta plataforma. Se aplican antes de Initialize().
void ConfigureForSwitch2() {
    using namespace Settings;
    // Sin NCE: kaiHEN no deja mapear el código del invitado como ejecutable en
    // direcciones propias. Dynarmic recompila a la caché JIT de kaiHEN.
    values.cpu_backend.SetValue(CpuBackend::Dynarmic);
    // Sin mmap/aliasing no hay fastmem: Dynarmic usa la tabla de páginas.
    values.cpuopt_fastmem.SetValue(false);
    values.cpuopt_fastmem_exclusives.SetValue(false);
    values.use_multi_core.SetValue(true);
    // Sin fastmem cada acceso a memoria ya es caro: no sumar comprobaciones de precisión.
    // Son las mismas optimizaciones "inseguras" que usa Eden en Android.
    values.cpu_accuracy.SetValue(CpuAccuracy::Unsafe);
    values.cpuopt_unsafe_unfuse_fma.SetValue(true);
    values.cpuopt_unsafe_reduce_fp_error.SetValue(true);
    values.cpuopt_unsafe_ignore_standard_fpcr.SetValue(true);
    values.cpuopt_unsafe_inaccurate_nan.SetValue(true);
    values.cpuopt_unsafe_ignore_global_monitor.SetValue(true);
    // 4 GiB como la Switch 1; el pool de un juego de Switch 2 no da para más con Eden encima.
    values.memory_layout_mode.SetValue(MemoryLayout::Memory_4Gb);

    values.renderer_backend.SetValue(RendererBackend::Vulkan);
    values.vsync_mode.SetValue(VSyncMode::Fifo);
    values.resolution_setup.SetValue(ResolutionSetup::Res1X);
    values.use_disk_shader_cache.SetValue(true);
    values.use_asynchronous_gpu_emulation.SetValue(true);
    // Eden solo pone los valores rápidos de GPU si compila para Android; aquí hay que pedirlos.
    values.gpu_accuracy.SetValue(GpuAccuracy::Low);
    values.use_reactive_flushing.SetValue(false);
    values.async_presentation.SetValue(true);
    values.max_anisotropy.SetValue(AnisotropyMode::Default);
    values.use_vulkan_driver_pipeline_cache.SetValue(true);
    // Compilar shaders en segundo plano: el juego no se detiene esperando a NVK la primera
    // vez que ve un shader (puede faltar algún objeto un instante, en vez de congelarse).
    values.use_asynchronous_shaders.SetValue(true);
    // NVDEC por CPU necesita FFmpeg; desactivado hasta portarlo (los vídeos salen en negro).
    values.nvdec_emulation.SetValue(NvdecEmulation::Off);
    // Fase 1: sin backend de audio nativo todavía.
    values.sink_id.SetValue(AudioEngine::Null);

    values.use_docked_mode.SetValue(Ounce::Hos::IsDocked() ? ConsoleMode::Docked
                                                           : ConsoleMode::Handheld);
}

bool CheckKeys(std::string& error) {
    const auto keys = Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir) / "prod.keys";
    std::error_code ec;
    if (!fs::exists(keys, ec)) {
        error = "Falta " + keys.string() +
                "\nDumpea las claves de tu propia consola (p. ej. con Lockpick) y copialas ahi.";
        return false;
    }
    return true;
}

} // namespace

extern "C" void eden_ounce_jit_stats(u64* count, u64* bytes, u64* ns) __attribute__((weak));

namespace {

/// Cualquier excepción de C++ sin atrapar (en cualquier hilo) termina aquí: dejar
/// constancia en el log y en crash.txt antes de abortar, para no volver al hbmenu a ciegas.
[[noreturn]] void OnTerminate() {
    std::string reason = "std::terminate sin excepción activa";
    if (const auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            reason = std::string{"Excepción sin atrapar: "} + e.what();
        } catch (...) {
            reason = "Excepción sin atrapar (tipo desconocido)";
        }
    }
    // Primero crash.txt (escritura directa): vaciar el log puede bloquearse.
    Ounce::Hos::WriteCrashNote(reason.c_str());
    LOG_CRITICAL(Frontend, "{}", reason);
    Common::Log::Stop(); // vacía el log a la SD
    std::abort();
}

int RunEmulator(int argc, char** argv);

} // namespace

int main(int argc, char** argv) {
    // El log ya no se escribe línea a línea (frenaba la emulación con escrituras a la SD);
    // las migas de pan de crash.txt siguen siendo inmediatas.
    Settings::values.log_flush_line.SetValue(false);
    Ounce::Hos::InstallCrashHandler();
    std::set_terminate(OnTerminate);
    try {
        return RunEmulator(argc, argv);
    } catch (const std::exception& e) {
        const std::string reason = std::string{"Excepción: "} + e.what();
        Ounce::Hos::WriteCrashNote(reason.c_str());
        LOG_CRITICAL(Frontend, "{}", reason);
        Common::Log::Stop();
        Ounce::Hos::ShowFatal(reason + "\nRevisa sdmc:/switch/eden/log/");
        return 1;
    }
}

namespace {

int RunEmulator(int argc, char** argv) {
    // Que el juego emulado no se pause si el usuario abre el menu HOME un instante.
    Ounce::Hos::SetNoSuspend();

    Common::Log::Initialize();
    Common::Log::Start();
    LOG_INFO(Frontend, "eden-ounce arrancando");
    if (!Ounce::Hos::InitNetwork()) {
        LOG_WARNING(Frontend, "Sin sockets: la red emulada no funcionara");
    }

    std::string error;
    if (!CheckKeys(error)) {
        Ounce::Hos::ShowFatal(error);
        return 1;
    }

    std::string filepath = argc > 1 ? std::string{argv[1]} : std::string{};
    if (filepath.empty()) {
        filepath = Ounce::Hos::PickGame(kGamesDir);
        if (filepath.empty()) {
            return 0;
        }
    }
    LOG_INFO(Frontend, "Juego: {}", filepath);
    Ounce::Hos::Breadcrumb("juego elegido");

    ConfigureForSwitch2();

    Core::System system{};
    InputCommon::InputSubsystem input_subsystem{};

    system.Initialize();
    system.ApplySettings();
    Ounce::Hos::Breadcrumb("Core::System inicializado");

    auto window = std::make_unique<EdenOunce::EmuWindowOunce>(input_subsystem, system);

    system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    system.GetFileSystemController().CreateFactories(*system.GetFilesystem());
    system.GetUserChannel().clear();

    Service::AM::FrontendAppletParameters load_parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    Ounce::Hos::Breadcrumb("cargando juego (incluye crear el renderer Vulkan)");
    const Core::SystemResultStatus load_result = system.Load(*window, filepath, load_parameters);
    Ounce::Hos::Breadcrumb("system.Load terminado");
    if (load_result != Core::SystemResultStatus::Success) {
        const u16 loader_id = u16(Core::SystemResultStatus::ErrorLoader);
        std::string description = "Error de carga";
        if (u16(load_result) > loader_id) {
            description = Loader::GetResultStatusString(Loader::ResultStatus(u16(load_result) - loader_id));
        }
        LOG_CRITICAL(Frontend, "Load fallo: {} ({})", static_cast<u32>(load_result), description);
        window.reset();
        Ounce::Hos::ShowFatal("No se pudo cargar " + filepath + "\n" + description +
                  "\nRevisa el log en sdmc:/switch/eden/log/");
        return 1;
    }

    // Pantalla "Cargando NX": la pinta el propio renderer Vulkan hasta que el juego presente
    // su primer fotograma (la última imagen se queda en pantalla, sin negro de por medio).
    std::string title;
    if (system.GetAppLoader().ReadTitle(title) != Loader::ResultStatus::Success || title.empty()) {
        title = std::filesystem::path(filepath).stem().string();
    }
    u64 tick = 0;
    const auto draw_loading = [&](const std::string& status, float progress) {
        system.Renderer().DrawHostImage([&](std::span<u32> px, u32 w, u32 h, bool bgra) {
            Ounce::Hos::PaintLoadingScreen(px.data(), w, h, bgra, title, status, progress, tick);
        });
        ++tick;
    };
    draw_loading("Preparando la GPU", -1.0f);

    system.GPU().Start();
    Ounce::Hos::Breadcrumb("GPU iniciada");
    system.GetCpuManager().OnGpuReady();
    Ounce::Hos::Breadcrumb("CpuManager listo");

    if (Settings::values.use_disk_shader_cache.GetValue()) {
        // Los shaders guardados se compilan en otro hilo; este dibuja el progreso.
        std::atomic<int> stage{static_cast<int>(VideoCore::LoadCallbackStage::Prepare)};
        std::atomic<size_t> built{0}, total{0};
        std::atomic<bool> done{false};
        std::stop_source stop;
        std::thread loader{[&] {
            system.Renderer().ReadRasterizer()->LoadDiskResources(
                system.GetApplicationProcessProgramID(), stop.get_token(),
                [&](VideoCore::LoadCallbackStage s, size_t value, size_t count) {
                    stage = static_cast<int>(s);
                    built = value;
                    total = count;
                });
            done = true;
        }};
        while (!done) {
            const auto s = static_cast<VideoCore::LoadCallbackStage>(stage.load());
            const size_t n = built, t = total;
            if (s == VideoCore::LoadCallbackStage::Build && t > 0) {
                draw_loading(std::format("Cargando shaders {} / {}", n, t), float(n) / float(t));
            } else {
                draw_loading("Leyendo la caché de shaders", -1.0f);
            }
            if (!window->PumpEvents()) {
                stop.request_stop();
            }
            Ounce::Hos::SleepNs(33'000'000);
        }
        loader.join();
        if (stop.stop_requested()) {
            Ounce::Hos::Breadcrumb("salida pedida durante la carga de shaders");
            system.ShutdownMainProcess();
            window.reset();
            Ounce::Hos::ExitNetwork();
            return 0;
        }
    }
    draw_loading("Iniciando el juego", 1.0f);

    Ounce::Hos::Breadcrumb("caché de shaders leída");
    system.RegisterExitCallback([&window] {
        Ounce::Hos::Breadcrumb("el juego pidió salir");
        window->RequestClose();
    });

    void(system.Run());
    Ounce::Hos::Breadcrumb("emulación en marcha (system.Run)");

    // El emulador corre en sus propios hilos (núcleos de CPU, GPU, servicios);
    // este hilo solo bombea entrada y eventos del applet.
    u64 loops = 0;
    while (window->PumpEvents()) {
        Ounce::Hos::SleepNs(4'000'000); // 4 ms: suficiente para entrada a 240 Hz
        if (++loops % 1250 == 0) { // ~cada 5 s: el hilo principal sigue vivo + rendimiento
            const auto perf = system.GetAndResetPerfStats();
            u64 jit_count = 0, jit_bytes = 0, jit_ns = 0;
            if (eden_ounce_jit_stats) {
                eden_ounce_jit_stats(&jit_count, &jit_bytes, &jit_ns);
            }
            const std::string beat = std::format(
                "latido {} s | velocidad {:.0f}% | juego {:.1f} FPS | JIT: {} publicaciones, "
                "{} KiB, {:.1f} s en IPC",
                loops / 250, perf.emulation_speed * 100.0, perf.average_game_fps, jit_count,
                jit_bytes >> 10, static_cast<double>(jit_ns) / 1e9);
            Ounce::Hos::Breadcrumb(beat.c_str());
        }
    }
    Ounce::Hos::Breadcrumb("saliendo del bucle principal");

    if (system.IsPoweredOn()) {
        system.Pause();
        system.ShutdownMainProcess();
    }
    window.reset();
    Ounce::Hos::ExitNetwork();
    LOG_INFO(Frontend, "eden-ounce cerrado");
    return 0;
}

} // namespace

#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"
