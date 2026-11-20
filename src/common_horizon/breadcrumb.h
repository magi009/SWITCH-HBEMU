// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Migas de pan para depurar el arranque en Switch 2 (ver eden_ounce/horizon.cpp).
// En Horizon/KaiHEN los fallos de CPU no llegan al proceso, así que la única forma de
// saber dónde muere es dejar una línea escrita al instante antes de cada paso.
// Fuera de Switch 2 no hace nada.

#pragma once

#ifdef __SWITCH2__
extern "C" void eden_ounce_breadcrumb(const char* text) __attribute__((weak));
#define EDEN_CRUMB(text)                                                                           \
    do {                                                                                           \
        if (eden_ounce_breadcrumb) {                                                               \
            eden_ounce_breadcrumb(text);                                                           \
        }                                                                                          \
    } while (0)
#else
#define EDEN_CRUMB(text) ((void)0)
#endif
