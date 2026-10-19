#!/bin/bash
# The frontend half of the gate: load the DuckStation package in Chimera (under
# Mono, on a private Xvfb display), run the console from a PROJECT for a fixed
# number of frames with nothing pressed, and require the first megabyte of its
# Main RAM to be byte-identical to the native reference. Then prove that a
# machine-shaping setting arrives through a project, that a project's disc
# does, that a movie's presses reach the pad on the frames they name, and that
# the package's keybinds become the frontend's defaults.
#
# Everything goes through a project, because that is what Chimera hands a
# core: a cue sheet and the files it names cannot be opened any other way.
#
# It needs what the core gate's second tier needs - a BIOS - because a
# PlayStation runs nothing without one; the disc leg needs the disc. Without
# them it reports SKIP and says so, rather than passing on nothing.
#
# Usage: ./run-frontend.sh --chimera-root <path> [--frames N]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
wb="$(cd "$here/.." && pwd)"
root="$(cd "$wb/.." && pwd)"
frames=600
chimera_root="${CHIMERA_ROOT:-}"
while [ $# -gt 0 ]; do
	case "$1" in
		--chimera-root) chimera_root="$2"; shift ;;
		--frames) frames="$2"; shift ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) break ;;
	esac
	shift
done

# A Chimera checkout is named, or found beside this repository (or beside its
# parent). No guess is made from the home directory.
if [ -z "$chimera_root" ]; then
	for candidate in "$root/../chimera" "$root/../../chimera"; do
		[ -d "$candidate" ] && { chimera_root="$candidate"; break; }
	done
fi
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || {
	echo "chimera checkout not found; pass --chimera-root <path>" >&2; exit 1; }
chimera_root="$(cd "$chimera_root" && pwd)"

emu_exe="$chimera_root/build/Chimera.exe"
package="$chimera_root/build/Cores/duckstation.chimeraCore"
crun="$chimera_root/build/meson-linux/chimera-run"
rn="$root/build/meson-native/run-native"
[ -f "$emu_exe" ] || { echo "Chimera not built: $emu_exe" >&2; exit 1; }
[ -f "$package" ] || { echo "package not installed: $package (run ../build-package.sh -r $chimera_root)" >&2; exit 1; }
[ -x "$crun" ] || { echo "chimera-run not built: $crun" >&2; exit 1; }
[ -x "$rn" ] || { echo "native reference not built" >&2; exit 1; }

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

# named content, as in run-gate.sh
bios="${CHIMERA_PS1_BIOS:-}"
[ -n "$bios" ] || { [ -f "$root/tests/roms/bios/PSX_4.1(A).bin" ] && bios="$root/tests/roms/bios/PSX_4.1(A).bin"; }
disc="${CHIMERA_PS1_DISC:-}"
[ -n "$disc" ] || { [ -f "$root/tests/roms/NHL Blades of Steel 2000 (USA).cue" ] && disc="$root/tests/roms/NHL Blades of Steel 2000 (USA).cue"; }

game3d="${CHIMERA_PS1_GAME3D:-}"
[ -n "$game3d" ] || { [ -f "$root/tests/roms/Need for Speed 5 - Porsche Unleashed [U] [SLUS-01104].cue" ] && game3d="$root/tests/roms/Need for Speed 5 - Porsche Unleashed [U] [SLUS-01104].cue"; }

if [ -z "$bios" ]; then
	for leg in menu:frontend settings:frontend disc:frontend play:frontend arcade:frontend keybinds; do
		report "$leg" SKIP "needs a PlayStation BIOS: tests/roms/bios/PSX_4.1(A).bin, or CHIMERA_PS1_BIOS=<file>"
	done
	echo
	echo "$ok ok, $failed failed, $skipped skipped"
	exit 0
fi

work="$here/work"
rm -rf "$work"
mkdir -p "$work"

export LD_LIBRARY_PATH="$chimera_root/build/dll:$chimera_root/build:/usr/lib/x86_64-linux-gnu"
export MONO_CRASH_NOFILE=1 MONO_WINFORMS_XIM_STYLE=disabled ALSOFT_DRIVERS=null
xvfb_pid=""
cleanup() { [ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null; }
trap cleanup EXIT
if [ -z "${DISPLAY:-}" ]; then
	command -v Xvfb >/dev/null || { echo "Xvfb not found (apt install xvfb)" >&2; exit 1; }
	for n in 90 91 92 93 94 95 96; do
		if [ ! -e "/tmp/.X11-unix/X$n" ]; then
			Xvfb ":$n" -screen 0 640x480x24 -nolisten tcp & xvfb_pid=$!
			export DISPLAY=":$n"; break
		fi
	done
	sleep 1
fi

config="$work/config.ini"
( cd "$chimera_root" && timeout 120 mono "$emu_exe" --headless "--config=$config" \
	"--lua=$here/exit.lua" ) > "$work/bootstrap.log" 2>&1
[ -f "$config" ] || { echo "config bootstrap failed (see $work/bootstrap.log)" >&2; exit 1; }
sed -i 's/"DispMethod": [0-9]/"DispMethod": 1/' "$config"
# this core for the PlayStation, whatever else is installed
python3 "$here/settings-config.py" "$config" "$work/config.base.ini" '{}' "{\"bios\": \"$bios\"}"

SLICE=1048576
firmware_args=("--firmware=bios=$bios")
# the same, as chimera-run takes it (the shape run: which controls are live)
crun_firmware=(--firmware "bios=$bios")

# A project and its native twin. <tag> <frames> then make-project.py's
# arguments (settings, slot files). The row of the movie is the engine's to
# say: a project of no frames is asked to record one.
run_project() {
	local tag="$1" nframes="$2"; shift 2
	local job="$work/job.$tag.txt"
	printf 'frames=%s\nout=%s/%s.ram.bin\nmeta=%s/%s.meta.txt\nshot=\nbytes=%s\nsystem=%s\n' "$nframes" "$work" "$tag" "$work" "$tag" "$SLICE" "${run_system:-PS1}" > "$job"
	rm -f "$work/$tag.ram.bin" "$work/$tag.meta.txt"
	python3 "$here/make-project.py" "$package" "$work/$tag.shape.chimeraProject" 0 "$@" > "$work/$tag.log" 2>&1 || return 1
	"$crun" --project "$work/$tag.shape.chimeraProject" "$package" --files "$work" \
		"${crun_firmware[@]}" --frames 1 --record "$work/$tag.shape.txt" >> "$work/$tag.log" 2>&1
	# the recording is a movie's [Input] block (Chimera 38a88e0): its first
	# row, not its first line
	local row; row="$(grep -m1 '^|' "$work/$tag.shape.txt" 2>/dev/null)"
	[ -n "$row" ] || return 1
	python3 "$here/make-project.py" "$package" "$work/$tag.chimeraProject" "$nframes" "--idle-row=$row" "$@" >> "$work/$tag.log" 2>&1 || return 1
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout 900 mono "$emu_exe" --headless \
		"--config=${run_config:-$work/config.base.ini}" "--project=$work/$tag.chimeraProject" "${firmware_args[@]}" \
		"--lua=$here/frontend-ram.lua" ) >> "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}

# <tag> <frames> <settings json> [disc sheet] [run-native arguments...]
native_ram() {
	local tag="$1" nframes="$2" settings="$3" sheet="${4:-}"
	[ $# -ge 4 ] && shift 4 || shift $#
	local wd="$work/native.$tag"
	rm -rf "$wd"; mkdir -p "$wd"
	cp "$bios" "$wd/bios"
	printf '%s' "$settings" > "$wd/settings"
	if [ -n "$sheet" ]; then
		cp "$sheet" "$wd/"
		sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$sheet" | sort -u | while IFS= read -r f; do ln -s "$(dirname "$sheet")/$f" "$wd/$f"; done
		printf '{"disc":["%s"]}' "$(basename "$sheet")" > "$wd/slots"
	fi
	"$rn" "$wd" --frames "$nframes" "$@" --dump-domain "Main RAM" "$work/native.$tag.ram.full" > "$work/native.$tag.txt" 2>&1 || return 1
	head -c "$SLICE" "$work/native.$tag.ram.full" > "$work/native.$tag.ram.bin"
}

# --- the machine the frontend builds must be the one the gate signed off on ---
# An empty tray: the console boots to its own menu, which needs nothing but
# the BIOS and is what a project with no disc is.
if ! native_ram "menu" "$frames" '{}'; then
	report "menu:frontend" FAIL "native runner error (see tests/work/native.menu.txt)"
elif ! run_project "menu" "$frames"; then
	report "menu:frontend" FAIL "no OK meta (see tests/work/menu.log)"
elif ! cmp -s "$work/native.menu.ram.bin" "$work/menu.ram.bin"; then
	report "menu:frontend" FAIL "Main RAM differs from the native reference"
elif [ "$(sed -n 's/^ramsize=//p' "$work/menu.meta.txt")" != "2097152" ]; then
	report "menu:frontend" FAIL "Main RAM is $(sed -n 's/^ramsize=//p' "$work/menu.meta.txt") bytes, want 2097152"
else
	report "menu:frontend" PASS "$frames frames of the console with an empty tray, from a project: Main RAM identical to the native reference"
fi

# --- a machine-shaping setting must reach the guest through a project ---------
# The console's region: a PAL console counts 314 lines a field at another
# rate, so its memory is another machine's by frame 600 - and still its own
# native reference's.
if ! native_ram "pal" "$frames" '{"region":"pal"}'; then
	report "settings:frontend" FAIL "native runner error (see tests/work/native.pal.txt)"
elif ! run_project "pal" "$frames" --set=region=pal; then
	report "settings:frontend" FAIL "run did not report OK (see tests/work/pal.log)"
elif ! cmp -s "$work/native.pal.ram.bin" "$work/pal.ram.bin"; then
	report "settings:frontend" FAIL "a PAL console's Main RAM differs from its native reference"
elif cmp -s "$work/native.pal.ram.bin" "$work/native.menu.ram.bin"; then
	report "settings:frontend" FAIL "a PAL console built the same RAM as an NTSC one: the setting changed nothing"
else
	report "settings:frontend" PASS "a project's region=pal matches its native reference and is another machine than the NTSC console"
fi

# --- a project's disc: the sheet and the files it names -----------------------
if [ -z "$disc" ]; then
	report "disc:frontend" SKIP "needs a disc: tests/roms/NHL Blades of Steel 2000 (USA).cue, or CHIMERA_PS1_DISC=<a .cue>"
else
	dframes=1500
	args=("--set=fast_boot=true" "disc=$disc")
	while IFS= read -r f; do args+=("support=$(dirname "$disc")/$f"); done < <(sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$disc" | sort -u)
	# the project resolves its files beside itself
	ln -sf "$disc" "$work/$(basename "$disc")"
	sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$disc" | sort -u | while IFS= read -r f; do ln -sf "$(dirname "$disc")/$f" "$work/$f"; done
	if ! native_ram "disc" "$dframes" '{"fast_boot":true}' "$disc"; then
		report "disc:frontend" FAIL "native runner error (see tests/work/native.disc.txt)"
	elif ! run_project "disc" "$dframes" "${args[@]}"; then
		report "disc:frontend" FAIL "no OK meta (see tests/work/disc.log)"
	elif ! cmp -s "$work/native.disc.ram.bin" "$work/disc.ram.bin"; then
		report "disc:frontend" FAIL "Main RAM differs from the native reference"
	elif cmp -s "$work/native.disc.ram.bin" "$work/native.menu.ram.bin"; then
		report "disc:frontend" FAIL "the disc run built the menu's RAM: nothing was in the tray"
	else
		report "disc:frontend" PASS "$dframes frames of $(basename "$disc") from a project: Main RAM identical to the native reference, and not the empty console's"
	fi
fi

# --- a movie with presses in it: the pad, through the frontend ----------------
# Every project above holds nothing down, so none of them says a row of a
# movie reaches the console's pad. The core gate's played game does: Need for
# Speed - Porsche Unleashed, Start past the films, Cross through the menus,
# Cross held on the grid - the same presses, written as a movie's rows in the
# package's own mnemonics. The reference is given them by wire index (4 Start,
# 7 Cross); the same game with nothing pressed is the control.
if [ -z "$game3d" ]; then
	report "play:frontend" SKIP "needs tests/roms/Need for Speed 5 - Porsche Unleashed [U] [SLUS-01104].cue with its image, or CHIMERA_PS1_GAME3D=<that .cue>"
else
	pframes=6000
	n_press=(--press 900:4:4 --press 1100:4:4 --press 1300:4:4)
	m_press=("--press=900:4:P1 Start" "--press=1100:4:P1 Start" "--press=1300:4:P1 Start")
	for f in $(seq 1500 150 5400); do n_press+=(--press "$f:4:7"); m_press+=("--press=$f:4:P1 Cross"); done
	n_press+=(--press 5500:500:7); m_press+=("--press=5500:500:P1 Cross")
	args=("--set=fast_boot=true" "${m_press[@]}" "disc=$game3d")
	while IFS= read -r f; do args+=("support=$(dirname "$game3d")/$f"); done < <(sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$game3d" | sort -u)
	ln -sf "$game3d" "$work/$(basename "$game3d")"
	sed -n 's/^ *FILE "\(.*\)".*/\1/p' "$game3d" | sort -u | while IFS= read -r f; do ln -sf "$(dirname "$game3d")/$f" "$work/$f"; done
	if ! native_ram "play" "$pframes" '{"fast_boot":true}' "$game3d" "${n_press[@]}" || ! native_ram "playidle" "$pframes" '{"fast_boot":true}' "$game3d"; then
		report "play:frontend" FAIL "native runner error (see tests/work/native.play.txt)"
	elif cmp -s "$work/native.play.ram.bin" "$work/native.playidle.ram.bin"; then
		report "play:frontend" FAIL "the reference with the presses and the one without built the same RAM: the script reaches nothing"
	elif ! run_project "play" "$pframes" "${args[@]}"; then
		report "play:frontend" FAIL "no OK meta (see tests/work/play.log)"
	elif ! cmp -s "$work/native.play.ram.bin" "$work/play.ram.bin"; then
		report "play:frontend" FAIL "Main RAM differs from the native reference$(cmp -s "$work/native.playidle.ram.bin" "$work/play.ram.bin" && echo ": it is the IDLE game's - the movie's presses never reached the pad")"
	else
		report "play:frontend" PASS "$pframes frames of a movie that presses its way from boot into a race: Main RAM identical to the native reference given the same presses, and not the idle game's"
	fi
fi

# --- the other machine in the package: an arcade board, from a project ---------
# Tekken on Namco System 11: a rom set in the Rom Set slot, the board's sound
# processor's rom as firmware, the machine setting, and a movie that puts two
# coins in, presses Start and fights - on the arcade panel's own wire, which
# is not the console's. The reference is the native build given the same
# presses by index (4 Start, 5 Coin, 6 and 8 two of the buttons).
ns11="${CHIMERA_ARCADE_NS11_SET:-}"
[ -n "$ns11" ] || { [ -f "$root/tests/roms/arcade/sets/tekkenac.zip" ] && ns11="$root/tests/roms/arcade/sets/tekkenac.zip"; }
ns11bios="${CHIMERA_ARCADE_NS11_BIOS:-}"
[ -n "$ns11bios" ] || { [ -f "$root/tests/roms/arcade/namcoc76.zip" ] && ns11bios="$root/tests/roms/arcade/namcoc76.zip"; }
if [ -z "$ns11" ] || [ -z "$ns11bios" ]; then
	report "arcade:frontend" SKIP "needs tests/roms/arcade/sets/tekkenac.zip (CHIMERA_ARCADE_NS11_SET) and tests/roms/arcade/namcoc76.zip (CHIMERA_ARCADE_NS11_BIOS)"
else
	aframes=2400
	n_press=(--press 700:6:5 --press 760:6:5 --press 860:6:4 --press 1000:6:6 --press 1100:6:6)
	m_press=("--press=700:6:P1 Coin" "--press=760:6:P1 Coin" "--press=860:6:P1 Start" "--press=1000:6:P1 Button 1" "--press=1100:6:P1 Button 1")
	for f in $(seq 1500 60 2340); do
		n_press+=(--press "$f:5:6" --press "$((f + 20)):5:8")
		m_press+=("--press=$f:5:P1 Button 1" "--press=$((f + 20)):5:P1 Button 3")
	done
	wd="$work/native.arcade"; rm -rf "$wd"; mkdir -p "$wd"
	cp "$ns11" "$wd/tekkenac.zip"; cp "$ns11bios" "$wd/namcoc76.zip"
	printf '{"romset":["tekkenac.zip"]}' > "$wd/slots"; printf '{"machine":"namco_system11"}' > "$wd/settings"
	cp "$ns11" "$work/tekkenac.zip"
	python3 "$here/settings-config.py" "$work/config.base.ini" "$work/config.arcade.ini" '{}' "{\"bios\": \"$bios\", \"namcoc76.zip\": \"$ns11bios\"}"
	arcade_ok=1
	"$rn" "$wd" --frames "$aframes" "${n_press[@]}" --dump-domain "Main RAM" "$work/native.arcade.ram.full" > "$work/native.arcade.txt" 2>&1 || arcade_ok=0
	"$rn" "$wd" --frames "$aframes" --dump-domain "Main RAM" "$work/native.arcadeidle.ram.full" > /dev/null 2>&1 || arcade_ok=0
	head -c "$SLICE" "$work/native.arcade.ram.full" > "$work/native.arcade.ram.bin"
	head -c "$SLICE" "$work/native.arcadeidle.ram.full" > "$work/native.arcadeidle.ram.bin"
	# the board's firmware in place of the console's, for this one project
	console_firmware=("${firmware_args[@]}"); console_crun_firmware=("${crun_firmware[@]}")
	firmware_args=("--firmware=namcoc76.zip=$ns11bios"); crun_firmware=(--firmware "namcoc76.zip=$ns11bios")
	run_config="$work/config.arcade.ini"; run_system=NAMCOS11
	arcade_ran=1
	run_project "arcade" "$aframes" "--arcadedb=$root/extern/arcadeduck/data/resources/arcadedb.yaml" \
		"--firmware=namcoc76.zip=$ns11bios" --set=machine=namco_system11 "${m_press[@]}" "romset=$ns11" || arcade_ran=0
	firmware_args=("${console_firmware[@]}"); crun_firmware=("${console_crun_firmware[@]}")
	unset run_config run_system
	if [ "$arcade_ok" -ne 1 ]; then
		report "arcade:frontend" FAIL "native runner error (see tests/work/native.arcade.txt)"
	elif cmp -s "$work/native.arcade.ram.bin" "$work/native.arcadeidle.ram.bin"; then
		report "arcade:frontend" FAIL "the reference with the presses and the one without built the same RAM: the script reaches nothing"
	elif [ "$arcade_ran" -ne 1 ]; then
		report "arcade:frontend" FAIL "no OK meta (see tests/work/arcade.log)"
	elif ! cmp -s "$work/native.arcade.ram.bin" "$work/arcade.ram.bin"; then
		report "arcade:frontend" FAIL "Main RAM differs from the native reference$(cmp -s "$work/native.arcadeidle.ram.bin" "$work/arcade.ram.bin" && echo ": it is the IDLE board's - the movie's presses never reached the panel")"
	elif [ "$(sed -n 's/^ramsize=//p' "$work/arcade.meta.txt")" != "4194304" ]; then
		report "arcade:frontend" FAIL "Main RAM is $(sed -n 's/^ramsize=//p' "$work/arcade.meta.txt") bytes, want 4194304"
	else
		report "arcade:frontend" PASS "$aframes frames of Tekken on Namco System 11 from a project, coins and a fight in its movie: Main RAM identical to the native reference given the same presses, and not the idle board's"
	fi
fi

# --- the bindings the package ships must become the frontend's defaults -------
python3 "$here/forget-controller.py" "$work/config.base.ini" "$work/config.keys.ini" "PlayStation Controllers"
# thirty frames, not one: the script may get its first turn a frame or two in
if run_config="$work/config.keys.ini" run_project "keys" 30; then
	if python3 "$here/check-keybinds.py" "$work/config.keys.ini" \
		"$wb/default_keybinds.json" "PlayStation Controllers" > "$work/keys.txt" 2>&1; then
		report "keybinds" PASS "$(cat "$work/keys.txt")"
	else
		report "keybinds" FAIL "$(head -1 "$work/keys.txt")"
	fi
else
	report "keybinds" FAIL "run did not report OK (see tests/work/keys.log)"
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ] && [ "$ok" -gt 0 ]
