#!/bin/sh
# build.sh <source checkout> <out dir>: builds OpenBIOS for the test script.
#
# OpenBIOS is PCSX-Redux's free replacement for the PlayStation's ROM, written
# from scratch and MIT-licensed. Its source is PCSX-Redux's MIPS tree, a
# repository of its own ("nugget"), at the commit nugget.ref names. The test
# script boots it so that a public CI runner, which may not hold Sony's ROM,
# runs the console all the same (chimera issue 260). It is built for the tests
# only and is never put in the package.
#
# The build needs a MIPS compiler: a bare-metal mipsel-none-elf one, or
# Ubuntu's gcc-mipsel-linux-gnu (the two name the object format differently).
# It works in a copy, so the checkout is left as it was.
set -eu
src="$1"
out="$2"
here="$(cd "$(dirname "$0")" && pwd)"
want="$(sed -n 's/^commit //p' "$here/nugget.ref")"
have="$(git -C "$src" rev-parse HEAD 2>/dev/null || echo "not a git checkout")"
if [ "$have" != "$want" ]; then
	echo "the source is at $have, not at $want (tests/openbios/nugget.ref)"
	exit 1
fi
if command -v mipsel-none-elf-gcc >/dev/null 2>&1; then
	prefix=mipsel-none-elf
	format=elf32-littlemips
elif command -v mipsel-linux-gnu-gcc >/dev/null 2>&1; then
	prefix=mipsel-linux-gnu
	format=elf32-tradlittlemips
else
	echo "no MIPS compiler: install gcc-mipsel-linux-gnu (or a mipsel-none-elf toolchain)"
	exit 1
fi
rm -rf "$out"
mkdir -p "$out"
cp -a "$src/." "$out/src"
make -C "$out/src/openbios" PREFIX="$prefix" FORMAT="$format" -j"$(nproc 2>/dev/null || echo 2)"
cp "$out/src/openbios/openbios.bin" "$out/openbios.bin"
# the ROM says what it is at 0x78, which is how DuckStation knows it
if [ "$(dd if="$out/openbios.bin" bs=1 skip=120 count=8 2>/dev/null)" != "OpenBIOS" ]; then
	echo "the build did not make an OpenBIOS ROM"
	exit 1
fi
echo "openbios.bin: $(wc -c < "$out/openbios.bin") bytes, sha1 $(sha1sum "$out/openbios.bin" | cut -c1-40), built with $prefix-gcc $($prefix-gcc -dumpversion)"
