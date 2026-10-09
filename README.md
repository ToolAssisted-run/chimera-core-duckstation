# chimera-core-duckstation

DuckStation - the PlayStation - as a Chimera waterbox core.

Upstream is `stenzek/duckstation`, pinned as a submodule at
`extern/duckstation` to commit `14ab642` (2024-09-01): the last one offered
under the GPL-3.0. The pin does not move; `docs/PLAN.md` says why.

**Work in progress.** The native reference and the sandboxed core build, boot
a game and agree with each other; there is no package yet. `docs/PLAN.md` has
the milestones and what was measured.

## Building

It needs a Chimera checkout with miniBox built (Chimera's `AGENTS.md` has the
commands).

```sh
git submodule update --init
meson setup build/meson-native -Dminibox_dir=<miniBox>    # the native reference and the runners
ninja -C build/meson-native
sh waterbox/setup-guest.sh -m <miniBox>                   # the sandboxed core
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
