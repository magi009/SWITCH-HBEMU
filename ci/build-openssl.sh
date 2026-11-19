#!/bin/sh
# Compila OpenSSL estático para Switch 2 e instálalo en los portlibs de Switch 2.
# Eden usa OpenSSL para su criptografía (AES/SHA de las NCA, claves, etc.).
set -eu
VERSION=${OPENSSL_VERSION:-3.6.2}
: "${DEVKITPRO:?}"
. "$DEVKITPRO/switch2vars.sh"

work=$(mktemp -d)
git clone -q --depth 1 --branch "openssl-$VERSION" https://github.com/openssl/openssl "$work/openssl"
cd "$work/openssl"

# linux-aarch64 solo aporta las opciones del compilador; sin asm, sin DSO, sin sockets,
# sin apps: nada de eso existe o hace falta en Horizon. NO_SYSLOG: newlib no trae syslog.h.
./Configure linux-aarch64 \
    no-shared no-dso no-asm no-async no-sock no-engine no-afalgeng \
    no-ui-console no-secure-memory no-tests no-apps no-docs \
    --prefix="$PORTLIBS_PREFIX" --libdir=lib --openssldir=/switch/eden/ssl \
    CC=aarch64-none-elf-gcc AR=aarch64-none-elf-ar RANLIB=aarch64-none-elf-ranlib \
    CFLAGS="$CFLAGS $CPPFLAGS -DNO_SYSLOG" LDFLAGS="$LDFLAGS"

make -j"$(nproc)" build_libs
make install_dev
echo ">> OpenSSL $VERSION instalado en $PORTLIBS_PREFIX"
