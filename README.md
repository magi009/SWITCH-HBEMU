# SWITCH-HBEMU: Eden para Switch 2 (KaiHEN + libounce)

Este port hace que **Eden** (el emulador de Switch 1 que salió de Yuzu) corra como
homebrew nativo en una **Switch 2** con KaiHEN. Usa:

- **libounce**, que es libnx adaptado a Switch 2,
- **OunceVK** para Vulkan,
- **libkaihen** para el JIT.

Todo se compila con **ounce-devkit 20261117** (libounce `39406be1f25a`, ouncevk
`114e9298fc0e`, kaiHEN `ddbf02006672`).

> **Estado: experimental, sin probar en hardware.** El código se revisó con un compilador
> contra los headers reales del devkit (los de tu `ounce-devkit-20261117.tar.xz`, con el
> SHA-256 verificado), Boost, oaknut y VMA. Pero todavía no se ha enlazado un `.nro`
> completo (ver "Fase 0"), ni se ha ejecutado en una consola.

## Contenido

| Ruta | Qué es |
|---|---|
| `patches/0001-eden-ounce-switch2.patch` | Todo el port, como un parche sobre Eden (34 archivos) |
| `patches/BASE_COMMIT` | El commit de Eden contra el que está hecho el parche |
| `build.sh` | Aplica el parche, configura con `aarch64-none-elf-cmake` y genera `eden.nro` |
| `src/eden_ounce/` | Una copia del frontend nuevo, para leerlo sin aplicar el parche |

## Decisiones de arquitectura (y por qué)

**1. CPU: Dynarmic, no NCE.** El JIT de kaiHEN no permite mapear memoria RWX ni elegir
direcciones ejecutables. `jitCreate` devuelve:

- una vista **RX** cuya dirección elige RO,
- un búfer de **staging RW** aparte, que no es un alias de la RX.

Todo lo escrito en staging se vuelve ejecutable solo al publicarlo por IPC. Con NCE,
el código del juego se ejecuta en sus propias direcciones, con `.data` escribible al lado
de `.text`, y eso aquí no es posible. Por eso se usa el backend arm64 de Dynarmic (A64 →
A64), con una caché de código nueva:

- `HorizonCodeBlock` (`dynarmic/backend/arm64/horizon_code_block.{h,cpp}`) sustituye a
  `oaknut::CodeBlock`.
- oaknut escribe en staging (`wptr`) y calcula los saltos con la dirección RX (`xptr`).
- `invalidate()` solo anota rangos. `protect()` los publica todos juntos con
  `kaihenJitPublishRanges`, en una sola petición.
- Cada núcleo emulado tiene su propio JIT, y solo su hilo emite y parchea código. Así se
  cumple la regla de kaiHEN de no modificar un bloque que otro hilo esté ejecutando.
- Caché de 64 MiB por núcleo: 4 núcleos × (64 MiB bloqueados + 64 MiB de staging) =
  512 MiB.
- El spinlock de Dynarmic, que también se generaba con JIT, ahora es C++ atómico. Un
  objeto estático no puede pedirle memoria a kaiHEN antes de que libounce arranque.

**2. Memoria: sin fastmem.** Horizon no tiene `mmap` ni `memfd`. La DRAM emulada
(4 GiB) es un bloque `aligned_alloc` del heap, y Dynarmic accede por tabla de páginas.
Esto reutiliza la ruta que Eden ya tenía para PS4 (`__OPENORBIS__`). Es más lento que
fastmem, pero funciona.

**3. GPU: Vulkan sobre OunceVK.** Hay un `WindowSystemType::NintendoSwitch` nuevo, y la
superficie se crea con `VK_NN_vi_surface` sobre `nwindowGetDefault()`, igual que en el
ejemplo `vulkan-clear` del devkit. Como no hay `dlopen`, OunceVK se enlaza estático y
`DynamicLibrary` expone su `vkGetInstanceProcAddr`.

**4. libnx y Eden nunca comparten un `.cpp`.** Esto salió en la revisión del
compilador. libnx define:

- `struct Service`, que choca con el `namespace Service` de Eden,
- `u128`, con un tipo distinto al de Eden,
- la macro `BIT()`, que rompe la instrucción `BIT` de oaknut.

Por eso solo `horizon.cpp`, `launcher.cpp`, `horizon_code_block.cpp` y
`memory_detect.cpp` incluyen `<switch.h>`. El resto usa `horizon.h`, que solo usa tipos
estándar. Esta es la capa de abstracción que elegiste.

**5. Entrada.** Los mandos de libounce alimentan el `VirtualGamepad` que Eden ya trae,
así que no hubo que tocar `input_common`. El mando 1 maneja también el mando "portátil"
del juego. Mantener **+ y −** durante 1 s cierra el emulador. Al poner o quitar la
consola del dock, el juego emulado cambia de modo.

## Uso en la consola

```
sdmc:/switch/eden/eden.nro          <- el emulador
sdmc:/switch/eden/keys/prod.keys    <- claves dumpeadas de TU consola (obligatorio)
sdmc:/switch/eden/keys/title.keys   <- opcional
sdmc:/switch/eden/nand/...          <- firmware dumpeado (recomendado)
sdmc:/switch/eden/games/*.nsp|xci   <- tus dumps
```

Abre Eden desde el Homebrew Launcher, elige un juego con el D-pad y pulsa A. El log
queda en `sdmc:/switch/eden/log/`. Eden no incluye claves, firmware ni juegos: todo tiene
que salir de tu propia consola.

## Compilar

```sh
git clone https://github.com/magi009/SWITCH-HBEMU && cd SWITCH-HBEMU
git clone https://github.com/eden-emulator/mirror eden
git -C eden checkout $(cat patches/BASE_COMMIT)
export DEVKITPRO=/opt/devkitpro
./build.sh ./eden
```

## Lo que falta, por orden

**Fase 0: que enlace (casi lista).** El CI (`.github/workflows/build-switch2.yml`)
compila con devkitA64 GCC 15.2 + ounce-devkit 20261117:

- Hecho: OpenSSL 3.6.2 para Switch 2 (`ci/build-openssl.sh`); FFmpeg, zstd, lz4 y
  Opus de los portlibs o de CPM; Boost sin Process ni Filesystem.
- Hecho: los ~2.400 archivos de Eden compilan. Se quitaron o vaciaron el
  depurador GDB, HTTP (hydrocarbon), el cliente UDP de mandos, renderdoc y
  `signal_chain`.
- Pendiente: confirmar que el enlace final de `eden.nro` pasa. El último run se cayó
  solo por símbolos (dav1d, zlib, `pipe`), y ya están arreglados.

Puede que aparezcan más funciones POSIX que newlib no tiene al probar en la consola.

**Fase 1: arrancar homebrew.** Probar primero con `.nro` caseros (más fácil de depurar
que un juego comercial). Medir el coste por IPC de `kaihenJitPublishRanges`. Si es alto,
juntar varias emisiones antes de publicar, o desactivar el block linking.

**Fase 2: audio.** Hacer un sink nativo con `audout` o `audren` en
`audio_core/sink/`. Por ahora se usa `AudioEngine::Null`.

**Fase 3: rendimiento y calidad.**

- Pantalla táctil, vibración y giroscopio (`SetMotionState`).
- Un archivo de configuración `.ini` en la SD.
- Una interfaz más bonita que la consola de texto.

## Riesgos conocidos

- La memoria del juego anfitrión limita todo. Hay que comprobar con
  `svcGetInfo(TotalMemorySize)` que caben 4 GiB de DRAM + 512 MiB de JIT + Eden + la caché
  de shaders.
- kaiHEN avisa que tiene acceso completo de escritura a la NAND. Un fallo de un homebrew
  puede dañar la consola. Haz un backup de la NAND antes de probar.
- Licencia: Eden es GPL-3.0. El port, y cualquier `.nro` que distribuyas, tiene que ser
  GPL-3.0, con el código fuente disponible.
