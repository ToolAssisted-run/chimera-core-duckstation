#!/bin/bash
# The core-level equivalence gate: the sandboxed core must produce
# byte-identical video, audio, lag and memory-domain digests to the native
# reference build (the same sources compiled natively), and must survive a
# whole-machine savestate round-trip around every frame.
#
# IN TIERS, because a PlayStation runs nothing without its ROM and the ROM is
# Sony's:
#
#   with nothing    the declarations, the per-title database, and both flavors
#                   refusing a machine with no BIOS and saying why
#   with OpenBIOS   $CHIMERA_NUGGET_SRC and a MIPS compiler: the free BIOS,
#                   built from source, boots its shell - equivalence, the
#                   savestate round-trip, a picture (this is what CI runs)
#   with a BIOS     tests/roms/bios/, or $CHIMERA_PS1_BIOS: equivalence, the
#                   savestate round-trip, determinism, the clock, input, lag
#   with a disc     tests/roms/, or $CHIMERA_PS1_DISC (a .cue): a game's own
#                   program, identical in the sandbox, by name and without one
#   with Chimera    $CHIMERA_ROOT, with chimera-run built and this package
#                   installed: the package through the engine
#   with a 3D game  tests/roms/, or $CHIMERA_PS1_GAME3D: Need for Speed -
#                   Porsche Unleashed (USA), driven from boot through its
#                   menus into a race by a script of presses
#
# A check whose content is missing reports SKIP and says what it would have
# proven. A SKIP is not a pass.
#
#   with a GPU     build/meson-gl (meson setup -Dgl_bridge=true) and a GL
#                   context on this machine: the hardware renderer through the
#                   GPU bridge
#
# Usage: ./run-gate.sh [-n <native build dir>] [-g <guest build dir>] [-b <bridge build dir>] [-a]
#   -a   the arcade boards' legs only (and the ones that need no content)
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
nat="$root/build/meson-native"
gst="$root/build/meson-guest"
brg="$root/build/meson-gl"
only_arcade=0
while getopts "n:g:b:a" opt; do
	case "$opt" in
		n) nat="$OPTARG" ;;
		g) gst="$OPTARG" ;;
		b) brg="$OPTARG" ;;
		a) only_arcade=1 ;;
		*) exit 2 ;;
	esac
done

[ -x "$nat/run-native" ] && [ -x "$nat/run-wbx" ] || {
	echo "native build missing: meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native" >&2; exit 1; }
[ -f "$gst/core.wbx" ] || {
	echo "guest build missing: sh waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest core.wbx" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# machineHash is the machine's clock and program counter folded in EVERY frame;
# the memory domains are hashed once, at the end, and a difference that healed
# before then would otherwise go unseen.
digests() { grep -E '^(frames|vsync|videoSize|videoHash|audioHash|lagFrames|machineHash|domain\[)'; }
# what a run can be held to when only the RENDERER differs: where the processor
# was every frame, what the frame sounded like and which frames polled. Not the
# picture, and not memory - a console that reads its own picture back puts the
# renderer's pixels in its RAM, and the BIOS shell does.
machine_only() { grep -E '^(frames|vsync|audioHash|lagFrames|machineHash)'; }
plain() { sed 's/\x1b\[[0-9;]*m//g'; }  # upstream colours its log

ok=0
failed=0
skipped=0
report() {
	printf "%-22s %-6s %s\n" "$1" "$2" "$3"
	case "$2" in
		PASS) ok=$((ok + 1)) ;;
		SKIP) skipped=$((skipped + 1)) ;;
		*) failed=$((failed + 1)) ;;
	esac
}
printf "%-22s %-6s %s\n" "Check" "Result" "Detail"
printf "%-22s %-6s %s\n" "-----" "------" "------"

# A stale guest is the oldest way a gate lies: the sources moved on, the build
# failed or was never run, and the comparison is of yesterday's core.
newest_src="$(find "$here" "$root/patches" "$root/patches-arcade" "$root/meson.build" -type f \
	\( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.inc' -o -name '*.S' -o -name '*.patch' -o -name 'meson.build' \) \
	-newer "$gst/core.wbx" 2>/dev/null | grep -v -e '/run-wbx.c$' -e '/run-native.c$' -e '/gate-harness.h$' | head -1)"
if [ -n "$newest_src" ]; then
	report "build:fresh" FAIL "core.wbx is older than ${newest_src#"$root"/}: rebuild the guest"
else
	report "build:fresh" PASS "core.wbx is newer than every source it is built from"
fi

# the sandbox runner, under a cap: a guest that runs away must not take the
# machine with it
wbx() {
	if command -v systemd-run >/dev/null 2>&1 && systemd-run --user --scope --quiet true 2>/dev/null; then
		systemd-run --user --scope --quiet -p MemoryMax=8G -p MemorySwapMax=0 "$nat/run-wbx" "$gst/core.wbx" "$@"
	else
		"$nat/run-wbx" "$gst/core.wbx" "$@"
	fi
}
native() { "$nat/run-native" "$@"; }
# the sandbox runner that carries the GPU bridge's host half, asked for a GPU.
# $GPU_ENV is extra environment for one call (the untold control).
wbxgpu() {
	if command -v systemd-run >/dev/null 2>&1 && systemd-run --user --scope --quiet true 2>/dev/null; then
		systemd-run --user --scope --quiet -p MemoryMax=8G -p MemorySwapMax=0 -E CHIMERA_GPU=1 ${GPU_ENV:+-E "$GPU_ENV"} \
			"$brg/run-wbx" "$gst/core.wbx" "$@"
	else
		env CHIMERA_GPU=1 ${GPU_ENV:+"$GPU_ENV"} "$brg/run-wbx" "$gst/core.wbx" "$@"
	fi
}

# a work folder: what the frontend would mount. <dir> [bios] [settings json]
mkwork() {
	rm -rf "$1"; mkdir -p "$1"
	[ -n "${2:-}" ] && cp "$2" "$1/bios"
	[ -n "${3:-}" ] && printf '%s' "$3" > "$1/settings"
	return 0
}

# ---- tier one: no content needed -------------------------------------------

# every binding names a declared control, and no two controls of one pad write
# the same character into a movie
kb="$(python3 - "$here/waterbox.config" "$here/default_keybinds.json" <<'PY' 2>&1
import json, sys
cfg = json.load(open(sys.argv[1])); kb = json.load(open(sys.argv[2]))
# one controller per machine; two machines may share one (the arcade boards do)
inputs = {}
for m in cfg["machines"]: inputs.setdefault(m["input"]["name"], m["input"])
bad = []; unbound = []; said = []
for name, inp in inputs.items():
    buttons = set(inp["buttons"]); axes = set(a["name"] for a in inp["axes"])
    bad += [name + ": " + k for k in kb["AllTrollers"].get(name, {}) if k not in buttons]
    bad += [name + ": " + k for k in kb["AllTrollersAnalog"].get(name, {}) if k not in axes]
    unbound += [b for b in inp["buttons"] if b.startswith("P1 ") and b not in kb["AllTrollers"].get(name, {})]
    mn = inp.get("mnemonics", {})
    per = {}
    for b in inp["buttons"]:
        port, _, key = b.partition(" ")
        if not (len(port) == 2 and port[0] == "P" and port[1].isdigit()): port, key = "", b
        ch = mn.get(b, mn.get(key))
        if ch is None: bad.append("no mnemonic: " + b); continue
        if ch in per.setdefault(port, {}): bad.append("%s and %s both write '%s'" % (per[port][ch], b, ch))
        per[port][ch] = b
    said.append("%s: %d buttons, %d axes, %d bindings" % (name, len(buttons), len(axes), len(kb["AllTrollers"].get(name, {})) + len(kb["AllTrollersAnalog"].get(name, {}))))
for name in list(kb["AllTrollers"]) + list(kb["AllTrollersAnalog"]):
    if name not in inputs: bad.append("bindings for a controller no machine has: " + name)
if bad or unbound: print("; ".join(bad + ["unbound: " + u for u in unbound])); sys.exit(1)
print("; ".join(said))
PY
)"
if [ $? -eq 0 ]; then report "keybinds" PASS "$kb"; else report "keybinds" FAIL "$kb"; fi

# no BIOS: both flavors must refuse, and say what is missing
mkwork "$work/nobios"
n_msg="$(native "$work/nobios" --frames 1 2>&1 | plain | grep -i 'no bios' | head -1)"
w_msg="$(wbx "$work/nobios" --frames 1 2>&1 | plain | grep -i 'no bios' | head -1)"
if [ -n "$n_msg" ] && [ -n "$w_msg" ]; then
	report "nobios:refuses" PASS "both flavors: ${n_msg#*: }"
else
	report "nobios:refuses" FAIL "native said [${n_msg:-nothing}], sandbox said [${w_msg:-nothing}]"
fi

# The per-title database is compiled in, and a core without it runs every game
# with none of its own corrections and nothing looks wrong. Asked for a title
# every copy of the database has; a serial that cannot exist is the control.
mkwork "$work/gamedb" "" '{"gamedb_probe":"SCUS-94900"}'
n_db="$(native "$work/gamedb" --frames 1 2>&1 | plain | sed -n 's/^duckstation: gamedb probe: //p' | head -1)"
w_db="$(wbx "$work/gamedb" --frames 1 2>&1 | plain | sed -n 's/^duckstation: gamedb probe: //p' | head -1)"
mkwork "$work/gamedb0" "" '{"gamedb_probe":"XXXX-00000"}'
n_no="$(native "$work/gamedb0" --frames 1 2>&1 | plain | sed -n 's/^duckstation: gamedb probe: //p' | head -1)"
if [ "$n_db" = "SCUS-94900 = Crash Bandicoot (USA)" ] && [ "$w_db" = "$n_db" ] && [ "$n_no" = "XXXX-00000 = (not on record)" ]; then
	report "gamedb:entry" PASS "$n_db, in both flavors; a serial that does not exist is not on record"
else
	report "gamedb:entry" FAIL "native [$n_db], sandbox [$w_db], control [$n_no]"
fi

# ---- the arcade boards --------------------------------------------------------
#
# The other emulator in the binary (meson.build, "one binary, two machines"):
# the boards built out of a PlayStation, which run MAME rom sets. A function,
# because it needs no PlayStation BIOS and is run whether or not there is one.
#
#   with nothing      the record of the boards is compiled in, and a board with
#                     no rom set refuses and says why
#   Namco System 11   tests/roms/arcade/sets/tekkenac.zip (Tekken, Asia,
#                     TE4/VER.C) or $CHIMERA_ARCADE_NS11_SET, and the board's
#                     sound processor's rom, tests/roms/arcade/namcoc76.zip or
#                     $CHIMERA_ARCADE_NS11_BIOS
#   Konami GQ         tests/roms/arcade/sets/cryptklr.zip (Crypt Killer) or
#                     $CHIMERA_ARCADE_GQ_SET, and its hard disk,
#                     tests/roms/arcade/420uaa04.chd or $CHIMERA_ARCADE_GQ_DISK
#
# The press scripts are written for those two sets and no others.
arcade_tier() {
	local adir="$root/tests/roms/arcade"

	# the record of the boards: a set every copy has, and one that cannot exist
	mkwork "$work/adb" "" '{"machine":"namco_system11","arcadedb_probe":"tekken"}'
	local n_db w_db n_no
	n_db="$(native "$work/adb" --frames 1 2>&1 | plain | sed -n 's/^duckstation: arcadedb probe: //p' | head -1)"
	w_db="$(wbx "$work/adb" --frames 1 2>&1 | plain | sed -n 's/^duckstation: arcadedb probe: //p' | head -1)"
	mkwork "$work/adb0" "" '{"machine":"namco_system11","arcadedb_probe":"nosuchset"}'
	n_no="$(native "$work/adb0" --frames 1 2>&1 | plain | sed -n 's/^duckstation: arcadedb probe: //p' | head -1)"
	if [ "$n_db" = "tekken = Tekken [namco_system11]" ] && [ "$w_db" = "$n_db" ] && [ "$n_no" = "nosuchset = (not on record)" ]; then
		report "arcadedb:entry" PASS "$n_db, in both flavors; a set that does not exist is not on record"
	else
		report "arcadedb:entry" FAIL "native [$n_db], sandbox [$w_db], control [$n_no]"
	fi

	# a board with nothing to run: both flavors must refuse, and say what is missing
	mkwork "$work/noset" "" '{"machine":"namco_system11"}'
	local n_msg w_msg
	n_msg="$(native "$work/noset" --frames 1 2>&1 | plain | grep -i 'no rom set' | head -1)"
	w_msg="$(wbx "$work/noset" --frames 1 2>&1 | plain | grep -i 'no rom set' | head -1)"
	if [ -n "$n_msg" ] && [ -n "$w_msg" ]; then
		report "arcade:noset" PASS "both flavors: $(cut -c1-90 <<< "${n_msg#*: }")"
	else
		report "arcade:noset" FAIL "native said [${n_msg:-nothing}], sandbox said [${w_msg:-nothing}]"
	fi

	# ---- Namco System 11: Tekken ---------------------------------------------
	local ns11="${CHIMERA_ARCADE_NS11_SET:-}" ns11bios="${CHIMERA_ARCADE_NS11_BIOS:-}"
	[ -n "$ns11" ] || { [ -f "$adir/sets/tekkenac.zip" ] && ns11="$adir/sets/tekkenac.zip"; }
	[ -n "$ns11bios" ] || { [ -f "$adir/namcoc76.zip" ] && ns11bios="$adir/namcoc76.zip"; }
	if [ -z "$ns11" ] || [ -z "$ns11bios" ]; then
		for leg in ns11:compare ns11:rerecord ns11:rewind ns11:memory ns11:refused ns11:crop panel:wheel ns11:engine ns11:gl ns11:gpu ns11:vulkan; do
			report "$leg" SKIP "needs tests/roms/arcade/sets/tekkenac.zip (CHIMERA_ARCADE_NS11_SET) and tests/roms/arcade/namcoc76.zip (CHIMERA_ARCADE_NS11_BIOS)"
		done
	else
		echo "# system 11: $(basename "$ns11") sha1 $(sha1sum "$ns11" | cut -c1-40)"
		# <dir> <settings json>: what the frontend would mount for this board
		mkns11() {
			mkwork "$1" "" "$2"
			cp "$ns11" "$1/tekkenac.zip"; cp "$ns11bios" "$1/namcoc76.zip"
			printf '{"romset":["tekkenac.zip"]}' > "$1/slots"
		}
		# Two coins (this set wants two a credit), Start, the first character on
		# offer, and then punches and kicks through the first fight. The wire's
		# own indices: 4 Start, 5 Coin, 6..9 the four buttons.
		local tk="--press 700:6:5 --press 760:6:5 --press 860:6:4 --press 1000:6:6 --press 1100:6:6"
		local f; for f in $(seq 1500 60 2340); do tk="$tk --press $f:5:6 --press $((f + 20)):5:8"; done

		mkns11 "$work/tk" '{"machine":"namco_system11"}'
		# shellcheck disable=SC2086
		native "$work/tk" --frames 2400 $tk 2>/dev/null | digests > "$work/tk.n"
		# shellcheck disable=SC2086
		wbx "$work/tk" --frames 2400 $tk 2>/dev/null | digests > "$work/tk.w"
		native "$work/tk" --frames 2400 2>/dev/null | digests > "$work/tk.idle"
		if [ -s "$work/tk.n" ] && cmp -s "$work/tk.n" "$work/tk.w" && ! cmp -s "$work/tk.n" "$work/tk.idle"; then
			report "ns11:compare" PASS "2400 frames of Tekken from power-on through coins, Start and a fight: video, audio, lag, the machine every frame and memory identical, and not the attract mode's"
		else
			report "ns11:compare" FAIL "flavors $(cmp -s "$work/tk.n" "$work/tk.w" && echo agree || echo DIFFER: "$(diff "$work/tk.n" "$work/tk.w" | head -3 | tr '\n' ' ')"); played and idle $(cmp -s "$work/tk.n" "$work/tk.idle" && echo "are the SAME run" || echo differ)"
		fi

		wbx "$work/tk" --frames 500 2>/dev/null | digests > "$work/tk.s"
		wbx "$work/tk" --frames 500 --rerecord 2>/dev/null | digests > "$work/tk.r"
		if [ -s "$work/tk.s" ] && cmp -s "$work/tk.s" "$work/tk.r"; then
			report "ns11:rerecord" PASS "500 frames with a state saved and loaded around every one: the straight run"
		else
			report "ns11:rerecord" FAIL "$(diff "$work/tk.s" "$work/tk.r" | head -4 | tr '\n' ' ')"
		fi

		# kept before the coins, restored in the middle of the fight
		# shellcheck disable=SC2086
		wbx "$work/tk" --frames 2400 $tk --rewind 650:2200 2>/dev/null | digests > "$work/tk.rw"
		if [ -s "$work/tk.w" ] && cmp -s "$work/tk.w" "$work/tk.rw"; then
			report "ns11:rewind" PASS "kept at frame 650 in the attract mode, restored at 2200 in the fight and replayed: the straight run"
		else
			report "ns11:rewind" FAIL "$(diff "$work/tk.w" "$work/tk.rw" | head -4 | tr '\n' ' ')"
		fi

		# What the board keeps: its EEPROM leaves as at28c16.nv, a project brings
		# one in under that name, and the one becomes the other. A file under a
		# name the board does not keep, and one of the wrong size, are refused.
		mkdir -p "$work/tk.sn" "$work/tk.sw" "$work/tk.back"
		native "$work/tk" --frames 300 --savedata-out "$work/tk.sn" > /dev/null 2>&1
		wbx "$work/tk" --frames 300 --savedata-out "$work/tk.sw" > /dev/null 2>&1
		local nv="$work/tk.sn/at28c16.nv" memory_ok=yes memory_why=""
		if [ ! -f "$nv" ] || [ "$(stat -c %s "$nv" 2>/dev/null)" != 2048 ] || ! cmp -s "$nv" "$work/tk.sw/at28c16.nv"; then
			memory_ok=no; memory_why="export: $(ls -l "$work/tk.sn" "$work/tk.sw" 2>&1 | tr '\n' ' ' | cut -c1-160)"
		else
			mkns11 "$work/tkin" '{"machine":"namco_system11"}'
			# a memory somebody wrote in: the exported one with a mark in it
			{ head -c 1792 "$nv"; printf 'CHIMERA-WAS-HERE'; tail -c 240 "$nv"; } > "$work/tkin/at28c16.nv"
			printf '{"romset":["tekkenac.zip"],"boardmemory":["at28c16.nv"]}' > "$work/tkin/slots"
			wbx "$work/tkin" --frames 1 --savedata-out "$work/tk.back" > /dev/null 2>&1
			if ! cmp -s "$work/tkin/at28c16.nv" "$work/tk.back/at28c16.nv" || cmp -s "$nv" "$work/tk.back/at28c16.nv"; then
				memory_ok=no; memory_why="import: what went in $(cmp -s "$work/tkin/at28c16.nv" "$work/tk.back/at28c16.nv" && echo "came back" || echo "did NOT come back")"
			fi
			mkns11 "$work/tkbad1" '{"machine":"namco_system11"}'; cp "$nv" "$work/tkbad1/scores.nv"
			printf '{"romset":["tekkenac.zip"],"boardmemory":["scores.nv"]}' > "$work/tkbad1/slots"
			wbx "$work/tkbad1" --frames 1 2>&1 | plain | grep -q 'rename it to the one it is' || { memory_ok=no; memory_why="$memory_why a file under another name was not refused;"; }
			mkns11 "$work/tkbad2" '{"machine":"namco_system11"}'; head -c 1000 /dev/zero > "$work/tkbad2/at28c16.nv"
			printf '{"romset":["tekkenac.zip"],"boardmemory":["at28c16.nv"]}' > "$work/tkbad2/slots"
			wbx "$work/tkbad2" --frames 1 2>&1 | plain | grep -q "is not this board's" || { memory_ok=no; memory_why="$memory_why a file of the wrong size was not refused;"; }
		fi
		if [ "$memory_ok" = yes ]; then
			report "ns11:memory" PASS "the board's EEPROM leaves as at28c16.nv, 2048 bytes, the same from both flavors; one a project brings comes back out byte for byte; another name and another size are refused"
		else
			report "ns11:memory" FAIL "$memory_why"
		fi

		# the machine is the project's, not the file's
		mkns11 "$work/tkgq" '{"machine":"konami_gq"}'
		local wrong
		wrong="$(wbx "$work/tkgq" --frames 1 2>&1 | plain | grep -c "is a set for the board 'namco_system11'")"
		if [ "$wrong" -ge 1 ]; then
			report "ns11:refused" PASS "a System 11 set in a project made for another board is refused, and the refusal names both"
		else
			report "ns11:refused" FAIL "a Konami GQ project took a System 11 set"
		fi

		# The lines a cabinet's monitor never showed. ArcadeDuck's record says
		# Tekken's hid eleven at the top and one at the bottom; under the
		# default crop they are not handed over, under "none" they are, and
		# either way it is the picture's edge and nothing the board can see.
		mkns11 "$work/tkfull" '{"machine":"namco_system11","crop":"none"}'
		wbx "$work/tk" --frames 600 2>/dev/null > "$work/tk.crop"
		wbx "$work/tkfull" --frames 600 2>/dev/null > "$work/tk.full"
		local size_crop size_full
		size_crop="$(sed -n 's/^videoSize=//p' "$work/tk.crop")"; size_full="$(sed -n 's/^videoSize=//p' "$work/tk.full")"
		if [ "$size_crop" = "512x228" ] && [ "$size_full" = "512x240" ] && cmp -s <(machine_only < "$work/tk.crop") <(machine_only < "$work/tk.full"); then
			report "ns11:crop" PASS "the picture is $size_crop without the twelve lines the cabinet's monitor hid and $size_full with them (crop: none), and the board is the same board"
		else
			report "ns11:crop" FAIL "default [$size_crop], want 512x228; crop none [$size_full], want 512x240; machine $(cmp -s <(machine_only < "$work/tk.crop") <(machine_only < "$work/tk.full") && echo same || echo DIFFERS)"
		fi

		# A driving cabinet's wheel and pedals, with no driving game to run:
		# the core is asked (arcade_panel_probe) to make player 1's part of
		# the panel a driving cabinet's whatever the game has, and to say what
		# ArcadeDuck's own readers are then given. The wire: axes 2 Steering,
		# 3 Accelerator, 4 Brake, 5 Clutch. What comes back must be what was
		# said, in both flavors. It proves the wire as far as the emulator's
		# input code, and not that any game reads it.
		mkns11 "$work/tkwheel" '{"machine":"namco_system11","arcade_panel_probe":"driving"}'
		local wheel="--axis 100:50:2:49152:32768 --axis 120:40:3:65535:0 --axis 200:10:4:13107:0 --axis 230:10:5:65535:0"
		# shellcheck disable=SC2086
		native "$work/tkwheel" --frames 300 $wheel 2>&1 | plain | grep 'panel probe' > "$work/wheel.n"
		# shellcheck disable=SC2086
		wbx "$work/tkwheel" --frames 300 $wheel 2>&1 | plain | grep 'panel probe' > "$work/wheel.w"
		if [ -s "$work/wheel.n" ] && cmp -s "$work/wheel.n" "$work/wheel.w" \
			&& grep -q 'steering 49152 accelerator 65535 brake 0 clutch 0' "$work/wheel.n" \
			&& grep -q 'steering 32768 accelerator 0 brake 13107 clutch 0' "$work/wheel.n" \
			&& grep -q 'steering 32768 accelerator 0 brake 0 clutch 65535' "$work/wheel.n"; then
			report "panel:wheel" PASS "a wheel three quarters over, each pedal in turn: ArcadeDuck's input code holds exactly what the movie said, in both flavors (no driving game is here: this is the wire, not a game)"
		else
			report "panel:wheel" FAIL "flavors $(cmp -s "$work/wheel.n" "$work/wheel.w" && echo agree || echo DIFFER); read back: $(tr '\n' ';' < "$work/wheel.n" | sed 's/duckstation: panel probe: //g' | cut -c1-170)"
		fi

		# the package, through Chimera's engine, from a project with the presses in it
		local chimera_root="${CHIMERA_ROOT:-}" c
		[ -n "$chimera_root" ] || for c in "$root/../chimera" "$root/../../chimera"; do
			[ -x "$c/build/meson-linux/chimera-run" ] && { chimera_root="$(cd "$c" && pwd)"; break; }
		done
		local crun="$chimera_root/build/meson-linux/chimera-run" cpkg="$chimera_root/build/Cores/duckstation.chimeraCore"
		if [ -z "$chimera_root" ] || [ ! -x "$crun" ] || [ ! -f "$cpkg" ]; then
			report "ns11:engine" SKIP "needs chimera-run and an installed duckstation.chimeraCore (set CHIMERA_ROOT; run waterbox/build-package.sh -r <chimera>)"
		elif [ "$cpkg" -ot "$gst/core.wbx" ]; then
			report "ns11:engine" FAIL "the installed package is older than core.wbx: run waterbox/build-package.sh"
		else
			local ep="$work/tk.engine"; mkdir -p "$ep/files" "$ep/save.e" "$ep/save.n"
			cp "$ns11" "$ep/files/tekkenac.zip"
			local db="$root/extern/arcadeduck/data/resources/arcadedb.yaml"
			local mp=("--press=700:6:P1 Coin" "--press=760:6:P1 Coin" "--press=860:6:P1 Start" "--press=1000:6:P1 Button 1" "--press=1100:6:P1 Button 1")
			for f in $(seq 1500 60 2340); do mp+=("--press=$f:5:P1 Button 1" "--press=$((f + 20)):5:P1 Button 3"); done
			python3 "$here/tests/make-project.py" "$cpkg" "$ep/shape.chimeraProject" 0 "--arcadedb=$db" \
				--set=machine=namco_system11 "--firmware=namcoc76.zip=$ns11bios" "romset=$ep/files/tekkenac.zip" > "$ep/log" 2>&1
			"$crun" --project "$ep/shape.chimeraProject" "$cpkg" --files "$ep/files" --firmware "namcoc76.zip=$ns11bios" \
				--frames 1 --record "$ep/shape.txt" >> "$ep/log" 2>&1
			python3 "$here/tests/make-project.py" "$cpkg" "$ep/tk.chimeraProject" 2400 "--idle-row=$(grep -m1 '^|' "$ep/shape.txt" 2>/dev/null)" \
				"--arcadedb=$db" --set=machine=namco_system11 "--firmware=namcoc76.zip=$ns11bios" "${mp[@]}" "romset=$ep/files/tekkenac.zip" >> "$ep/log" 2>&1
			"$crun" --project "$ep/tk.chimeraProject" "$cpkg" --files "$ep/files" --firmware "namcoc76.zip=$ns11bios" \
				--dump "Main RAM=$ep/engine.ram" --export-savedata "$ep/save.e" >> "$ep/log" 2>&1
			# shellcheck disable=SC2086
			native "$work/tk" --frames 2400 $tk --dump-domain "Main RAM" "$ep/native.ram" --savedata-out "$ep/save.n" > /dev/null 2>&1
			if [ -s "$ep/engine.ram" ] && cmp -s "$ep/engine.ram" "$ep/native.ram" && cmp -s "$ep/save.e/at28c16.nv" "$ep/save.n/at28c16.nv"; then
				report "ns11:engine" PASS "the installed package, from a project with the fight's presses in its movie, through chimera-run: Main RAM after 2400 frames is the native reference's, and so is the EEPROM it exports"
			else
				report "ns11:engine" FAIL "RAM $(cmp -s "$ep/engine.ram" "$ep/native.ram" && echo agrees || echo DIFFERS), EEPROM $(cmp -s "$ep/save.e/at28c16.nv" "$ep/save.n/at28c16.nv" && echo agrees || echo DIFFERS): $(tail -2 "$ep/log" | tr '\n' ' ' | cut -c1-160)"
			fi
		fi

		# The hardware renderer on the OpenGL inside the sandbox: the board is
		# the same board whichever renderer draws it, the pictures are two
		# rasterisers', and a rewind under it is exact, picture included.
		mkns11 "$work/tkgl" '{"machine":"namco_system11","renderer":"opengl","verbose":true}'
		local tkgl_log; tkgl_log="$(wbx "$work/tkgl" --frames 60 2>&1 | plain)"
		if grep -q 'has no OpenGL in it' <<< "$tkgl_log"; then
			report "ns11:gl" SKIP "this core.wbx was built without a guest Mesa"
		else
			mkns11 "$work/tkgl" '{"machine":"namco_system11","renderer":"opengl"}'
			wbx "$work/tk" --frames 900 2>/dev/null > "$work/tk.sw900"
			wbx "$work/tkgl" --frames 900 2>/dev/null > "$work/tk.gl900"
			wbx "$work/tkgl" --frames 900 --rewind 700:860 2>/dev/null | digests > "$work/tk.glrw"
			if grep -q '^machineHash=' "$work/tk.gl900" && cmp -s <(machine_only < "$work/tk.sw900") <(machine_only < "$work/tk.gl900") \
				&& ! cmp -s <(grep '^videoHash' "$work/tk.sw900") <(grep '^videoHash' "$work/tk.gl900") \
				&& cmp -s <(digests < "$work/tk.gl900") "$work/tk.glrw"; then
				report "ns11:gl" PASS "900 frames on the hardware renderer inside the sandbox: the processor, the sound and the polls are the software renderer's, the picture is its own, and a rewind from 700 to 860 replays it exactly"
			else
				report "ns11:gl" FAIL "machine $(cmp -s <(machine_only < "$work/tk.sw900") <(machine_only < "$work/tk.gl900") && echo same || echo DIFFERS); pictures $(cmp -s <(grep '^videoHash' "$work/tk.sw900") <(grep '^videoHash' "$work/tk.gl900") && echo "the SAME" || echo differ); rewind $(cmp -s <(digests < "$work/tk.gl900") "$work/tk.glrw" && echo exact || echo DIFFERS)"
			fi
		fi

		# On a host's OpenGL, through the bridge: the board's VRAM is a texture
		# on the card and in no state. Told a state is coming, the core fetches
		# it and the picture after a load is the straight run's; untold, it is
		# not - or the leg cannot tell a core that carries VRAM from one that
		# does not. Frames 1400..1460 are the attract mode's fight, at 2x.
		if [ ! -x "$brg/run-wbx" ]; then
			report "ns11:gpu" SKIP "needs a runner with the bridge's host half: meson setup build/meson-gl -Dminibox_dir=<miniBox> -Dgl_bridge=true"
			report "ns11:vulkan" SKIP "needs a runner with the bridge's host half, as ns11:gpu does"
		else
			mkns11 "$work/tkhw" '{"machine":"namco_system11","renderer":"opengl-hw","internalResolution":"2x","verbose":true}'
			local hw_log hw_gl
			hw_log="$(wbxgpu "$work/tkhw" --frames 60 2>&1 | plain)"
			hw_gl="$(sed -n 's/.*GL_RENDERER: //p' <<< "$hw_log" | head -1)"
			if [ -z "$(sed -n 's/^gpu bridge: \([0-9].*\)/\1/p' <<< "$hw_log" | head -1)" ]; then
				report "ns11:gpu" SKIP "this machine gave the bridge no GL context"
			else
				mkns11 "$work/tkhw" '{"machine":"namco_system11","renderer":"opengl-hw","internalResolution":"2x"}'
				wbxgpu "$work/tkhw" --frames 1480 2>/dev/null | digests > "$work/tkhw.s"
				wbxgpu "$work/tkhw" --frames 1480 --rewind 1400:1460 2>/dev/null | digests > "$work/tkhw.t"
				GPU_ENV=CHIMERA_NO_STATE_SAVING=1 wbxgpu "$work/tkhw" --frames 1480 --rewind 1400:1460 2>/dev/null | digests > "$work/tkhw.u"
				local told_ok=no untold_differs=no untold_machine=no
				cmp -s "$work/tkhw.s" "$work/tkhw.t" && [ -s "$work/tkhw.s" ] && told_ok=yes
				cmp -s "$work/tkhw.s" "$work/tkhw.u" || untold_differs=yes
				cmp -s <(machine_only < "$work/tkhw.s") <(machine_only < "$work/tkhw.u") && untold_machine=yes
				if [ "$told_ok" = yes ] && [ "$untold_differs" = yes ] && [ "$untold_machine" = yes ] && [ "$hw_gl" != "softpipe" ]; then
					report "ns11:gpu" PASS "drawn outside the sandbox ($hw_gl) at 2x, kept at 1400 and restored at 1460: every frame after the load is the straight run's; left untold, the picture is not and the machine still is"
				else
					report "ns11:gpu" FAIL "renderer [$hw_gl]; told and straight $([ "$told_ok" = yes ] && echo agree || echo DIFFER); untold picture $([ "$untold_differs" = yes ] && echo differs || echo "is the SAME - the control shows nothing"); untold machine $([ "$untold_machine" = yes ] && echo same || echo DIFFERS)"
				fi
			fi

			# The same board on the Vulkan device, the same rewind and the same
			# control. The arcade tree is its own copy of the renderer, so it
			# is its own proof.
			mkns11 "$work/tkvk" '{"machine":"namco_system11","renderer":"vulkan-hw","internalResolution":"2x"}'
			local vk_first
			vk_first="$(wbxgpu "$work/tkvk" --frames 60 2>&1 | plain)"
			if grep -q 'no Vulkan was handed over' <<< "$vk_first"; then
				report "ns11:vulkan" SKIP "this computer handed the core no Vulkan"
			else
				wbxgpu "$work/tkvk" --frames 1480 2>/dev/null | digests > "$work/tkvk.s"
				wbxgpu "$work/tkvk" --frames 1480 --rewind 1400:1460 2>"$work/tkvk.err" | digests > "$work/tkvk.t"
				GPU_ENV=CHIMERA_NO_STATE_SAVING=1 wbxgpu "$work/tkvk" --frames 1480 --rewind 1400:1460 2>/dev/null | digests > "$work/tkvk.u"
				local vk_told=no vk_untold_differs=no vk_untold_machine=no vk_machine=no
				cmp -s "$work/tkvk.s" "$work/tkvk.t" && [ -s "$work/tkvk.s" ] && vk_told=yes
				cmp -s "$work/tkvk.s" "$work/tkvk.u" || vk_untold_differs=yes
				cmp -s <(machine_only < "$work/tkvk.s") <(machine_only < "$work/tkvk.u") && vk_untold_machine=yes
				# and the machine is the one OpenGL ran, when that run is here
				[ ! -s "$work/tkhw.s" ] || cmp -s <(machine_only < "$work/tkhw.s") <(machine_only < "$work/tkvk.s") && vk_machine=yes
				if [ "$vk_told" = yes ] && [ "$vk_untold_differs" = yes ] && [ "$vk_untold_machine" = yes ] && [ "$vk_machine" = yes ] && ! grep -q 'calls were refused' "$work/tkvk.err"; then
					report "ns11:vulkan" PASS "the board drawn with Vulkan at 2x, kept at 1400 and restored at 1460: every frame after the load is the straight run's and the machine is the one OpenGL ran; left untold, the picture is not and the machine still is"
				else
					report "ns11:vulkan" FAIL "told and straight $([ "$vk_told" = yes ] && echo agree || echo DIFFER); untold picture $([ "$vk_untold_differs" = yes ] && echo differs || echo "is the SAME - the control shows nothing"); untold machine $([ "$vk_untold_machine" = yes ] && echo same || echo DIFFERS); machine against OpenGL's $([ "$vk_machine" = yes ] && echo same || echo DIFFERS) $(grep -m1 'calls were refused' "$work/tkvk.err")"
				fi
			fi
		fi
	fi

	# ---- Konami GQ: Crypt Killer ----------------------------------------------
	local gq="${CHIMERA_ARCADE_GQ_SET:-}" gqdisk="${CHIMERA_ARCADE_GQ_DISK:-}"
	[ -n "$gq" ] || { [ -f "$adir/sets/cryptklr.zip" ] && gq="$adir/sets/cryptklr.zip"; }
	[ -n "$gqdisk" ] || { [ -f "$adir/420uaa04.chd" ] && gqdisk="$adir/420uaa04.chd"; }
	if [ -z "$gq" ] || [ -z "$gqdisk" ]; then
		for leg in gq:compare gq:rewind gq:three; do
			report "$leg" SKIP "needs tests/roms/arcade/sets/cryptklr.zip (CHIMERA_ARCADE_GQ_SET) and its hard disk tests/roms/arcade/420uaa04.chd (CHIMERA_ARCADE_GQ_DISK)"
		done
	else
		echo "# konami gq: $(basename "$gq") sha1 $(sha1sum "$gq" | cut -c1-40)"
		mkwork "$work/ck" "" '{"machine":"konami_gq"}'
		cp "$gq" "$work/ck/cryptklr.zip"; ln -s "$gqdisk" "$work/ck/420uaa04.chd"
		printf '{"romset":["cryptklr.zip","420uaa04.chd"]}' > "$work/ck/slots"
		# The board tests itself for a minute. Then a coin, Start, and a gun
		# aimed a little left of centre firing through the first stage. The
		# wire: 4 Start, 5 Coin, 12 Trigger; axes 0 and 1 are the gun's aim.
		# The board's own setting is two coins a credit: one coin is "CREDIT 0 1/2"
		# and Start does nothing, which a digest cannot tell from a game.
		local ck="--hold-axis 0:28000 --hold-axis 1:30000 --press 4300:6:5 --press 4340:6:5 --press 4400:6:4"
		for f in $(seq 4600 40 5560); do ck="$ck --press $f:4:12"; done
		# shellcheck disable=SC2086
		native "$work/ck" --frames 5600 $ck 2>/dev/null | digests > "$work/ck.n"
		# shellcheck disable=SC2086
		wbx "$work/ck" --frames 5600 $ck 2>/dev/null | digests > "$work/ck.w"
		native "$work/ck" --frames 5600 --hold-axis 0:28000 --hold-axis 1:30000 2>/dev/null | digests > "$work/ck.idle"
		if [ -s "$work/ck.n" ] && cmp -s "$work/ck.n" "$work/ck.w" && ! cmp -s "$work/ck.n" "$work/ck.idle"; then
			report "gq:compare" PASS "5600 frames of Crypt Killer from power-on through its self-test, the two coins of a credit, Start and a gun firing: video, audio, lag, the machine every frame and memory identical, and not the attract mode's"
		else
			report "gq:compare" FAIL "flavors $(cmp -s "$work/ck.n" "$work/ck.w" && echo agree || echo DIFFER: "$(diff "$work/ck.n" "$work/ck.w" | head -3 | tr '\n' ' ')"); played and idle $(cmp -s "$work/ck.n" "$work/ck.idle" && echo "are the SAME run" || echo differ)"
		fi
		# shellcheck disable=SC2086
		wbx "$work/ck" --frames 5600 $ck --rewind 4250:5400 2>/dev/null | digests > "$work/ck.rw"
		if [ -s "$work/ck.w" ] && cmp -s "$work/ck.w" "$work/ck.rw"; then
			report "gq:rewind" PASS "kept at frame 4250 before the coins, restored at 5400 in the game and replayed: the straight run"
		else
			report "gq:rewind" FAIL "$(diff "$work/ck.w" "$work/ck.rw" | head -4 | tr '\n' ' ')"
		fi

		# A third player: this cabinet has three guns and two coin slots, and
		# player 3's coin is slot 1's. With a second credit paid (65 is player
		# 3's Coin), player 3's Start (64) must put a third player in the game,
		# and the trigger (72) and aim (axes 16 and 17) must then change the
		# game that a player 3 who only stands there is in.
		local p3j="--press 4420:6:65 --press 4460:6:65 --press 4520:6:64"
		local p3="$p3j --hold-axis 16:40000 --hold-axis 17:30000"
		for f in $(seq 4700 40 5560); do p3="$p3 --press $f:4:72"; done
		# shellcheck disable=SC2086
		native "$work/ck" --frames 5600 $ck $p3 2>/dev/null | digests > "$work/ck3.n"
		# shellcheck disable=SC2086
		wbx "$work/ck" --frames 5600 $ck $p3 2>/dev/null | digests > "$work/ck3.w"
		# shellcheck disable=SC2086
		native "$work/ck" --frames 5600 $ck $p3j 2>/dev/null | digests > "$work/ck3.j"
		if [ -s "$work/ck3.n" ] && cmp -s "$work/ck3.n" "$work/ck3.w" && ! cmp -s "$work/ck3.j" "$work/ck.n" && ! cmp -s "$work/ck3.n" "$work/ck3.j"; then
			report "gq:three" PASS "a second credit and player 3's Start: not the one-player run; player 3's trigger and aim: not the run where player 3 only joined; identical in the sandbox"
		else
			report "gq:three" FAIL "flavors $(cmp -s "$work/ck3.n" "$work/ck3.w" && echo agree || echo DIFFER); player 3 joining and not $(cmp -s "$work/ck3.j" "$work/ck.n" && echo "are the SAME run" || echo differ); player 3 firing and standing $(cmp -s "$work/ck3.n" "$work/ck3.j" && echo "are the SAME run" || echo differ)"
		fi
	fi
}

# ---- what content there is -------------------------------------------------

# Named, never "the first file in the folder": a leg that takes whatever is
# there changes meaning the day a file is added.
bios="${CHIMERA_PS1_BIOS:-}"
[ -n "$bios" ] || { [ -f "$root/tests/roms/bios/PSX_4.1(A).bin" ] && bios="$root/tests/roms/bios/PSX_4.1(A).bin"; }
disc="${CHIMERA_PS1_DISC:-}"
[ -n "$disc" ] || { [ -f "$root/tests/roms/NHL Blades of Steel 2000 (USA).cue" ] && disc="$root/tests/roms/NHL Blades of Steel 2000 (USA).cue"; }

if [ "$only_arcade" -eq 1 ]; then
	arcade_tier
	echo
	echo "$ok ok, $failed failed, $skipped skipped (the arcade legs only)"
	[ "$failed" -eq 0 ]
	exit $?
fi

# ---- a free BIOS: OpenBIOS, built from source ------------------------------
#
# Sony's ROM may not be put in a public repository, so on a CI runner the
# tiers below have nothing to boot. PCSX-Redux's OpenBIOS is a free
# replacement written from scratch (MIT), and with an empty tray its shell
# draws a spinning cube: the processor, the geometry coprocessor and the
# graphics chip, with nothing of anybody's in it (chimera issue 260). It is
# built here from a pinned source with a MIPS compiler, for these checks only;
# it is not in the package. tests/openbios/build.sh says how.
#   CHIMERA_NUGGET_SRC     a checkout of the source at tests/openbios/nugget.ref
#   CHIMERA_PS1_OPENBIOS   or an openbios.bin already built from it
openbios_tier() {
	local ob="${CHIMERA_PS1_OPENBIOS:-}" legs="openbios:compare openbios:rerecord openbios:drew" leg
	if [ -z "$ob" ] && [ -n "${CHIMERA_NUGGET_SRC:-}" ]; then
		if sh "$root/tests/openbios/build.sh" "$CHIMERA_NUGGET_SRC" "$work/openbios-build" > "$work/openbios-build.log" 2>&1; then
			ob="$work/openbios-build/openbios.bin"
			echo "# $(tail -1 "$work/openbios-build.log")"
		else
			# what the compiler said, where a CI log keeps it
			grep -E -i -B2 -A2 'error|no rule|not found|cannot' "$work/openbios-build.log" | head -40 | sed 's/^/# build: /'
			for leg in $legs; do
				report "$leg" FAIL "OpenBIOS did not build: $(grep -E -i -m1 'error|no rule|not found|cannot' "$work/openbios-build.log" || tail -1 "$work/openbios-build.log")"
			done
			return
		fi
	fi
	if [ -z "$ob" ]; then
		for leg in $legs; do
			report "$leg" SKIP "needs CHIMERA_NUGGET_SRC (a checkout of tests/openbios/nugget.ref) and a MIPS compiler, or CHIMERA_PS1_OPENBIOS=<openbios.bin>"
		done
		return
	fi
	mkwork "$work/openbios" "$ob"
	native "$work/openbios" --frames 600 2>/dev/null | digests > "$work/openbios.n"
	wbx "$work/openbios" --frames 600 2>/dev/null | digests > "$work/openbios.w"
	if [ -s "$work/openbios.n" ] && cmp -s "$work/openbios.n" "$work/openbios.w"; then
		report "openbios:compare" PASS "600 frames of OpenBIOS's shell: video, audio, lag and memory identical"
	else
		report "openbios:compare" FAIL "$(diff "$work/openbios.n" "$work/openbios.w" | head -4 | tr '\n' ' ')"
	fi
	wbx "$work/openbios" --frames 300 2>/dev/null | digests > "$work/openbios.s"
	wbx "$work/openbios" --frames 300 --rerecord 2>/dev/null | digests > "$work/openbios.r"
	if [ -s "$work/openbios.s" ] && cmp -s "$work/openbios.s" "$work/openbios.r"; then
		report "openbios:rerecord" PASS "300 frames with a state saved and loaded around every one, identical to the straight run"
	else
		report "openbios:rerecord" FAIL "$(diff "$work/openbios.s" "$work/openbios.r" | head -4 | tr '\n' ' ')"
	fi
	# the cube is drawn, and it turns: two frames apart, each with the
	# background and at least two of the cube's flat-shaded faces, and not the
	# same picture
	wbx "$work/openbios" --frames 300 --screenshot "$work/openbios-300.tga" > /dev/null 2>&1
	wbx "$work/openbios" --frames 600 --screenshot "$work/openbios-600.tga" > /dev/null 2>&1
	local drew
	drew="$(python3 - "$work/openbios-300.tga" "$work/openbios-600.tga" <<'PY' 2>&1
import sys
def colours(path):
    data = open(path, "rb").read()
    if len(data) < 18 or data[2] != 2:
        return None, None
    w, h, bpp = data[12] | data[13] << 8, data[14] | data[15] << 8, data[16] // 8
    px = data[18 + data[0]:18 + data[0] + w * h * bpp]
    return len(set(px[i:i + 3] for i in range(0, len(px), bpp))), px
a, pa = colours(sys.argv[1])
b, pb = colours(sys.argv[2])
if a is None or b is None:
    print("no picture"); sys.exit(1)
print("%d and %d colours, %s" % (a, b, "different pictures" if pa != pb else "THE SAME picture"))
sys.exit(0 if a >= 3 and b >= 3 and pa != pb else 1)
PY
)"
	if [ $? -eq 0 ]; then
		report "openbios:drew" PASS "the shell draws its cube and turns it: frames 300 and 600 have $drew"
	else
		report "openbios:drew" FAIL "frames 300 and 600: $drew"
	fi
}
openbios_tier

if [ -z "$bios" ]; then
	for leg in bios:compare bios:rerecord bios:rewind bios:deterministic clock:virtual input:reaches lag:counted region:rom \
		mouse:menu guns:wire multitap:pads ports:columns negcon:race widescreen:machine \
		gl:softpipe gl:machine gl:scale gl:rerecord gl:rewind \
		gpu:bridge gpu:machine gpu:picture-after-load gpu:picture-after-load-progressive gpu:rerecord \
		console:reset cards:export cards:import cards:refused cue:alone \
		disc:compare disc:rerecord disc:rewind disc:nameless engine:project \
		play:compare play:rewind gpu:play gpu:enhancements disc:swap; do
		report "$leg" SKIP "needs a PlayStation BIOS: tests/roms/bios/PSX_4.1(A).bin, or CHIMERA_PS1_BIOS=<file>"
	done
	arcade_tier
	echo
	echo "$ok ok, $failed failed, $skipped skipped"
	[ "$failed" -eq 0 ]
	exit $?
fi
echo "# bios: $(basename "$bios") sha1 $(sha1sum "$bios" | cut -c1-40)"
[ -n "$disc" ] && echo "# disc: $(basename "$disc")"

# ---- tier two: the machine, with a BIOS ------------------------------------

# the console with an empty tray: it boots, draws its logos and reaches its
# menu, which is 1100 frames in
mkwork "$work/shell" "$bios"
native "$work/shell" --frames 1200 2>/dev/null | digests > "$work/shell.n"
wbx "$work/shell" --frames 1200 2>/dev/null | digests > "$work/shell.w"
if [ -s "$work/shell.n" ] && cmp -s "$work/shell.n" "$work/shell.w"; then
	report "bios:compare" PASS "1200 frames of the console's own menu: video, audio, lag and memory identical"
else
	report "bios:compare" FAIL "$(diff "$work/shell.n" "$work/shell.w" | head -4 | tr '\n' ' ')"
fi

wbx "$work/shell" --frames 400 2>/dev/null | digests > "$work/shell.s"
wbx "$work/shell" --frames 400 --rerecord 2>/dev/null | digests > "$work/shell.r"
if [ -s "$work/shell.s" ] && cmp -s "$work/shell.s" "$work/shell.r"; then
	report "bios:rerecord" PASS "400 frames with a state saved and loaded around every one, identical to the straight run"
else
	report "bios:rerecord" FAIL "$(diff "$work/shell.s" "$work/shell.r" | head -4 | tr '\n' ' ')"
fi

# Rewind: keep the machine at frame 700, run on to 1150, put it back and run
# those frames again. The round-trip above saves and loads with nothing in
# between, so it cannot see state a snapshot does not reach - by frame 1150
# that state has moved on, and a machine that kept any would end up elsewhere.
# The two frames are chosen to LOOK different (a logo, and the menu): the
# console's picture is interlaced, half of every frame handed over is the
# field before it, and a frame buffer left out of the state would show the
# menu's lines under the logo for one frame and nowhere else.
#
# On weave, the console's interlace: since 2026-10-19 the default is
# progressive, which draws every line and keeps no field from one frame to
# the next, and so could not see a field left out of the state.
mkwork "$work/shellw" "$bios" '{"interlacing":"weave"}'
wbx "$work/shellw" --frames 1200 2>/dev/null | digests > "$work/shellw.w"
wbx "$work/shellw" --frames 1200 --rewind 700:1150 2>/dev/null | digests > "$work/shell.rw"
if [ -s "$work/shellw.w" ] && cmp -s "$work/shellw.w" "$work/shell.rw"; then
	report "bios:rewind" PASS "kept at frame 700, restored at 1150 and replayed: the 1200-frame run it would have been"
else
	report "bios:rewind" FAIL "$(diff "$work/shellw.w" "$work/shell.rw" | head -4 | tr '\n' ' ')"
fi

wbx "$work/shell" --frames 1200 2>/dev/null | digests > "$work/shell.w2"
native "$work/shell" --frames 1200 2>/dev/null | digests > "$work/shell.n2"
if cmp -s "$work/shell.w" "$work/shell.w2" && cmp -s "$work/shell.n" "$work/shell.n2"; then
	report "bios:deterministic" PASS "two runs of each flavor are the same run"
else
	report "bios:deterministic" FAIL "a second run of the same thing differed"
fi

# The clock. Every timer upstream reads is the machine's own tick counter, so
# its log - every line stamped with that clock - is the same text whether or
# not the host stalled for a third of a second in the middle. On the host's
# clock (a reference built with -Dhost_clock=true) the stamps are the wall's
# and no two runs agree, which is how this leg is seen to fail.
mkwork "$work/clock" "$bios" '{"verbose":true}'
native "$work/clock" --frames 300 2>&1 | plain | grep -E '^\[ *[0-9]+\.[0-9]+\]' > "$work/clock.a"
native "$work/clock" --frames 300 --stall 150:300 2>&1 | plain | grep -E '^\[ *[0-9]+\.[0-9]+\]' > "$work/clock.b"
lines="$(wc -l < "$work/clock.a")"
if [ "$lines" -gt 20 ] && cmp -s "$work/clock.a" "$work/clock.b"; then
	report "clock:virtual" PASS "$lines stamped log lines identical with the host stalled 300ms at frame 150"
else
	report "clock:virtual" FAIL "$lines lines; a stalled host changed what the machine's clock said: $(diff "$work/clock.a" "$work/clock.b" | head -2 | tr '\n' ' ')"
fi

# Input: Cross at the menu opens the memory card screen. Idle and pressed must
# differ - or the press never reached the machine and "both flavors agree"
# means nothing - and pressed must agree across flavors.
native "$work/shell" --frames 1300 2>/dev/null | digests > "$work/idle.n"
native "$work/shell" --frames 1300 --press 1100:5:7 2>/dev/null | digests > "$work/press.n"
wbx "$work/shell" --frames 1300 --press 1100:5:7 2>/dev/null | digests > "$work/press.w"
if ! cmp -s "$work/idle.n" "$work/press.n" && [ -s "$work/press.n" ] && cmp -s "$work/press.n" "$work/press.w"; then
	report "input:reaches" PASS "Cross at frame 1100 changes the picture, and changes it the same way in the sandbox"
else
	report "input:reaches" FAIL "idle and pressed $(cmp -s "$work/idle.n" "$work/press.n" && echo "are the SAME run" || echo differ); flavors $(cmp -s "$work/press.n" "$work/press.w" && echo agree || echo disagree)"
fi

lag="$(sed -n 's/^lagFrames=//p' "$work/idle.n")"
if [ -n "$lag" ] && [ "$lag" -gt 0 ] && [ "$lag" -lt 1300 ]; then
	report "lag:counted" PASS "$lag of 1300 frames went unpolled: the boot logos do not read the pad, the menu does"
else
	report "lag:counted" FAIL "lagFrames=${lag:-missing} of 1300: either every frame polls or none does"
fi

# ---- an empty tray is the console its ROM was made for ------------------------
#
# "auto" reads the region off the disc, and with no disc upstream makes a
# North American console - which a European ROM then drives in PAL mode off
# the NTSC crystal. The ROM decides instead: a second BIOS, a European one,
# must boot a console that keeps the PAL field rate, in both flavors, while
# the first one's stays what it was.
bios_pal="${CHIMERA_PS1_BIOS_PAL:-}"
[ -n "$bios_pal" ] || { [ -f "$root/tests/roms/bios/PSX_4.1(E).bin" ] && bios_pal="$root/tests/roms/bios/PSX_4.1(E).bin"; }
if [ -z "$bios_pal" ]; then
	report "region:rom" SKIP "needs a European BIOS as well: tests/roms/bios/PSX_4.1(E).bin, or CHIMERA_PS1_BIOS_PAL=<file>"
else
	mkwork "$work/palrom" "$bios_pal" '{"bios":"v4.1-e"}'
	native "$work/palrom" --frames 600 2>/dev/null | digests > "$work/palrom.n"
	wbx "$work/palrom" --frames 600 2>/dev/null | digests > "$work/palrom.w"
	pal_rate="$(sed -n 's/^vsync=//p' "$work/palrom.n")"
	us_rate="$(sed -n 's/^vsync=//p' "$work/shell.n")"
	if [ -s "$work/palrom.n" ] && cmp -s "$work/palrom.n" "$work/palrom.w" && [ "$pal_rate" = "53203425/1069484" ] && [ "$us_rate" = "53693175/897619" ]; then
		report "region:rom" PASS "a European BIOS and an empty tray is a PAL console ($pal_rate, 49.75 fields a second) in both flavors; the American one stays at $us_rate"
	else
		report "region:rom" FAIL "European BIOS: field rate [$pal_rate], want 53203425/1069484; American: [$us_rate]; flavors $(cmp -s "$work/palrom.n" "$work/palrom.w" && echo agree || echo DIFFER)"
	fi
fi

# ---- the hardware renderer, on the OpenGL inside the sandbox -----------------
#
# The native reference has no OpenGL - Mesa is built for the guest and nothing
# else - so these legs are the sandbox against itself: the hardware renderer
# against the software one where they must agree (the processor, the sound),
# and against itself across a state. What they do NOT stand in for: a real
# driver. softpipe is the one GL every machine draws identically with, which is
# its whole use here and exactly why it says nothing about a graphics card.
mkwork "$work/gl" "$bios" '{"renderer":"opengl","verbose":true}'
gl_log="$(wbx "$work/gl" --frames 300 2>&1 | plain)"
if grep -q 'has no OpenGL in it' <<< "$gl_log"; then
	for leg in gl:softpipe gl:machine gl:scale gl:rerecord gl:rewind; do
		report "$leg" SKIP "this core.wbx was built without a guest Mesa: waterbox/setup-mesa.sh, then setup-guest.sh -- -Dmesa_guest_dir=build/mesa"
	done
else
	gl_renderer="$(sed -n 's/.*GL_RENDERER: //p' <<< "$gl_log" | head -1)"
	hw="$(grep -c 'GPU_HW: Created HW framebuffer' <<< "$gl_log")"
	if [ "$gl_renderer" = "softpipe" ] && [ "$hw" -ge 1 ]; then
		report "gl:softpipe" PASS "the hardware renderer came up on GL_RENDERER softpipe, inside the sandbox"
	else
		report "gl:softpipe" FAIL "GL_RENDERER [${gl_renderer:-none}], hardware framebuffers made: $hw"
	fi

	# the same 500 frames, drawn by each renderer: one machine
	mkwork "$work/gl1" "$bios" '{"renderer":"opengl"}'
	wbx "$work/shell" --frames 500 2>/dev/null > "$work/sw.500"
	wbx "$work/gl1" --frames 500 2>/dev/null > "$work/gl.500"
	sw_v="$(sed -n 's/^videoHash=//p' "$work/sw.500")"; gl_v="$(sed -n 's/^videoHash=//p' "$work/gl.500")"
	if [ -n "$gl_v" ] && cmp -s <(machine_only < "$work/sw.500") <(machine_only < "$work/gl.500"); then
		report "gl:machine" PASS "500 frames: the processor, the sound and the polls are the software renderer's (pictures: $([ "$sw_v" = "$gl_v" ] && echo identical || echo different, as two rasterisers are))"
	else
		report "gl:machine" FAIL "$(diff <(machine_only < "$work/sw.500") <(machine_only < "$work/gl.500") | head -4 | tr '\n' ' ')"
	fi

	# twice the resolution is twice the picture - and still the same machine
	mkwork "$work/gl2" "$bios" '{"renderer":"opengl","internalResolution":"2x"}'
	wbx "$work/gl2" --frames 300 2>/dev/null > "$work/gl2.300"
	wbx "$work/gl1" --frames 300 2>/dev/null > "$work/gl1.300"
	s1="$(sed -n 's/^videoSize=//p' "$work/gl1.300")"; s2="$(sed -n 's/^videoSize=//p' "$work/gl2.300")"
	w1="${s1%x*}"; h1="${s1#*x}"
	if [ -n "$s1" ] && [ "$s2" = "$((w1 * 2))x$((h1 * 2))" ] && cmp -s <(machine_only < "$work/gl1.300") <(machine_only < "$work/gl2.300"); then
		report "gl:scale" PASS "2x hands over $s2 where 1x hands over $s1, and the machine is the same"
	else
		report "gl:scale" FAIL "1x [$s1], 2x [$s2]; machine $(cmp -s <(machine_only < "$work/gl1.300") <(machine_only < "$work/gl2.300") && echo same || echo DIFFERS)"
	fi

	# and 16x, the most offered (user-decided, 2026-10-19): the renderer
	# takes it (8x was the most once, and asked for 16x the core drew 8x), and
	# the machine is the 1x run's. The picture is not waited for: the menu's
	# first scaled frame is near frame 300, and a 16x frame (10240x7168, 290
	# MB handed over) takes softpipe some 3 s, about 975 s for the 300 frames
	# (measured 2026-10-19: 10240x7168 at frame 300, the menu at 16x).
	mkwork "$work/gl16" "$bios" '{"renderer":"opengl","internalResolution":"16x","verbose":true}'
	wbx "$work/gl16" --frames 120 > "$work/gl16.120" 2> "$work/gl16.log"
	wbx "$work/gl1" --frames 120 2>/dev/null > "$work/gl1.120"
	scale16="$(cat "$work/gl16.120" "$work/gl16.log" | plain | sed -n 's/.*GPU_HW: Resolution Scale: \([0-9]* ([0-9x]*)\).*/\1/p' | head -1)"
	if [ "$scale16" = "16 (16384x8192)" ] && [ -s "$work/gl1.120" ] && cmp -s <(machine_only < "$work/gl1.120") <(machine_only < "$work/gl16.120"); then
		report "gl:scale16" PASS "asked for 16x the renderer draws at $scale16, and 120 frames are the 1x run's machine"
	else
		report "gl:scale16" FAIL "the renderer's scale [${scale16:-none}], want 16 (16384x8192); machine $(cmp -s <(machine_only < "$work/gl1.120") <(machine_only < "$work/gl16.120") && echo same || echo DIFFERS)"
	fi

	# what the GL holds is in the guest's memory, so a state carries it
	wbx "$work/gl1" --frames 300 --rerecord 2>/dev/null | digests > "$work/gl.rr"
	if [ -s "$work/gl.rr" ] && cmp -s <(digests < "$work/gl1.300") "$work/gl.rr"; then
		report "gl:rerecord" PASS "300 frames with a state saved and loaded around every one: the straight run, picture included"
	else
		report "gl:rerecord" FAIL "$(diff <(digests < "$work/gl1.300") "$work/gl.rr" | head -4 | tr '\n' ' ')"
	fi
	wbx "$work/gl1" --frames 300 --rewind 120:260 2>/dev/null | digests > "$work/gl.rw"
	if [ -s "$work/gl.rw" ] && cmp -s <(digests < "$work/gl1.300") "$work/gl.rw"; then
		report "gl:rewind" PASS "kept at frame 120, restored at 260 and replayed: the straight run, picture included"
	else
		report "gl:rewind" FAIL "$(diff <(digests < "$work/gl1.300") "$work/gl.rw" | head -4 | tr '\n' ' ')"
	fi
fi

# ---- the hardware renderer on a host's OpenGL, through the GPU bridge --------
#
# The same renderer, with its GL calls leaving the sandbox for a real context
# (run-wbx built with -Dgl_bridge=true, which is the engine's host half in
# miniature). What draws is whatever this machine's EGL hands out; the leg
# names it. On a machine with no card that is llvmpipe, which is a fine place
# to prove the BRIDGE and says nothing about a graphics card (docs/gates.md, E):
# a real driver is measured by hand, on the card, and docs/PLAN.md has what it
# said.
if [ ! -x "$brg/run-wbx" ]; then
	for leg in gpu:bridge gpu:machine gpu:picture-after-load gpu:picture-after-load-progressive gpu:rerecord; do
		report "$leg" SKIP "needs a runner with the bridge's host half: meson setup build/meson-gl -Dminibox_dir=<miniBox> -Dgl_bridge=true"
	done
else
	mkwork "$work/hw" "$bios" '{"renderer":"opengl-hw","verbose":true}'
	hw_log="$(wbxgpu "$work/hw" --frames 300 2>&1 | plain)"
	hw_driver="$(sed -n 's/^gpu bridge: \([0-9].*\)/\1/p' <<< "$hw_log" | head -1)"
	if grep -q 'has no OpenGL in it' <<< "$hw_log"; then
		for leg in gpu:bridge gpu:machine gpu:picture-after-load gpu:picture-after-load-progressive gpu:rerecord; do
			report "$leg" SKIP "this core.wbx was built without a guest Mesa, and so without the GL device the bridge carries"
		done
	elif [ -z "$hw_driver" ]; then
		for leg in gpu:bridge gpu:machine gpu:picture-after-load gpu:picture-after-load-progressive gpu:rerecord; do
			report "$leg" SKIP "this machine gave the bridge no GL context: $(grep -m1 -i 'gpu bridge' <<< "$hw_log" | cut -c1-120)"
		done
	else
		hw_gl="$(sed -n 's/.*GL_RENDERER: //p' <<< "$hw_log" | head -1)"
		unhandled="$(grep -c 'opcodes this host has no case for' <<< "$hw_log")"
		if [ -n "$hw_gl" ] && [ "$hw_gl" != "softpipe" ] && [ "$unhandled" -eq 0 ] && grep -q 'GPU_HW: Created HW framebuffer' <<< "$hw_log"; then
			report "gpu:bridge" PASS "the renderer came up on the host's driver ($hw_gl) and every call it made was answered"
		else
			report "gpu:bridge" FAIL "GL_RENDERER [${hw_gl:-none}], unanswered-opcode reports: $unhandled"
		fi

		mkwork "$work/hw1" "$bios" '{"renderer":"opengl-hw"}'
		wbxgpu "$work/hw1" --frames 500 2>/dev/null > "$work/hw.500"
		[ -s "$work/sw.500" ] || wbx "$work/shell" --frames 500 2>/dev/null > "$work/sw.500"
		if grep -q '^machineHash=' "$work/hw.500" && cmp -s <(machine_only < "$work/sw.500") <(machine_only < "$work/hw.500"); then
			report "gpu:machine" PASS "500 frames drawn outside the sandbox: the processor, the sound and the polls are the software renderer's"
		else
			report "gpu:machine" FAIL "$(diff <(machine_only < "$work/sw.500") <(machine_only < "$work/hw.500") | head -4 | tr '\n' ' ')"
		fi

		# The console's VRAM is a texture on the card and in no state. The core is
		# told before a state is taken and fetches it; after a load it gives it
		# back, and the picture is the straight run's from the first frame. The
		# control is the same rewind with the core left untold: then what comes
		# back is what the card happened to hold, and the picture must differ - or
		# this leg cannot tell a core that carries VRAM from one that does not.
		# Frames 1100 and 1130 are both the menu, 3x the console's resolution.
		# On weave: on progressive, the default since 2026-10-19, the first
		# frame after the load is not the straight run's (the leg after this).
		mkwork "$work/hw3" "$bios" '{"renderer":"opengl-hw","internalResolution":"3x","interlacing":"weave"}'
		wbxgpu "$work/hw3" --frames 1140 2>/dev/null | digests > "$work/hw3.s"
		wbxgpu "$work/hw3" --frames 1140 --rewind 1100:1130 2>/dev/null | digests > "$work/hw3.t"
		GPU_ENV=CHIMERA_NO_STATE_SAVING=1 wbxgpu "$work/hw3" --frames 1140 --rewind 1100:1130 2>/dev/null | digests > "$work/hw3.u"
		told_ok=no; cmp -s "$work/hw3.s" "$work/hw3.t" && [ -s "$work/hw3.s" ] && told_ok=yes
		untold_differs=no; cmp -s "$work/hw3.s" "$work/hw3.u" || untold_differs=yes
		untold_machine=no; cmp -s <(machine_only < "$work/hw3.s") <(machine_only < "$work/hw3.u") && untold_machine=yes
		if [ "$told_ok" = yes ] && [ "$untold_differs" = yes ] && [ "$untold_machine" = yes ]; then
			report "gpu:picture-after-load" PASS "at 3x, kept at 1100 and restored at 1130: every frame after the load is the straight run's; left untold, the picture is not and the machine still is"
		else
			report "gpu:picture-after-load" FAIL "told and straight $([ "$told_ok" = yes ] && echo agree || echo DIFFER); untold picture $([ "$untold_differs" = yes ] && echo differs || echo "is the SAME - the control shows nothing"); untold machine $([ "$untold_machine" = yes ] && echo same || echo DIFFERS)"
		fi

		# The same on progressive, upstream's interlace and the default since
		# 2026-10-19. The first frame after a load is NOT the straight run's:
		# parts of the menu it does not redraw show the console's 15-bit VRAM
		# (dithered) where the straight run has the card's true-colour one,
		# although the card's VRAM is given back (measured 2026-10-19, an open
		# defect: docs/PLAN.md). Weave hides it - every line shown is drawn that
		# frame or kept from the last. Held here: the machine is the straight
		# run's, and so is every picture from the second frame after the load.
		mkwork "$work/hwp" "$bios" '{"renderer":"opengl-hw"}'
		GPU_ENV=GATE_FRAME_HASHES=1 wbxgpu "$work/hwp" --frames 1140 > "$work/hwp.s" 2> "$work/hwp.s.f"
		GPU_ENV=GATE_FRAME_HASHES=1 wbxgpu "$work/hwp" --frames 1140 --rewind 1100:1130 > "$work/hwp.t" 2> "$work/hwp.t.f"
		pa_after="$(python3 - "$work/hwp.s.f" "$work/hwp.t.f" <<'PY'
import sys
s = dict(l.split()[1:3] for l in open(sys.argv[1]) if l.startswith("frameHash "))
t = [l.split()[1:3] for l in open(sys.argv[2]) if l.startswith("frameHash ")]
after = t[1130:]
if len(after) != 40:
    print("bad"); sys.exit(1)
first = "the same" if s.get(after[0][0]) == after[0][1] else "not the same"
later = [f for f, h in after[1:] if s.get(f) != h]
print("first frame after the load %s; %d of the 39 after it differ" % (first, len(later)))
sys.exit(1 if later else 0)
PY
)"
		pa_rc=$?
		if [ $pa_rc -eq 0 ] && [ -s "$work/hwp.s" ] && cmp -s <(machine_only < "$work/hwp.s") <(machine_only < "$work/hwp.t"); then
			report "gpu:picture-after-load-progressive" PASS "on progressive, kept at 1100 and restored at 1130: the machine is the straight run's; $pa_after"
		else
			report "gpu:picture-after-load-progressive" FAIL "$pa_after; machine $(cmp -s <(machine_only < "$work/hwp.s") <(machine_only < "$work/hwp.t") && echo same || echo DIFFERS)"
		fi

		# a load before every frame: the renderer is rebuilt 300 times
		wbxgpu "$work/hw1" --frames 300 2>/dev/null | digests > "$work/hw.300"
		wbxgpu "$work/hw1" --frames 300 --rerecord 2>/dev/null | digests > "$work/hw.rr"
		if [ -s "$work/hw.300" ] && cmp -s "$work/hw.300" "$work/hw.rr"; then
			report "gpu:rerecord" PASS "300 frames with a state saved and loaded around every one - a new context each time - and the straight run, picture included"
		else
			report "gpu:rerecord" FAIL "$(diff "$work/hw.300" "$work/hw.rr" | head -4 | tr '\n' ' ')"
		fi
	fi
fi

# ---- the hardware renderer on Vulkan --------------------------------------------
#
# The Renderer setting's "vulkan-hw": the same hardware renderer on
# DuckStation's Vulkan device, through miniBox's Vulkan bridge. The runner
# hands both bridges over, as the frontend does, and the core uses the one
# the setting names. It needs a Vulkan driver on this computer - one that
# draws on the processor will do (Mesa's lavapipe) - and says SKIP without.
vk_legs="vk:bridge vk:machine vk:picture vk:picture-after-load vk:rerecord"
if [ ! -x "$brg/run-wbx" ]; then
	for leg in $vk_legs; do
		report "$leg" SKIP "needs a runner with the bridge's host half: meson setup build/meson-gl -Dminibox_dir=<miniBox> -Dgl_bridge=true"
	done
else
	mkwork "$work/vk" "$bios" '{"renderer":"vulkan-hw","verbose":true}'
	vk_log="$(wbxgpu "$work/vk" --frames 300 2>&1 | plain)"
	# (not anchored to the start of a line: the runner's two streams share
	# the pipe, and this line can land in the middle of one of the core's)
	vk_card="$(grep -o 'vulkan bridge: .*(Vulkan [^)]*)' <<< "$vk_log" | head -1 | cut -c16-)"
	if grep -q 'no Vulkan was handed over' <<< "$vk_log"; then
		for leg in $vk_legs; do
			report "$leg" SKIP "this computer handed the core no Vulkan: $(grep -m1 '^vulkan bridge:' <<< "$vk_log" | cut -c1-120)"
		done
	elif grep -q 'has no OpenGL in it' <<< "$vk_log"; then
		for leg in $vk_legs; do
			report "$leg" SKIP "this core.wbx was built without a guest Mesa, and so without the hardware renderer"
		done
	else
		refused="$(grep -c 'calls were refused' <<< "$vk_log")"
		if [ -n "$vk_card" ] && [ "$refused" -eq 0 ] && grep -q 'Trying to create a Vulkan GPU device' <<< "$vk_log" && grep -q 'GPU_HW: Created HW framebuffer' <<< "$vk_log"; then
			report "vk:bridge" PASS "the renderer came up on a Vulkan device ($vk_card) and no command it sent was refused"
		else
			report "vk:bridge" FAIL "card [${vk_card:-none}], runs with refused commands: $refused, $(grep -m1 -i 'vulkan' <<< "$vk_log" | cut -c1-100)"
		fi

		mkwork "$work/vk1" "$bios" '{"renderer":"vulkan-hw"}'
		wbxgpu "$work/vk1" --frames 500 2>/dev/null > "$work/vk.500"
		[ -s "$work/sw.500" ] || wbx "$work/shell" --frames 500 2>/dev/null > "$work/sw.500"
		if grep -q '^machineHash=' "$work/vk.500" && cmp -s <(machine_only < "$work/sw.500") <(machine_only < "$work/vk.500"); then
			report "vk:machine" PASS "500 frames drawn with Vulkan: the processor, the sound and the polls are the software renderer's"
		else
			report "vk:machine" FAIL "$(diff <(machine_only < "$work/sw.500") <(machine_only < "$work/vk.500") | head -4 | tr '\n' ' ')"
		fi

		# The picture itself, against OpenGL's on the same computer: the same
		# but for pixels the two do not round alike. Frame 300 is the diamond
		# of the console's start-up, all gradient; frame 1140 is its menu, and
		# is told apart from it.
		mkwork "$work/glp" "$bios" '{"renderer":"opengl-hw"}'
		wbxgpu "$work/glp" --frames 300 --screenshot "$work/gl.300.tga" >/dev/null 2>&1
		wbxgpu "$work/vk1" --frames 300 --screenshot "$work/vk.300.tga" >/dev/null 2>&1
		wbxgpu "$work/vk1" --frames 1140 --screenshot "$work/vk.1140.tga" >/dev/null 2>&1
		same="$(python3 "$here/tests/check-same-picture.py" "$work/gl.300.tga" "$work/vk.300.tga" 2>&1)"; same_rc=$?
		other="$(python3 "$here/tests/check-same-picture.py" "$work/gl.300.tga" "$work/vk.1140.tga" 2>&1)"; other_rc=$?
		if [ "$same_rc" = 0 ] && [ "$other_rc" = 1 ]; then
			report "vk:picture" PASS "Vulkan draws what OpenGL draws at frame 300: ${same%% (*}; another frame is told apart (${other%% (*})"
		else
			report "vk:picture" FAIL "against OpenGL at frame 300: $(head -1 <<< "$same"); another frame, which must differ: $(head -1 <<< "$other")"
		fi

		# VRAM is on the card, as with OpenGL: fetched before a state, given
		# back after a load. The same rewind, and the same control.
		mkwork "$work/vk3" "$bios" '{"renderer":"vulkan-hw","internalResolution":"3x"}'
		wbxgpu "$work/vk3" --frames 1140 2>/dev/null | digests > "$work/vk3.s"
		wbxgpu "$work/vk3" --frames 1140 --rewind 1100:1130 2>/dev/null | digests > "$work/vk3.t"
		GPU_ENV=CHIMERA_NO_STATE_SAVING=1 wbxgpu "$work/vk3" --frames 1140 --rewind 1100:1130 2>/dev/null | digests > "$work/vk3.u"
		told_ok=no; cmp -s "$work/vk3.s" "$work/vk3.t" && [ -s "$work/vk3.s" ] && told_ok=yes
		untold_differs=no; cmp -s "$work/vk3.s" "$work/vk3.u" || untold_differs=yes
		untold_machine=no; cmp -s <(machine_only < "$work/vk3.s") <(machine_only < "$work/vk3.u") && untold_machine=yes
		if [ "$told_ok" = yes ] && [ "$untold_differs" = yes ] && [ "$untold_machine" = yes ]; then
			report "vk:picture-after-load" PASS "at 3x, kept at 1100 and restored at 1130: every frame after the load is the straight run's; left untold, the picture is not and the machine still is"
		else
			report "vk:picture-after-load" FAIL "told and straight $([ "$told_ok" = yes ] && echo agree || echo DIFFER); untold picture $([ "$untold_differs" = yes ] && echo differs || echo "is the SAME - the control sees nothing"); untold machine $([ "$untold_machine" = yes ] && echo same || echo DIFFERS)"
		fi

		# a load before every frame: a new Vulkan device 300 times
		wbxgpu "$work/vk1" --frames 300 2>/dev/null | digests > "$work/vk.300"
		wbxgpu "$work/vk1" --frames 300 --rerecord 2>"$work/vk.rr.err" | digests > "$work/vk.rr"
		if [ -s "$work/vk.300" ] && cmp -s "$work/vk.300" "$work/vk.rr" && ! grep -q 'calls were refused' "$work/vk.rr.err"; then
			report "vk:rerecord" PASS "300 frames with a state saved and loaded around every one - a new Vulkan device each time - and the straight run, picture included; no command refused"
		else
			report "vk:rerecord" FAIL "$(diff "$work/vk.300" "$work/vk.rr" | head -4 | tr '\n' ' ') $(grep -m1 'calls were refused' "$work/vk.rr.err")"
		fi
	fi
fi

# ---- the other controllers ------------------------------------------------------
#
# A socket holds what its setting says, and the wires of that controller are
# the live ones. Each kind is proved on what this machine has to prove it on:
# the mouse on the console's own menu, which has a pointer for one; the light
# guns on the bytes the console reads from them, because no gun game is here;
# and (further down, with their games) the NeGcon in a race and the multitap
# in a game that polls it.

# The mouse. Wire: axes 10 and 11 are how far it moved this frame. The menu's
# pointer must go where it is dragged, the same in the sandbox.
mkwork "$work/mouse" "$bios" '{"port1":"mouse"}'
native "$work/mouse" --frames 1400 2>/dev/null | digests > "$work/mouse.idle"
native "$work/mouse" --frames 1400 --axis 1150:60:10:3:0 --axis 1150:60:11:2:0 2>/dev/null | digests > "$work/mouse.n"
wbx "$work/mouse" --frames 1400 --axis 1150:60:10:3:0 --axis 1150:60:11:2:0 2>/dev/null | digests > "$work/mouse.w"
if [ -s "$work/mouse.n" ] && cmp -s "$work/mouse.n" "$work/mouse.w" && ! cmp -s <(grep '^videoHash' "$work/mouse.n") <(grep '^videoHash' "$work/mouse.idle"); then
	report "mouse:menu" PASS "a mouse in port 1, dragged for a second at the console's menu: the pointer moves, and the run is the same in the sandbox"
else
	report "mouse:menu" FAIL "flavors $(cmp -s "$work/mouse.n" "$work/mouse.w" && echo agree || echo DIFFER); dragged and still $(cmp -s <(grep '^videoHash' "$work/mouse.n") <(grep '^videoHash' "$work/mouse.idle") && echo "are the SAME picture" || echo differ)"
fi

# The guns. Wire: 20 Trigger, 21 Shoot Offscreen; axes 8 and 9 the aim. What
# can be shown without a game is that each reaches the bytes the console reads
# from the gun, which its pad driver keeps in memory: a GunCon reports its aim
# in them, so trigger, aim and the off-screen shot must each change memory. A
# Justifier reports its aim by WHEN it interrupts, which nothing on this
# machine listens for - its trigger is held to the same test and its aim is
# not tested at all.
guns_ok=yes; guns_why=""
mkwork "$work/guncon" "$bios" '{"port1":"guncon"}'
native "$work/guncon" --frames 1300 2>/dev/null | digests > "$work/guncon.idle"
native "$work/guncon" --frames 1300 --press 1290:10:20 2>/dev/null | digests > "$work/guncon.trig"
native "$work/guncon" --frames 1300 --press 1290:10:21 2>/dev/null | digests > "$work/guncon.off"
native "$work/guncon" --frames 1300 --hold-axis 8:9000 --hold-axis 9:50000 --press 1290:10:20 2>/dev/null | digests > "$work/guncon.n"
wbx "$work/guncon" --frames 1300 --hold-axis 8:9000 --hold-axis 9:50000 --press 1290:10:20 2>/dev/null | digests > "$work/guncon.w"
ram() { grep '^domain\[Main RAM\]' "$1"; }
[ -s "$work/guncon.n" ] && cmp -s "$work/guncon.n" "$work/guncon.w" || { guns_ok=no; guns_why="$guns_why GunCon flavors DIFFER;"; }
cmp -s <(ram "$work/guncon.idle") <(ram "$work/guncon.trig") && { guns_ok=no; guns_why="$guns_why GunCon trigger reaches nothing;"; }
cmp -s <(ram "$work/guncon.idle") <(ram "$work/guncon.off") && { guns_ok=no; guns_why="$guns_why GunCon off-screen shot reaches nothing;"; }
cmp -s <(ram "$work/guncon.trig") <(ram "$work/guncon.n") && { guns_ok=no; guns_why="$guns_why GunCon aim reaches nothing;"; }
mkwork "$work/justifier" "$bios" '{"port1":"justifier"}'
native "$work/justifier" --frames 1300 2>/dev/null | digests > "$work/just.idle"
native "$work/justifier" --frames 1300 --press 1290:10:20 2>/dev/null | digests > "$work/just.n"
wbx "$work/justifier" --frames 1300 --press 1290:10:20 2>/dev/null | digests > "$work/just.w"
[ -s "$work/just.n" ] && cmp -s "$work/just.n" "$work/just.w" || { guns_ok=no; guns_why="$guns_why Justifier flavors DIFFER;"; }
cmp -s <(ram "$work/just.idle") <(ram "$work/just.n") && { guns_ok=no; guns_why="$guns_why Justifier trigger reaches nothing;"; }
if [ "$guns_ok" = yes ]; then
	report "guns:wire" PASS "a GunCon's trigger, off-screen shot and aim each change the bytes the console reads from it, and a Justifier's trigger does; the same in the sandbox (no gun game is here: this is the wire, not a game)"
else
	report "guns:wire" FAIL "$guns_why"
fi

# ---- the console's own button -------------------------------------------------

# Reset, on the case: pressed at the menu, the console starts over, and the
# run cannot be the one in which nobody pressed it.
native "$work/shell" --frames 1500 2>/dev/null | digests > "$work/reset.idle"
native "$work/shell" --frames 1500 --press 1150:3:200 2>/dev/null | digests > "$work/reset.n"
wbx "$work/shell" --frames 1500 --press 1150:3:200 2>/dev/null | digests > "$work/reset.w"
if [ -s "$work/reset.n" ] && ! cmp -s "$work/reset.idle" "$work/reset.n" && cmp -s "$work/reset.n" "$work/reset.w"; then
	report "console:reset" PASS "Reset at frame 1150 is a different run from the idle one, and the same run in the sandbox"
else
	report "console:reset" FAIL "pressed and idle $(cmp -s "$work/reset.idle" "$work/reset.n" && echo "are the SAME run" || echo differ); flavors $(cmp -s "$work/reset.n" "$work/reset.w" && echo agree || echo DIFFER)"
fi

# ---- memory cards ---------------------------------------------------------------
#
# A card is the machine's memory. It leaves through the save-data exports as
# memcard1.mcd, a project brings one in under that name, and the one must be
# able to become the other.
mkdir -p "$work/cards.n" "$work/cards.w" "$work/cards.back"
native "$work/shell" --frames 300 --savedata-out "$work/cards.n" > "$work/cards.n.log" 2>/dev/null
wbx "$work/shell" --frames 300 --savedata-out "$work/cards.w" > "$work/cards.w.log" 2>/dev/null
card="$work/cards.n/memcard1.mcd"
if [ -f "$card" ] && [ "$(stat -c %s "$card")" = 131072 ] && [ "$(head -c 2 "$card")" = "MC" ] \
	&& [ ! -e "$work/cards.n/memcard2.mcd" ] && cmp -s "$card" "$work/cards.w/memcard1.mcd"; then
	report "cards:export" PASS "slot 1's card leaves as memcard1.mcd, 131072 bytes of a formatted card, the same from both flavors; the empty slot 2 leaves nothing"
else
	report "cards:export" FAIL "$(ls -l "$work/cards.n" "$work/cards.w" 2>&1 | tr '\n' ' ' | cut -c1-200)"
fi

if [ ! -f "$card" ]; then
	report "cards:import" FAIL "no exported card to bring back"
	report "cards:refused" FAIL "no exported card to make the wrong files from"
else
	# a card somebody wrote on: the exported one with a mark in block 5, which
	# is data and under no checksum
	mkwork "$work/cardin" "$bios"
	python3 - "$card" "$work/cardin/memcard1.mcd" <<'PY'
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
d[5 * 8192 + 100:5 * 8192 + 116] = b'CHIMERA-WAS-HERE'
open(sys.argv[2], 'wb').write(d)
PY
	printf '{"savedata":["memcard1.mcd"]}' > "$work/cardin/slots"
	wbx "$work/cardin" --frames 300 --savedata-out "$work/cards.back" > /dev/null 2>&1
	if cmp -s "$work/cardin/memcard1.mcd" "$work/cards.back/memcard1.mcd" && ! cmp -s "$card" "$work/cards.back/memcard1.mcd"; then
		report "cards:import" PASS "a card a project brings is the card the console has: it comes back out byte for byte, and it is not the formatted one"
	else
		report "cards:import" FAIL "the card that went in $(cmp -s "$work/cardin/memcard1.mcd" "$work/cards.back/memcard1.mcd" && echo "came back" || echo "did NOT come back"); it $(cmp -s "$card" "$work/cards.back/memcard1.mcd" && echo "IS the formatted card" || echo "differs from the formatted card")"
	fi

	# three files this console must not swallow: not a card, a card under a
	# name nothing reads, and a card for a slot that is switched off
	refused=0
	mkwork "$work/bad1" "$bios"; head -c 1000 /dev/zero > "$work/bad1/memcard1.mcd"
	printf '{"savedata":["memcard1.mcd"]}' > "$work/bad1/slots"
	wbx "$work/bad1" --frames 1 2>&1 | plain | grep -q 'not a PlayStation memory card image' && refused=$((refused + 1))
	mkwork "$work/bad2" "$bios"; cp "$card" "$work/bad2/mysave.mcr"
	printf '{"savedata":["mysave.mcr"]}' > "$work/bad2/slots"
	wbx "$work/bad2" --frames 1 2>&1 | plain | grep -q 'rename it to the card it is' && refused=$((refused + 1))
	mkwork "$work/bad3" "$bios"; cp "$card" "$work/bad3/memcard2.mcd"
	printf '{"savedata":["memcard2.mcd"]}' > "$work/bad3/slots"
	wbx "$work/bad3" --frames 1 2>&1 | plain | grep -q 'Memory Card 2 is switched off' && refused=$((refused + 1))
	# ...and the same card for slot 2 is taken once the slot is on
	mkwork "$work/ok2" "$bios" '{"memcard2":true}'; cp "$card" "$work/ok2/memcard2.mcd"
	printf '{"savedata":["memcard2.mcd"]}' > "$work/ok2/slots"
	taken="$(wbx "$work/ok2" --frames 1 2>/dev/null | grep -c '^frames=1')"
	if [ "$refused" -eq 3 ] && [ "$taken" -eq 1 ]; then
		report "cards:refused" PASS "a file that is not a card, a card under another name and a card for a slot that is off are each refused with their reason; the last is taken once the slot is on"
	else
		report "cards:refused" FAIL "$refused of 3 refusals seen; the card for an enabled slot 2 was $([ "$taken" -eq 1 ] && echo taken || echo REFUSED)"
	fi
fi

# a cue sheet handed over without the files it names
mkwork "$work/cue" "$bios"
printf 'FILE "game.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' > "$work/cue/disc"
c_n="$(native "$work/cue" --frames 1 2>&1 | plain | grep -c 'cue sheet')"
c_w="$(wbx "$work/cue" --frames 1 2>&1 | plain | grep -c 'cue sheet')"
if [ "$c_n" -ge 1 ] && [ "$c_w" -ge 1 ]; then
	report "cue:alone" PASS "a cue sheet opened without its files is refused, and the refusal says what to do"
else
	report "cue:alone" FAIL "native $c_n, sandbox $c_w mentions of the cue sheet"
fi

# ---- tier three: a disc ----------------------------------------------------

if [ -z "$disc" ]; then
	for leg in disc:compare disc:rerecord disc:rewind disc:nameless engine:project multitap:pads ports:columns; do
		report "$leg" SKIP "needs a disc: tests/roms/NHL Blades of Steel 2000 (USA).cue, or CHIMERA_PS1_DISC=<a .cue>"
	done
else
	discdir="$(dirname "$disc")"
	mkwork "$work/disc" "$bios"
	# the sheet and every file it names, under their own names, and the slot map
	cp "$disc" "$work/disc/"
	sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$disc" | while IFS= read -r f; do ln -s "$discdir/$f" "$work/disc/$f"; done
	printf '{"disc":["%s"]}' "$(basename "$disc")" > "$work/disc/slots"

	native "$work/disc" --frames 1800 --exercise 2>/dev/null | digests > "$work/disc.n"
	wbx "$work/disc" --frames 1800 --exercise 2>/dev/null | digests > "$work/disc.w"
	if [ -s "$work/disc.n" ] && cmp -s "$work/disc.n" "$work/disc.w"; then
		report "disc:compare" PASS "1800 frames of the game with the pad exercised: video, audio, lag and memory identical"
	else
		report "disc:compare" FAIL "$(diff "$work/disc.n" "$work/disc.w" | head -4 | tr '\n' ' ')"
	fi

	wbx "$work/disc" --frames 900 --exercise 2>/dev/null | digests > "$work/disc.s"
	wbx "$work/disc" --frames 900 --exercise --rerecord 2>/dev/null | digests > "$work/disc.r"
	if [ -s "$work/disc.s" ] && cmp -s "$work/disc.s" "$work/disc.r"; then
		report "disc:rerecord" PASS "900 frames with a state saved and loaded around every one, identical to the straight run"
	else
		report "disc:rerecord" FAIL "$(diff "$work/disc.s" "$work/disc.r" | head -4 | tr '\n' ' ')"
	fi

	wbx "$work/disc" --frames 1700 --exercise --rewind 1300:1600 2>/dev/null | digests > "$work/disc.rw"
	wbx "$work/disc" --frames 1700 --exercise 2>/dev/null | digests > "$work/disc.17"
	if [ -s "$work/disc.17" ] && cmp -s "$work/disc.17" "$work/disc.rw"; then
		report "disc:rewind" PASS "kept at frame 1300, restored at 1600 and replayed with the pad exercised: the straight run"
	else
		report "disc:rewind" FAIL "$(diff "$work/disc.17" "$work/disc.rw" | head -4 | tr '\n' ' ')"
	fi

	# A disc opened directly arrives as a file called "disc" with no extension,
	# and the core has to work out what it is from its bytes. Only a sheet of
	# one file can be tried this way; the machine must not be able to tell.
	tracks="$(grep -c '^ *FILE ' "$disc")"
	if [ "$tracks" -ne 1 ]; then
		report "disc:nameless" SKIP "the sheet names $tracks files; a disc opened directly is one file"
	else
		mkwork "$work/bare" "$bios"
		ln -s "$discdir/$(sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$disc")" "$work/bare/disc"
		native "$work/disc" --frames 1500 2>/dev/null | digests > "$work/named.n"
		native "$work/bare" --frames 1500 2>/dev/null | digests > "$work/bare.n"
		wbx "$work/bare" --frames 1500 2>/dev/null | digests > "$work/bare.w"
		if [ -s "$work/named.n" ] && cmp -s "$work/named.n" "$work/bare.n" && cmp -s "$work/bare.n" "$work/bare.w"; then
			report "disc:nameless" PASS "the image mounted as 'disc' runs as it does from its sheet, in both flavors"
		else
			report "disc:nameless" FAIL "named and nameless $(cmp -s "$work/named.n" "$work/bare.n" && echo agree || echo DIFFER); flavors $(cmp -s "$work/bare.n" "$work/bare.w" && echo agree || echo DIFFER)"
		fi
	fi

	# ---- tier four: the package, through Chimera's engine -------------------
	chimera_root="${CHIMERA_ROOT:-}"
	[ -n "$chimera_root" ] || for c in "$root/../chimera" "$root/../../chimera"; do
		[ -x "$c/build/meson-linux/chimera-run" ] && { chimera_root="$(cd "$c" && pwd)"; break; }
	done
	crun="$chimera_root/build/meson-linux/chimera-run"
	cpkg="$chimera_root/build/Cores/duckstation.chimeraCore"
	if [ -z "$chimera_root" ] || [ ! -x "$crun" ] || [ ! -f "$cpkg" ]; then
		report "engine:project" SKIP "needs chimera-run and an installed duckstation.chimeraCore (set CHIMERA_ROOT; run waterbox/build-package.sh -r <chimera>)"
	elif [ "$cpkg" -ot "$gst/core.wbx" ]; then
		report "engine:project" FAIL "the installed package is older than core.wbx: run waterbox/build-package.sh"
	else
		ep="$work/engine"; mkdir -p "$ep" "$ep/cards.native" "$ep/cards.engine"
		args=("disc=$work/disc/$(basename "$disc")")
		while IFS= read -r f; do args+=("support=$discdir/$f"); done < <(sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$disc")
		# the row of a movie is the engine's to say (make-project.py): a project
		# of no frames is asked for one frame of it, and the real project is
		# written from that
		# a recording is a movie's [Input] block (Chimera 38a88e0): the row the
		# engine records is its first row, not its first line
		python3 "$here/tests/make-project.py" "$cpkg" "$ep/shape.chimeraProject" 0 "${args[@]}" > "$ep/project.log" 2>&1
		"$crun" --project "$ep/shape.chimeraProject" "$cpkg" --files "$discdir" --firmware "bios=$bios" \
			--frames 1 --record "$ep/shape.txt" > "$ep/shape.log" 2>&1
		idle_row="$(grep -m1 '^|' "$ep/shape.txt" 2>/dev/null)"
		python3 "$here/tests/make-project.py" "$cpkg" "$ep/gate.chimeraProject" 900 "--idle-row=$idle_row" "${args[@]}" >> "$ep/project.log" 2>&1
		"$crun" --project "$ep/gate.chimeraProject" "$cpkg" --files "$discdir" --firmware "bios=$bios" \
			--dump "Main RAM=$ep/engine.ram" --export-savedata "$ep/cards.engine" --meta "$ep/meta.txt" > "$ep/run.log" 2>&1
		native "$work/disc" --frames 900 --dump-domain "Main RAM" "$ep/native.ram" > /dev/null 2>&1
		native "$work/disc" --frames 900 --savedata-out "$ep/cards.native" > /dev/null 2>&1
		if grep -q '^status=OK' "$ep/meta.txt" 2>/dev/null && [ -s "$ep/native.ram" ] && cmp -s "$ep/native.ram" "$ep/engine.ram" \
			&& cmp -s "$ep/cards.native/memcard1.mcd" "$ep/cards.engine/memcard1.mcd"; then
			report "engine:project" PASS "the installed package, from a project, through chimera-run: Main RAM after 900 frames is the native reference's, and so is the memory card it exports"
		else
			report "engine:project" FAIL "$(grep -h -E '^(status|detail)=' "$ep/meta.txt" 2>/dev/null | tr '\n' ' ') $(tail -1 "$ep/run.log" | plain | cut -c1-160)"
		fi
	fi

	# ---- a multitap ------------------------------------------------------------
	# One in port 1, with a pad in each of its other three sockets. The game
	# polls it (an ice hockey game takes eight players), so the pad in socket
	# 1B - the third block of the wire - must be able to change the run, and
	# must not be able to with no multitap there.
	withdisc() { # <dir>: the sheet, its files and the slot map, as $work/disc has them
		cp "$disc" "$1/"
		sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$disc" | while IFS= read -r f; do ln -s "$discdir/$f" "$1/$f"; done
		printf '{"disc":["%s"]}' "$(basename "$disc")" > "$1/slots"
	}
	mkwork "$work/tap" "$bios" '{"multitap":"port1","port1b":"digital","port1c":"digital","port1d":"digital"}'
	withdisc "$work/tap"
	native "$work/tap" --frames 1800 --exercise 2>/dev/null | digests > "$work/tap.a"
	native "$work/tap" --frames 1800 --exercise --exercise-pad 3 2>/dev/null | digests > "$work/tap.n"
	wbx "$work/tap" --frames 1800 --exercise --exercise-pad 3 2>/dev/null | digests > "$work/tap.w"
	native "$work/disc" --frames 1800 --exercise --exercise-pad 3 2>/dev/null | digests > "$work/notap.b"
	if [ -s "$work/tap.n" ] && cmp -s "$work/tap.n" "$work/tap.w" && ! cmp -s "$work/tap.a" "$work/tap.n" \
		&& cmp -s "$work/notap.b" "$work/disc.n" && ! cmp -s "$work/tap.a" "$work/disc.n"; then
		report "multitap:pads" PASS "a multitap in port 1 with four pads: the game is another machine with it, the pad in socket 1B changes the run and cannot without the multitap; identical in the sandbox"
	else
		report "multitap:pads" FAIL "flavors $(cmp -s "$work/tap.n" "$work/tap.w" && echo agree || echo DIFFER); pad 1B $(cmp -s "$work/tap.a" "$work/tap.n" && echo "changes NOTHING" || echo "changes the run"); the same pad with no multitap $(cmp -s "$work/notap.b" "$work/disc.n" && echo "changes nothing" || echo "CHANGES the run"); with and without a multitap $(cmp -s "$work/tap.a" "$work/disc.n" && echo "are the SAME machine" || echo differ)"
	fi

	# ---- the columns of a movie, for every kind of socket --------------------
	# A movie has a column for each LIVE control, and which are live is the
	# core's to say (IsButtonActive, IsAxisActive) for whatever is plugged in.
	# make-project.py restates that rule; the engine records a row; the two
	# must agree for a NeGcon, a mouse, the guns, the analog joystick and two
	# arrangements of multitap - or a movie made for one of them is read by
	# the frontend under the wrong names.
	if [ -z "$chimera_root" ] || [ ! -x "$crun" ] || [ ! -f "$cpkg" ]; then
		report "ports:columns" SKIP "needs chimera-run and an installed duckstation.chimeraCore (set CHIMERA_ROOT; run waterbox/build-package.sh -r <chimera>)"
	elif [ "$cpkg" -ot "$gst/core.wbx" ]; then
		report "ports:columns" FAIL "the installed package is older than core.wbx: run waterbox/build-package.sh"
	else
		cp_dir="$work/columns"; mkdir -p "$cp_dir"
		cargs=("disc=$work/disc/$(basename "$disc")")
		while IFS= read -r f; do cargs+=("support=$discdir/$f"); done < <(sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$disc")
		cols_ok=yes; cols_why=""; cols_n=0
		for conf in "port1=negcon" "port1=mouse" "port1=guncon port2=justifier" "port1=analogJoystick port2=negconRumble" \
			"multitap=port1 port1b=digital port1c=dualshock port1d=negcon" \
			"multitap=both port2=digital port2b=mouse port2d=guncon"; do
			sets=(); for kv in $conf; do sets+=("--set=$kv"); done
			python3 "$here/tests/make-project.py" "$cpkg" "$cp_dir/shape.chimeraProject" 0 "${sets[@]}" "${cargs[@]}" > "$cp_dir/log" 2>&1
			rm -f "$cp_dir/shape.txt"
			"$crun" --project "$cp_dir/shape.chimeraProject" "$cpkg" --files "$discdir" --firmware "bios=$bios" \
				--frames 1 --record "$cp_dir/shape.txt" >> "$cp_dir/log" 2>&1
			row="$(grep -m1 '^|' "$cp_dir/shape.txt" 2>/dev/null)"
			if [ -n "$row" ] && python3 "$here/tests/make-project.py" "$cpkg" "$cp_dir/check.chimeraProject" 1 "--idle-row=$row" "${sets[@]}" "${cargs[@]}" >> "$cp_dir/log" 2>&1; then
				cols_n=$((cols_n + 1))
			else
				cols_ok=no; cols_why="$cols_why [$conf] $(tail -1 "$cp_dir/log" | cut -c1-110);"
			fi
		done
		if [ "$cols_ok" = yes ]; then
			report "ports:columns" PASS "$cols_n arrangements of sockets - a NeGcon, a mouse, both guns, the analog joystick, two of multitap: the row the engine records has the columns the rule gives it"
		else
			report "ports:columns" FAIL "$cols_why"
		fi
	fi
fi

# ---- a game played: boot, menus, a race --------------------------------------
#
# Need for Speed: Porsche Unleashed (USA, SLUS-01104). Start skips the intro
# films, Cross every 150 frames walks the quick-race menus on their defaults,
# and from frame 5500 Cross is held, which is the accelerator: by frame 7200
# the car has driven half a minute of France, Riviera. It is one disc image of
# a data track and audio tracks, a film player (MDEC), 2D menus and a 3D race,
# which is most of what a PlayStation does. The presses are the wire's own
# indices (4 Start, 7 Cross); a different release of the game would need a
# different script, which is why the content is named.
game3d="${CHIMERA_PS1_GAME3D:-}"
[ -n "$game3d" ] || { [ -f "$root/tests/roms/Need for Speed 5 - Porsche Unleashed [U] [SLUS-01104].cue" ] && game3d="$root/tests/roms/Need for Speed 5 - Porsche Unleashed [U] [SLUS-01104].cue"; }
if [ -z "$game3d" ]; then
	for leg in play:compare play:rewind gpu:play gpu:enhancements negcon:race widescreen:machine; do
		report "$leg" SKIP "needs tests/roms/Need for Speed 5 - Porsche Unleashed [U] [SLUS-01104].cue with its image, or CHIMERA_PS1_GAME3D=<that .cue>"
	done
else
	g3dir="$(dirname "$game3d")"
	mk3d() { # <dir> <settings json>
		mkwork "$1" "$bios" "$2"
		cp "$game3d" "$1/"
		sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$game3d" | sort -u | while IFS= read -r f; do ln -s "$g3dir/$f" "$1/$f"; done
		printf '{"disc":["%s"]}' "$(basename "$game3d")" > "$1/slots"
	}
	presses="--press 900:4:4 --press 1100:4:4 --press 1300:4:4"
	for f in $(seq 1500 150 5400); do presses="$presses --press $f:4:7"; done
	presses="$presses --press 5500:1650:7"

	mk3d "$work/play" '{"fast_boot":true}'
	# shellcheck disable=SC2086
	native "$work/play" --frames 7200 $presses 2>/dev/null | digests > "$work/play.n"
	# shellcheck disable=SC2086
	wbx "$work/play" --frames 7200 $presses 2>/dev/null | digests > "$work/play.w"
	# the same run with nothing pressed must be a different one, or the script
	# reached nothing and "identical" is two idle machines agreeing
	native "$work/play" --frames 7200 2>/dev/null | digests > "$work/play.idle"
	if [ -s "$work/play.n" ] && cmp -s "$work/play.n" "$work/play.w" && ! cmp -s "$work/play.n" "$work/play.idle"; then
		report "play:compare" PASS "7200 frames from boot through the menus into a driven race: video, audio, lag, the machine every frame and memory identical"
	else
		report "play:compare" FAIL "flavors $(cmp -s "$work/play.n" "$work/play.w" && echo agree || echo DIFFER: "$(diff "$work/play.n" "$work/play.w" | head -3 | tr '\n' ' ')"); pressed and idle $(cmp -s "$work/play.n" "$work/play.idle" && echo "are the SAME run" || echo differ)"
	fi

	# kept in the menus, restored half a minute into the race
	# shellcheck disable=SC2086
	wbx "$work/play" --frames 7200 $presses --rewind 4000:6800 2>/dev/null | digests > "$work/play.rw"
	if [ -s "$work/play.w" ] && cmp -s "$work/play.w" "$work/play.rw"; then
		report "play:rewind" PASS "kept at frame 4000 in the menus, restored at 6800 in the race and replayed: the straight run"
	else
		report "play:rewind" FAIL "$(diff "$work/play.w" "$work/play.rw" | head -4 | tr '\n' ' ')"
	fi

	if [ ! -x "$brg/run-wbx" ] || [ -z "${hw_driver:-}" ]; then
		for leg in gpu:play gpu:enhancements; do
			report "$leg" SKIP "needs the bridge's runner (build/meson-gl) and a GL context on this machine"
		done
	else
		# The race, drawn by the hardware renderer with nothing turned on: the
		# machine must be the software renderer's, memory included - this game
		# does not read its picture back.
		# shellcheck disable=SC2086
		wbx "$work/play" --frames 5600 $presses 2>/dev/null > "$work/play.sw"
		mk3d "$work/playhw" '{"fast_boot":true,"renderer":"opengl-hw"}'
		# shellcheck disable=SC2086
		wbxgpu "$work/playhw" --frames 5600 $presses 2>/dev/null > "$work/play.hw"
		same_machine() { cmp -s <(grep -E '^(audioHash|lagFrames|machineHash|domain\[)' "$1") <(grep -E '^(audioHash|lagFrames|machineHash|domain\[)' "$2"); }
		if grep -q '^machineHash=' "$work/play.hw" && same_machine "$work/play.sw" "$work/play.hw"; then
			report "gpu:play" PASS "5600 frames into the race on the host's driver: sound, lag, the machine every frame and Main RAM are the software renderer's"
		else
			report "gpu:play" FAIL "$(diff <(grep -E '^(audioHash|lagFrames|machineHash|domain)' "$work/play.sw") <(grep -E '^(audioHash|lagFrames|machineHash|domain)' "$work/play.hw") | head -4 | tr '\n' ' ')"
		fi

		# The enhancements reach the renderer: each one, alone, must change the
		# picture of the race (True Colour is on by default, so it is turned
		# OFF). Two of them must leave the machine alone. PGXP
		# must NOT: it decides which polygons are drawn, and how long the
		# console's GPU is busy follows from that (measured, docs/PLAN.md).
		plain_v="$(sed -n 's/^videoHash=//p' "$work/play.hw")"
		wrong=""
		for pair in 'trueColor:"trueColor":false:same' 'textureFiltering:"textureFiltering":"bilinear":same' 'pgxp:"pgxp":true:differs'; do
			nm="${pair%%:*}"; rest="${pair#*:}"; want="${rest##*:}"; kv="${rest%:*}"
			mk3d "$work/enh" "{\"fast_boot\":true,\"renderer\":\"opengl-hw\",$kv}"
			# shellcheck disable=SC2086
			wbxgpu "$work/enh" --frames 5600 $presses 2>/dev/null > "$work/enh.$nm"
			v="$(sed -n 's/^videoHash=//p' "$work/enh.$nm")"
			[ -n "$v" ] && [ "$v" != "$plain_v" ] || wrong="$wrong; $nm left the picture as it was"
			if same_machine "$work/play.hw" "$work/enh.$nm"; then got=same; else got=differs; fi
			[ "$got" = "$want" ] || wrong="$wrong; with $nm the machine $got, expected $want"
		done
		if [ -z "$wrong" ]; then
			report "gpu:enhancements" PASS "true colour (turned off), bilinear filtering and PGXP each change the race's picture; the first two leave the machine alone and PGXP, as measured, does not"
		else
			report "gpu:enhancements" FAIL "${wrong#; }"
		fi
	fi

	# ---- a NeGcon ---------------------------------------------------------------
	# The same game, which takes one. Its analog button I is what this game
	# reads as Cross - pressed home through the menus and held as the
	# accelerator (axis 5) - and the twist is the steering (axis 4): a second
	# and a half of it in the race must change the race.
	mk3d "$work/negcon" '{"port1":"negcon","fast_boot":true}'
	ng="--press 900:4:4 --press 1100:4:4 --press 1300:4:4"
	for f in $(seq 1500 150 5400); do ng="$ng --axis $f:4:5:255:0"; done
	ng="$ng --axis 5500:1100:5:255:0"
	native "$work/negcon" --frames 6400 2>/dev/null | digests > "$work/ng.idle"
	# shellcheck disable=SC2086
	native "$work/negcon" --frames 6400 $ng 2>/dev/null | digests > "$work/ng.straight"
	# shellcheck disable=SC2086
	native "$work/negcon" --frames 6400 $ng --axis 6000:100:4:70:128 2>/dev/null | digests > "$work/ng.n"
	# shellcheck disable=SC2086
	wbx "$work/negcon" --frames 6400 $ng --axis 6000:100:4:70:128 2>/dev/null | digests > "$work/ng.w"
	if [ -s "$work/ng.n" ] && cmp -s "$work/ng.n" "$work/ng.w" && ! cmp -s "$work/ng.straight" "$work/ng.idle" && ! cmp -s "$work/ng.n" "$work/ng.straight"; then
		report "negcon:race" PASS "the game driven by a NeGcon - its analog button through the menus and as the accelerator, its twist in the race: each changes the run, and it is identical in the sandbox"
	else
		report "negcon:race" FAIL "flavors $(cmp -s "$work/ng.n" "$work/ng.w" && echo agree || echo DIFFER); the analog button $(cmp -s "$work/ng.straight" "$work/ng.idle" && echo "reaches NOTHING" || echo "is read"); the twist $(cmp -s "$work/ng.n" "$work/ng.straight" && echo "changes NOTHING" || echo "changes the race")"
	fi

	# ---- the widescreen hack -----------------------------------------------------
	# It widens what the geometry processor computes, so the video chip draws
	# other triangles and is busy for other lengths of time: the machine is a
	# different one with it on (measured, not assumed), and the same one in
	# both flavors.
	mk3d "$work/wide" '{"fast_boot":true,"widescreen":"16:9"}'
	# shellcheck disable=SC2086
	native "$work/play" --frames 6000 $presses 2>/dev/null | digests > "$work/wide.off"
	# shellcheck disable=SC2086
	native "$work/wide" --frames 6000 $presses 2>/dev/null | digests > "$work/wide.n"
	# shellcheck disable=SC2086
	wbx "$work/wide" --frames 6000 $presses 2>/dev/null | digests > "$work/wide.w"
	if [ -s "$work/wide.n" ] && cmp -s "$work/wide.n" "$work/wide.w" && ! cmp -s <(machine_only < "$work/wide.n") <(machine_only < "$work/wide.off"); then
		report "widescreen:machine" PASS "the race with the view widened for 16:9: identical in the sandbox, and another machine than the race without it"
	else
		report "widescreen:machine" FAIL "flavors $(cmp -s "$work/wide.n" "$work/wide.w" && echo agree || echo DIFFER); with and without $(cmp -s <(machine_only < "$work/wide.n") <(machine_only < "$work/wide.off") && echo "are the SAME machine - the setting changed nothing" || echo differ)"
	fi
fi

# ---- two discs, and the lid ----------------------------------------------------
#
# The two games as one project's two discs. At frame 1300 Disc Swap is held
# for two seconds: the lid goes up, and comes down on the second disc. The
# games are not a set - nothing asks for the second disc - so what this
# proves is the DRIVE: the console is told the lid opened, the new disc is
# the one read, and all of it is machine state.
if [ -z "$disc" ] || [ -z "$game3d" ]; then
	report "disc:swap" SKIP "needs both games: the disc and the 3D game above"
else
	mkwork "$work/two" "$bios" '{"verbose":true}'
	for sheet in "$disc" "$game3d"; do
		cp "$sheet" "$work/two/"
		sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$sheet" | sort -u | while IFS= read -r f; do ln -s "$(dirname "$sheet")/$f" "$work/two/$f"; done
	done
	printf '{"disc":["%s","%s"]}' "$(basename "$disc")" "$(basename "$game3d")" > "$work/two/slots"
	second="$(native "$work/two" --frames 1500 --press 1300:120:203 2>&1 | plain | grep -c 'Inserted media from .*SLUS-01104')"
	printf '{}' > "$work/two/settings"
	native "$work/two" --frames 1700 2>/dev/null | digests > "$work/two.idle"
	native "$work/two" --frames 1700 --press 1300:120:203 2>/dev/null | digests > "$work/two.n"
	wbx "$work/two" --frames 1700 --press 1300:120:203 2>/dev/null | digests > "$work/two.w"
	wbx "$work/two" --frames 1700 --press 1300:120:203 --rewind 1250:1600 2>/dev/null | digests > "$work/two.rw"
	if [ "$second" -ge 1 ] && [ -s "$work/two.n" ] && ! cmp -s "$work/two.idle" "$work/two.n" \
		&& cmp -s "$work/two.n" "$work/two.w" && cmp -s "$work/two.w" "$work/two.rw"; then
		report "disc:swap" PASS "the lid held up for 120 frames closes on the second disc; identical in the sandbox, and a rewind from before the swap to after it replays it"
	else
		report "disc:swap" FAIL "second disc inserted: $second; swapped and idle $(cmp -s "$work/two.idle" "$work/two.n" && echo "are the SAME run" || echo differ); flavors $(cmp -s "$work/two.n" "$work/two.w" && echo agree || echo DIFFER); rewind $(cmp -s "$work/two.w" "$work/two.rw" && echo agrees || echo DIFFERS)"
	fi
fi

arcade_tier

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
