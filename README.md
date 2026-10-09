# chimera-core-duckstation

DuckStation - the PlayStation - as a Chimera waterbox core.

Upstream is `stenzek/duckstation`, pinned as a submodule at
`extern/duckstation` to commit `14ab642` (2024-09-01): the last one offered
under the GPL-3.0. The pin does not move; `docs/PLAN.md` says why.

**Work in progress.** It boots a game in the sandbox identically to its
native build, on the software renderer and on the hardware renderer at up to
8x, and the package loads in Chimera. The hardware renderer draws through an
OpenGL compiled into the core, which is slow; the route to a real graphics
card, and the enhancements beyond resolution, are not built yet.
`docs/PLAN.md` has the milestones and what was measured.

## Building

It needs a Chimera checkout with miniBox built (Chimera's `AGENTS.md` has the
commands).

```sh
git submodule update --init
meson setup build/meson-native -Dminibox_dir=<miniBox>    # the native reference and the runners
ninja -C build/meson-native
bash waterbox/setup-mesa.sh -m <miniBox>                  # once: Mesa 24.0.9 for the guest, into build/mesa
sh waterbox/setup-guest.sh -m <miniBox> -- -Dmesa_guest_dir="$PWD/build/mesa"   # the sandboxed core
ninja -C build/meson-guest core.wbx
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

## The gate and the package

```sh
./waterbox/run-gate.sh                               # native against sandbox, leg by leg
./waterbox/build-package.sh -r <chimera checkout>    # -> build/Cores/duckstation.chimeraCore
```

The gate needs a BIOS at `tests/roms/bios/PSX_4.1(A).bin` (or
`CHIMERA_PS1_BIOS`) for anything past its first four checks, and a disc
(`CHIMERA_PS1_DISC`) for the rest. What is missing is reported SKIP.
