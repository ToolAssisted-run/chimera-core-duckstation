# chimera-core-duckstation

DuckStation - the PlayStation - and ArcadeDuck - the arcade boards built out
of a PlayStation - as one Chimera waterbox core.

The console is `stenzek/duckstation`, pinned as a submodule at
`extern/duckstation` to commit `14ab642` (2024-09-01): the last one offered
under the GPL-3.0. That pin does not move; `docs/PLAN.md` says why. The
arcade boards are `StillJC/ArcadeDuck`, a GPL-3.0 derivative of that same
commit, pinned at `extern/arcadeduck` and compiled whole beside it: one
`core.wbx`, two emulators, and a project's Machine setting picks.

**The PlayStation** runs its games in the sandbox identically to its native
build: any of the console's controllers in its two ports or, with multitaps,
eight sockets; up to eight discs with a lid, memory cards, savestates and
rewind. The picture comes from the software
renderer, or from the hardware renderer at up to 8x with true colour, texture
filtering, downsampling, PGXP and crop - drawn by an OpenGL compiled into the
core, or on the computer's own graphics card: with OpenGL (`opengl-hw`)
through Chimera's GPU bridge, or with Vulkan (`vulkan-hw`) through its Vulkan
bridge. Both have been measured on one card (a GTX 1060, on Windows).

**The arcade boards** run MAME rom sets. Two are offered, each proved on one
game: Namco System 11 (Tekken) and Konami GQ (Crypt Killer), with the panel
as the cabinet has it, the board's own non-volatile memory in and out, and
the same renderers. ArcadeDuck also knows Sony's ZN-1 and ZN-2 and the boards
built on them, and Konami's GV; they are compiled in and not offered until a
complete set has been booted on each.

The widescreen hack is a setting. An arcade panel is the cabinet's: up to
four players, guns, a wheel and pedals, a trackball. Not proved on a game,
for want of one: where a light gun's shot lands, a wheel, a trackball.
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

`-Darcade=false` builds the console alone.

## Running it

Both runners take a folder holding what the frontend would mount. For the
console: the BIOS under the name `bios`, a disc image under its own name, and
a `slots` file naming it (`{"disc": ["game.cue"]}`). For a board: the rom set
and its firmware archive under their own names, `slots` naming the set
(`{"romset": ["tekkenac.zip"]}`) and `settings` naming the machine
(`{"machine": "namco_system11"}`).

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
`CHIMERA_PS1_BIOS`) and the two games `docs/BUILDING.md` names for the
console, and the two rom sets it names for the boards. What is missing is
reported SKIP, and a SKIP is not a pass: with no content - which is all a
public runner has - the gates prove the build and the package, not the
machines.

## Licence

DuckStation at this pin is GPL-3.0, ArcadeDuck is GPL-3.0, and so is the
package. This repository's own files are MIT, but for the patches and two
files made from upstream's (`LICENSE`). No BIOS, rom set or game is here, and
none may be added.
