#!/bin/sh
# Joins one emulator's libraries into ONE object in which every name is local
# but the one given: its table of the guest ABI (waterbox/machine.h).
#
# The console and the arcade boards are two emulators that began as the same
# commit, so they define the same names - thousands of them, classes with the
# same name and a different layout among them. Linked together as they are,
# the linker would keep one definition of each and both emulators would run
# on a mixture. So each side is linked by itself first (a relocatable link:
# every reference one of its files makes to another is settled here, inside
# it), and then every symbol it defines is made local. What is left undefined
# - the C library, the C++ runtime, OpenGL, the sandbox's own calls - is
# shared, and is resolved when the whole binary is linked.
#
# --force-group-allocation matters: inline functions and templates travel in
# COMDAT groups, which a relocatable link normally keeps as groups, to be
# merged by name later - merged, that is, with the other emulator's.
#
# Usage: link-machine.sh <out.o> <the one global name> <cc> [cc args...] -- <whole archives...> -- <archives searched...>
set -eu
out="$1"; keep="$2"; shift 2
cc=""
while [ "$1" != "--" ]; do cc="$cc $1"; shift; done
shift
whole=""
while [ "$1" != "--" ]; do whole="$whole $1"; shift; done
shift
# shellcheck disable=SC2086
$cc -r -nostdlib -Wl,--force-group-allocation -o "$out.all" \
	-Wl,--whole-archive $whole -Wl,--no-whole-archive "$@"
objcopy --keep-global-symbol="$keep" "$out.all" "$out"
rm -f "$out.all"
# the one name must be there, and must be the only one this object offers
defined="$(nm --defined-only --extern-only "$out" | awk '{print $3}')"
[ "$defined" = "$keep" ] || { echo "link-machine.sh: $out offers [$defined], wanted exactly [$keep]" >&2; exit 1; }
