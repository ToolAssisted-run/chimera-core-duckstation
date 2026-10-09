# chimera-core-duckstation

DuckStation - the PlayStation - as a Chimera waterbox core.

Upstream is `stenzek/duckstation`, pinned as a submodule at
`extern/duckstation` to commit `14ab642` (2024-09-01): the last one offered
under the GPL-3.0. The pin does not move; `docs/PLAN.md` says why.

It runs the console and its games in the sandbox identically to its native
build: two ports of digital pad or DualShock, up to eight discs with a lid,
memory cards, savestates and rewind. The picture comes from the software
renderer, or from the hardware renderer at up to 8x with true colour, texture
filtering, downsampling, PGXP and crop - drawn by an OpenGL compiled into the
core, or (`opengl-hw`) on the computer's own graphics card through Chimera's
GPU bridge, which has been measured on one card (a GTX 1060, on Windows).

Not there yet: the other controllers (mouse, light guns, NeGcon) and the
multitap, the widescreen hack, and the arcade boards of ArcadeDuck.
`docs/PLAN.md` has the milestones, what was measured and what was not tried.

## Building

It needs a Chimera checkout with miniBox built. `docs/BUILDING.md` has every
step; in short:

```sh
git submodule update --init
bash waterbox/setup-mesa.sh -m <miniBox>                  # once: Mesa 24.0.9 for the guest, into build/mesa
meson setup build/meson-native -Dminibox_dir=<miniBox>    # the native reference and the runners
ninja -C build/meson-native
./waterbox/build-package.sh -m <miniBox> -r <chimera>     # core.wbx, and <chimera>/build/Cores/duckstation.chimeraCore
```

## Running it

Both runners take a folder holding what the frontend would mount: the BIOS
under the name `bios`, a disc image under its own name, and a `slots` file
naming it (`{"disc": ["game.cue"]}`).

```sh
build/meson-native/run-native <folder> --frames 600 --screenshot out.tga
build/meson-native/run-wbx build/meson-guest/core.wbx <folder> --frames 600
```

The two print the same digests of video, audio and memory, or something is
wrong. The BIOS and the games are yours to provide; none is in this
repository.

## The gates

```sh
CHIMERA_ROOT=<chimera> ./waterbox/run-gate.sh                 # native against sandbox, check by check
./waterbox/tests/run-frontend.sh --chimera-root <chimera>     # the package inside the frontend
```

The gates need a BIOS at `tests/roms/bios/PSX_4.1(A).bin` (or
`CHIMERA_PS1_BIOS`) for anything past the first four checks, and the two
games `docs/BUILDING.md` names for the rest. What is missing is reported
SKIP, and a SKIP is not a pass: with no content - which is all a public
runner has - the gates prove the build and the package, not the console.

## Licence

DuckStation at this pin is GPL-3.0, and so is the package. This repository's
own files are MIT, but for the patches and two files made from upstream's
(`LICENSE`).
