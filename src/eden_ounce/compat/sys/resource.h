// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Shim de <sys/resource.h> para Horizon, solo para Boost.Context
// (stack_traits.cpp pregunta el límite de pila con getrlimit).
// newlib trae un sys/resource.h sin rlimit; en Horizon la pila no tiene límite
// impuesto por el sistema: el tamaño lo fija quien crea el hilo o la fibra.

#pragma once

#include <sys/types.h>

typedef unsigned long rlim_t;

#define RLIM_INFINITY (~(rlim_t)0)
#define RLIMIT_STACK 3

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#ifdef __cplusplus
extern "C" {
#endif

static inline int getrlimit(int resource, struct rlimit* limit) {
    (void)resource;
    limit->rlim_cur = RLIM_INFINITY;
    limit->rlim_max = RLIM_INFINITY;
    return 0;
}

#ifdef __cplusplus
}
#endif
