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
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/cpu_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
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
    // 4 GiB como la Switch 1; el pool de un juego de Switch 2 no da para más con Eden encima.
    values.memory_layout_mode.SetValue(MemoryLayout::Memory_4Gb);

    values.renderer_backend.SetValue(RendererBackend::Vulkan);
    values.vsync_mode.SetValue(VSyncMode::Fifo);
    values.resolution_setup.SetValue(ResolutionSetup::Res1X);
    values.use_disk_shader_cache.SetValue(true);
    values.use_asynchronous_gpu_emulation.SetValue(true);
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

int main(int argc, char** argv) {
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

    ConfigureForSwitch2();

    Core::System system{};
    InputCommon::InputSubsystem input_subsystem{};

    system.Initialize();
    system.ApplySettings();

    auto window = std::make_unique<EdenOunce::EmuWindowOunce>(input_subsystem, system);

    system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    system.GetFileSystemController().CreateFactories(*system.GetFilesystem());
    system.GetUserChannel().clear();

    Service::AM::FrontendAppletParameters load_parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    const Core::SystemResultStatus load_result = system.Load(*window, filepath, load_parameters);
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

    system.GPU().Start();
    system.GetCpuManager().OnGpuReady();

    if (Settings::values.use_disk_shader_cache.GetValue()) {
        system.Renderer().ReadRasterizer()->LoadDiskResources(
            system.GetApplicationProcessProgramID(), std::stop_token{},
            [](VideoCore::LoadCallbackStage, size_t, size_t) {});
    }

    system.RegisterExitCallback([&window] { window->RequestClose(); });

    void(system.Run());

    // El emulador corre en sus propios hilos (núcleos de CPU, GPU, servicios);
    // este hilo solo bombea entrada y eventos del applet.
    while (window->PumpEvents()) {
        Ounce::Hos::SleepNs(4'000'000); // 4 ms: suficiente para entrada a 240 Hz
    }

    if (system.IsPoweredOn()) {
        system.Pause();
        system.ShutdownMainProcess();
    }
    window.reset();
    Ounce::Hos::ExitNetwork();
    LOG_INFO(Frontend, "eden-ounce cerrado");
    return 0;
}

#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"
