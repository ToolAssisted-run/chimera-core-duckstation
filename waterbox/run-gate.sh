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
# Usage: ./run-gate.sh [-n <native build dir>] [-g <guest build dir>] [-b <bridge build dir>]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
nat="$root/build/meson-native"
gst="$root/build/meson-guest"
brg="$root/build/meson-gl"
while getopts "n:g:b:" opt; do
	case "$opt" in
		n) nat="$OPTARG" ;;
		g) gst="$OPTARG" ;;
		b) brg="$OPTARG" ;;
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
		gl:softpipe gl:machine gl:scale gl:rerecord gl:rewind \
		gpu:bridge gpu:machine gpu:picture-after-load gpu:rerecord cue:alone \
		disc:compare disc:rerecord disc:rewind disc:nameless engine:project \
		play:compare play:rewind gpu:play gpu:enhancements; do
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
	for leg in gpu:bridge gpu:machine gpu:picture-after-load gpu:rerecord; do
		report "$leg" SKIP "needs a runner with the bridge's host half: meson setup build/meson-gl -Dminibox_dir=<miniBox> -Dgl_bridge=true"
	done
else
	mkwork "$work/hw" "$bios" '{"renderer":"opengl-hw","verbose":true}'
	hw_log="$(wbxgpu "$work/hw" --frames 300 2>&1 | plain)"
	hw_driver="$(sed -n 's/^gpu bridge: \([0-9].*\)/\1/p' <<< "$hw_log" | head -1)"
	if grep -q 'has no OpenGL in it' <<< "$hw_log"; then
		for leg in gpu:bridge gpu:machine gpu:picture-after-load gpu:rerecord; do
			report "$leg" SKIP "this core.wbx was built without a guest Mesa, and so without the GL device the bridge carries"
		done
	elif [ -z "$hw_driver" ]; then
		for leg in gpu:bridge gpu:machine gpu:picture-after-load gpu:rerecord; do
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
		mkwork "$work/hw3" "$bios" '{"renderer":"opengl-hw","internalResolution":"3x"}'
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
	for leg in play:compare play:rewind gpu:play gpu:enhancements; do
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
		# picture of the race. Two of them must leave the machine alone. PGXP
		# must NOT: it decides which polygons are drawn, and how long the
		# console's GPU is busy follows from that (measured, docs/PLAN.md).
		plain_v="$(sed -n 's/^videoHash=//p' "$work/play.hw")"
		wrong=""
		for pair in 'trueColor:"trueColor":true:same' 'textureFiltering:"textureFiltering":"bilinear":same' 'pgxp:"pgxp":true:differs'; do
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
			report "gpu:enhancements" PASS "true colour, bilinear filtering and PGXP each change the race's picture; the first two leave the machine alone and PGXP, as measured, does not"
		else
			report "gpu:enhancements" FAIL "${wrong#; }"
		fi
	fi
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
