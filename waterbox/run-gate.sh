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
#   with a BIOS     tests/roms/bios/, or $CHIMERA_PS1_BIOS: equivalence, the
#                   savestate round-trip, determinism, the clock, input, lag
#   with a disc     tests/roms/, or $CHIMERA_PS1_DISC (a .cue): a game's own
#                   program, identical in the sandbox, by name and without one
#   with Chimera    $CHIMERA_ROOT, with chimera-run built and this package
#                   installed: the package through the engine
#
# A check whose content is missing reports SKIP and says what it would have
# proven. A SKIP is not a pass.
#
# Usage: ./run-gate.sh [-n <native build dir>] [-g <guest build dir>]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
nat="$root/build/meson-native"
gst="$root/build/meson-guest"
while getopts "n:g:" opt; do
	case "$opt" in
		n) nat="$OPTARG" ;;
		g) gst="$OPTARG" ;;
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
newest_src="$(find "$here" "$root/patches" "$root/meson.build" -type f \
	\( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.patch' -o -name 'meson.build' \) \
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
inp = cfg["input"]; name = inp["name"]
buttons = set(inp["buttons"]); axes = set(a["name"] for a in inp["axes"])
bad = [k for k in kb["AllTrollers"].get(name, {}) if k not in buttons]
bad += [k for k in kb["AllTrollersAnalog"].get(name, {}) if k not in axes]
unbound = [b for b in inp["buttons"] if b.startswith("P1 ") and b not in kb["AllTrollers"].get(name, {})]
mn = inp.get("mnemonics", {})
per = {}
for b in inp["buttons"]:
    port, _, key = b.partition(" ")
    ch = mn.get(b, mn.get(key))
    if ch is None: bad.append("no mnemonic: " + b); continue
    if ch in per.setdefault(port, {}): bad.append("%s and %s both write '%s'" % (per[port][ch], b, ch))
    per[port][ch] = b
if bad or unbound: print("; ".join(bad + ["unbound: " + u for u in unbound])); sys.exit(1)
print("%d buttons, %d axes, %d bindings" % (len(buttons), len(axes), len(kb["AllTrollers"][name]) + len(kb["AllTrollersAnalog"][name])))
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

# ---- what content there is -------------------------------------------------

# Named, never "the first file in the folder": a leg that takes whatever is
# there changes meaning the day a file is added.
bios="${CHIMERA_PS1_BIOS:-}"
[ -n "$bios" ] || { [ -f "$root/tests/roms/bios/PSX_4.1(A).bin" ] && bios="$root/tests/roms/bios/PSX_4.1(A).bin"; }
disc="${CHIMERA_PS1_DISC:-}"
[ -n "$disc" ] || { [ -f "$root/tests/roms/NHL Blades of Steel 2000 (USA).cue" ] && disc="$root/tests/roms/NHL Blades of Steel 2000 (USA).cue"; }

if [ -z "$bios" ]; then
	for leg in bios:compare bios:rerecord bios:rewind bios:deterministic clock:virtual input:reaches lag:counted \
		gl:softpipe gl:machine gl:scale gl:rerecord gl:rewind cue:alone \
		disc:compare disc:rerecord disc:rewind disc:nameless engine:project; do
		report "$leg" SKIP "needs a PlayStation BIOS: tests/roms/bios/PSX_4.1(A).bin, or CHIMERA_PS1_BIOS=<file>"
	done
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
wbx "$work/shell" --frames 1200 --rewind 700:1150 2>/dev/null | digests > "$work/shell.rw"
if [ -s "$work/shell.w" ] && cmp -s "$work/shell.w" "$work/shell.rw"; then
	report "bios:rewind" PASS "kept at frame 700, restored at 1150 and replayed: the 1200-frame run it would have been"
else
	report "bios:rewind" FAIL "$(diff "$work/shell.w" "$work/shell.rw" | head -4 | tr '\n' ' ')"
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
	for leg in disc:compare disc:rerecord disc:rewind disc:nameless engine:project; do
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
		ep="$work/engine"; mkdir -p "$ep"
		args=("disc=$work/disc/$(basename "$disc")")
		while IFS= read -r f; do args+=("support=$discdir/$f"); done < <(sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$disc")
		python3 "$here/tests/make-project.py" "$cpkg" "$ep/gate.chimeraProject" 900 "${args[@]}" > "$ep/project.log" 2>&1
		"$crun" --project "$ep/gate.chimeraProject" "$cpkg" --files "$discdir" --firmware "bios=$bios" \
			--dump "Main RAM=$ep/engine.ram" --meta "$ep/meta.txt" > "$ep/run.log" 2>&1
		native "$work/disc" --frames 900 --dump-domain "Main RAM" "$ep/native.ram" > /dev/null 2>&1
		if grep -q '^status=OK' "$ep/meta.txt" 2>/dev/null && [ -s "$ep/native.ram" ] && cmp -s "$ep/native.ram" "$ep/engine.ram"; then
			report "engine:project" PASS "the installed package, from a project, through chimera-run: Main RAM after 900 frames is the native reference's"
		else
			report "engine:project" FAIL "$(grep -h -E '^(status|detail)=' "$ep/meta.txt" 2>/dev/null | tr '\n' ' ') $(tail -1 "$ep/run.log" | plain | cut -c1-160)"
		fi
	fi
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
