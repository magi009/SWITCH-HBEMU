#!/bin/sh
# Compila eden-ounce (Eden para Switch 2) con ounce-devkit.
#
# Uso:  ./build.sh /ruta/a/eden
#   - /ruta/a/eden: un clon de Eden en el commit de patches/BASE_COMMIT
#     (https://git.eden-emu.dev/eden-emu/eden o el mirror de GitHub).
# Requisitos: devkitPro con devkitA64 + ounce-devkit (libounce, ouncevk, libkaihen),
# es decir `sh install.sh` del repo ounce-devkit, o su imagen Docker; CMake >= 3.31,
# glslangValidator, y OpenSSL para Switch 2 (ci/build-openssl.sh).
# Variables opcionales: EXTRA_CMAKE_ARGS, BUILD_ARGS.
set -eu

EDEN_DIR=${1:?"Uso: $0 /ruta/a/eden"}
HERE=$(cd "$(dirname "$0")" && pwd)

: "${DEVKITPRO:?Define DEVKITPRO (p. ej. /opt/devkitpro)}"

cd "$EDEN_DIR"
if ! git apply --check -R "$HERE/patches/0001-eden-ounce-switch2.patch" 2>/dev/null; then
    echo ">> Aplicando el parche de Switch 2"
    git apply "$HERE/patches/0001-eden-ounce-switch2.patch"
fi

# shellcheck disable=SC1091
. "$DEVKITPRO/switch2vars.sh"

# ccache si está disponible (CI y recompilaciones locales).
LAUNCHER=""
if command -v ccache >/dev/null 2>&1; then
    LAUNCHER="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
fi

# Todo lo que no tiene sentido (o no existe) en la consola, apagado.
aarch64-none-elf-cmake -S . -B build-switch2 -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_QT=OFF \
    -DYUZU_CMD=OFF \
    -DYUZU_ROOM=OFF \
    -DYUZU_ROOM_STANDALONE=OFF \
    -DYUZU_TESTS=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_UPDATE_CHECKER=OFF \
    -DENABLE_CUBEB=OFF \
    -DENABLE_OPENGL=OFF \
    -DENABLE_LIBUSB=OFF \
    -DENABLE_LSFG=OFF \
    -DENABLE_RESHADE=OFF \
    -DENABLE_WERROR=OFF \
    -DYUZU_DISABLE_LLVM=ON \
    -DYUZU_USE_BUNDLED_SDL3=OFF \
    -DYUZU_USE_BUNDLED_FFMPEG=OFF \
    -DYUZU_USE_BUNDLED_OPENSSL=OFF \
    -DYUZU_USE_EXTERNAL_FFMPEG=OFF \
    -DDYNARMIC_TESTS=OFF \
    -Dopenssl_FORCE_SYSTEM=ON -DOpenSSL_FORCE_SYSTEM=ON \
    -DBOOST_CONTEXT_ARCHITECTURE=arm64 -DBOOST_CONTEXT_ABI=aapcs \
    -DBOOST_CONTEXT_BINARY_FORMAT=elf -DBOOST_CONTEXT_ASSEMBLER=gas \
    -DBOOST_CONTEXT_IMPLEMENTATION=fcontext \
    $LAUNCHER \
    ${EXTRA_CMAKE_ARGS:-}

cmake --build build-switch2 --target eden-ounce ${BUILD_ARGS:-}

echo
echo ">> Listo: $EDEN_DIR/build-switch2/eden.nro"
echo "   Copialo a sdmc:/switch/eden/eden.nro y lanzalo desde el Homebrew Launcher de KaiHEN."
