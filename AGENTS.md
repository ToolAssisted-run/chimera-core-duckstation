# AGENTS.md - DuckStation core for Chimera

This repository builds DuckStation (the PlayStation) as a sandboxed guest for
Chimera, a frontend for tool-assisted speedruns. It produces one file,
`duckstation.chimeraCore`, which Chimera loads from its `Cores` folder. The
same sources are also built natively as a reference, and the gate holds the
two byte-identical. `.github/workflows/chimera.yml` is the authoritative
build recipe; `docs/BUILDING.md` explains it step by step; `docs/PLAN.md` is
the design log.

## The one rule that is not like the other cores'

**The upstream pin never moves.** `extern/duckstation` is at `14ab642`
(2024-09-01), the last commit of `stenzek/duckstation` under the GPL-3.0.
Everything after it is CC BY-NC-ND 4.0: it may not be modified and
redistributed, so it may not be pinned, cherry-picked, backported or read
and retyped. A fix this core needs is written here, as a patch, from the
GPL sources and the hardware's behaviour. `docs/PLAN.md` ("The pin") has the
analysis.

## Layout

- `extern/duckstation` - upstream, a pinned submodule. Never edited in place.
- `extern/zlib`, `extern/zstd` - pinned submodules the core links.
- `patches/` - 11 numbered patch files applied to `extern/duckstation`.
- `meson.build`, `meson_options.txt` - one build for both flavors (`minibox_dir`, `mesa_guest_dir`, `gl_bridge`, `host_clock`, `recompiler`).
- `waterbox/sources.sh` - the curated list of upstream sources both flavors compile.
- `waterbox/cinterface.cpp` - the guest ABI: frames, input, video, audio, memory domains, save data, the drive.
- `waterbox/host.cpp`, `audio-stream.cpp` - upstream's host and audio interfaces, answered for a core with no window and no sound card (GPL-3.0: made from upstream's files).
- `waterbox/memory-device.cpp` - the graphics device made of memory the software renderer presents to.
- `waterbox/gl-osmesa.cpp`, `gl-bridged.cpp`, `gl-host.c`, `gl-entry-points.txt` - OpenGL in the sandbox, and the GPU bridge's guest and host halves.
- `waterbox/resources.S`, `resources.cpp` - upstream's per-title database, compiled in.
- `waterbox/chimera-shim.h`, `stubs/`, `native-shim/` - what the guest toolchain lacks, and what a native build lacks of the guest's.
- `waterbox/run-native.c`, `run-wbx.c`, `gate-harness.h` - the native reference and the sandbox driver.
- `waterbox/apply-patches.sh`, `setup-mesa.sh`, `setup-guest.sh`, `build-package.sh`, `run-gate.sh` - the build and the core gate.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`, `package-licenses.json` - what the package declares to Chimera.
- `waterbox/tests/` - the frontend gate (`run-frontend.sh`) and its helpers.
- `tests/roms/` - your own BIOS and discs, ignored wholesale. Nothing in it is tracked.
- `build/` - every build output. Ignored by git.

## Set up the build environment

Ubuntu, as CI uses. Set the two paths first; both must be absolute.

```sh
chimera=/absolute/path/to/chimera              # a Chimera checkout
mb="$chimera/extern/chimera-common-minibox"    # miniBox, a submodule of it

sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3 bison flex pkg-config python3-mako python3-packaging curl xz-utils

git submodule update --init

[ -d "$chimera" ] || git clone https://github.com/ToolAssisted-run/chimera.git "$chimera"
git -C "$chimera" submodule update --init extern/chimera-common-minibox

[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

## Build

```sh
bash waterbox/setup-mesa.sh -m "$mb"     # once: fetches Mesa 24.0.9 (SHA-256 pinned), builds build/mesa
meson setup build/meson-native -Dminibox_dir="$mb"
ninja -C build/meson-native              # run-native, run-wbx
./waterbox/build-package.sh -m "$mb" -r "$chimera"   # core.wbx and the package
```

- `setup-mesa.sh` needs `bash`, and network access the first time.
- The patches are applied by `meson.build` at configure time. Nothing to run.
- `build-package.sh` configures `build/meson-guest` only if it has no
  `build.ninja`. An existing directory keeps the options it was configured
  with, including a missing `-Dmesa_guest_dir`.
- After changing a flag, the cross file or the guest toolchain, delete the
  build directory: no object is rebuilt for a flag.
- The GPU checks need a third build: `meson setup build/meson-gl
  -Dminibox_dir="$mb" -Dgl_bridge=true && ninja -C build/meson-gl run-wbx`
  (`libegl-dev`).
- A hand-built package is stamped `<commit>+local` (`-dirty` with changes in
  the tree). CI stamps the commit.

## Install the core into Chimera

`build-package.sh -r "$chimera"` writes
`$chimera/build/Cores/duckstation.chimeraCore`: the cores folder of a Chimera
source checkout. For a release bundle, copy the file into the `Cores` folder
beside `Chimera.exe`. Chimera downloads nothing. The same file runs on Linux
and on Windows.

## Test before you commit

```sh
CHIMERA_ROOT="$chimera" ./waterbox/run-gate.sh               # the core gate
./waterbox/tests/run-frontend.sh --chimera-root "$chimera"   # the frontend gate
```

and, in the Chimera checkout, the contract tests (`docs/BUILDING.md`).

- **Know what ran.** Both gates must end with `0 failed`. A PlayStation runs
  nothing without its ROM, so with no content the core gate runs four checks
  (`build:fresh`, `keybinds`, `nobios:refuses`, `gamedb:entry`), skips 31,
  and the frontend gate skips all five of its own. That is all CI proves.
- To gate the machine, provide the BIOS the gate names:
  `tests/roms/bios/PSX_4.1(A).bin`, or `CHIMERA_PS1_BIOS=<file>`. The disc
  checks want `NHL Blades of Steel 2000 (USA)`, the played-game checks `Need
  for Speed 5 - Porsche Unleashed [U] [SLUS-01104]` (`CHIMERA_PS1_DISC`,
  `CHIMERA_PS1_GAME3D`); the table in `docs/BUILDING.md` says which check
  needs what. If you have no BIOS, say in your report that the machine checks
  did not run; do not present the green tier as proof.
- The GPU checks draw on whatever this machine's EGL gives and name it. A
  result seen on llvmpipe is not said of a graphics card.
- The frontend gate needs Chimera built (natives and the .NET solution), the
  package installed, `run-native`, Mono and Xvfb. Its work folder
  (`waterbox/tests/work/`) is one folder: a control run leaves ITS projects
  and dumps there, so run the real gate last before using anything in it.
- A check that was skipped has proven nothing about your change.

## Rules of this repository

- **Upstream is patched, not edited.** Changes to `extern/duckstation` are
  numbered patches in `patches/` (`NNNN-chimera-<what it does>.patch`, `git
  diff` format, paths relative to the submodule).
  `waterbox/apply-patches.sh` applies the whole series or none, in file-name
  order. `git status` shows `extern/duckstation` as modified once it is
  applied; that is expected. Never commit inside the submodule. After adding
  or changing a patch, restore the submodule to its pin and configure again:
  the series must apply to pristine upstream, and `apply-patches.sh` must
  then print `already applied: all N patches`.
- **Determinism is the product.** The guest must not read host time, host
  randomness or anything else that differs between runs, and a savestate
  must round-trip. Time is the console's own, bought with executed
  instructions (patch 0003). Whatever a frame leaves behind for the next one
  lives in memory the savestate carries: `alloc_invisible` is for what can
  be thrown away.
- **Nothing on the graphics card is state.** The hardware renderer's VRAM is
  fetched into the core before a state is saved and put back after a load
  (patch 0010, `StateSaving`, `StateLoaded`). A change to the renderer must
  keep `gpu:picture-after-load` green, and that check must still fail when
  the core is not told a state is coming.
- **PGXP changes the machine**, not only the picture: it changes which
  polygons are culled and so how long the GPU is busy. A setting that looks
  like an enhancement is measured before it is described as one.
- **Run the gate before committing.** A new check needs a negative control:
  break the thing it checks and see it fail (`docs/PLAN.md` has the table).
- **Never commit a BIOS, a game or a memory card with somebody's save.**
  `tests/roms/` is ignored wholesale and is where local content goes. Copy
  content into it; never link to the originals.
- **Never add network access** to the core. A sandbox has no sockets.
- **Scripts stay executable.** Every `.sh` under `waterbox/` is git mode
  100755; meson and CI run them directly.
- **Documentation prose is plain ASCII.**
- **Commit messages** follow the log: `type(scope): a sentence that says what
  is now true`. The body is prose: the cause, the fix, what was measured,
  and the gate count. Issues live in the chimera repository and are cited as
  `chimera#N`. Assisted commits end with a `Co-Authored-By:` trailer.
- **Do not edit `.github/workflows`** unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - every build step, option and gate check.
- `docs/PLAN.md` - why each decision was made, what was measured, the sharp
  edges; grep it before changing a patch.
- `.github/workflows/chimera.yml` - the recipe CI runs.
- `LICENSE`, `waterbox/package-licenses.json` - the terms of this repository
  and of the package.
- In the Chimera checkout: `docs/new-core.md`, `docs/porting-a-core.md` (how
  a core is put together, patch traps), `docs/gates.md` (how a green gate can
  be wrong), `docs/gpu-bridge.md`, `docs/save-data.md`, `docs/multi-file.md`.
