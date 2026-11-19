# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Frontend nativo de Eden para Switch 2. Solo se configura con el toolchain de
# ounce-devkit (Switch2.cmake), que define NINTENDO_SWITCH2 y __SWITCH2__.

add_executable(eden-ounce
    # Lado Eden (sin <switch.h>)
    emu_window_ounce.cpp
    emu_window_ounce.h
    main.cpp
    pad_input.cpp
    pad_input.h
    # Lado Horizon (solo <switch.h>, sin headers de Eden; ver horizon.h)
    horizon.cpp
    horizon.h
    launcher.cpp
)

target_include_directories(eden-ounce PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(eden-ounce PRIVATE
    common core input_common frontend_common video_core audio_core
    Threads::Threads
)
target_link_libraries(eden-ounce PRIVATE OpenSSL::SSL OpenSSL::Crypto)

# OunceVK (Vulkan) va estático: dynamic_library.cpp expone su vkGetInstanceProcAddr.
# libkaihen aporta el proveedor del JIT (kaihen-jit.o) que usan jitCreate/
# kaihenJitPublishRanges; debe ir antes de libounce (-lnx) en la línea de enlace.
target_link_libraries(eden-ounce PRIVATE vulkan kaihen)

nx_generate_nacp(eden-ounce.nacp
    NAME    "Eden"
    AUTHOR  "Eden Emulator Project (port Ounce)"
    VERSION "0.0.1-ounce"
)

nx_create_nro(eden-ounce
    OUTPUT ${CMAKE_BINARY_DIR}/eden.nro
    NACP   eden-ounce.nacp   # icono por defecto de libounce
)
