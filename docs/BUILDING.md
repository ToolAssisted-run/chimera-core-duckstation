# Building the DuckStation core

This repository builds DuckStation (the PlayStation) and ArcadeDuck (the
arcade boards built out of a PlayStation) as one sandboxed guest for Chimera.
The result is one file, `duckstation.chimeraCore`, which Chimera loads. The steps below are the ones `.github/workflows/chimera.yml` runs on a
fresh clone on a public Ubuntu runner. Cores are built on Linux; the same
package file runs on Linux and on Windows, because the guest inside it is run
by Chimera's sandbox (miniBox) on either.

Names used below:

- `<chimera>` - a checkout of https://github.com/ToolAssisted-run/chimera.
- `<minibox>` - `<chimera>/extern/chimera-common-minibox`, the miniBox
  submodule: the sandbox host and the guest toolchain.

The commands use two shell variables for them. Both must be absolute paths.
Commands run from the root of this repository unless they say otherwise.

```sh
chimera=/absolute/path/to/chimera
mb="$chimera/extern/chimera-common-minibox"
```

## Requirements

**Operating system.** Ubuntu, x86-64. CI builds on GitHub's `ubuntu-latest`.

**Packages.**

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  meson ninja-build build-essential cmake pkg-config python3 bison flex \
  python3-mako python3-packaging curl xz-utils \
  mono-complete xvfb \
  libgl1-mesa-dev libx11-dev libxext-dev libasound2-dev
```

`bison`, `flex`, `python3-mako` and `python3-packaging` are for the guest
Mesa. `cmake`, `mono-complete`, `xvfb` and the `lib*-dev` packages are for
building Chimera and running the frontend gate; the core and the core gate do
not need them. `libegl-dev` is needed only for the GPU checks
(`-Dgl_bridge=true`, below).

**Toolchains.**

- C and C++: the gcc and g++ of `build-essential`. Built and gated here with
  GCC 14.2 and GCC 13.3. Upstream builds with clang only; under GCC this
  build passes `-fpermissive` (`meson.build` says where and why).
- .NET SDK 8.0, Mono and Xvfb, for Chimera and the frontend gate only.
- No Rust, no clang, no cmake for the core itself: the build is one
  `meson.build` over a curated list of upstream's sources.

**What the scripts fetch or build themselves.**

- `waterbox/setup-mesa.sh` downloads one file,
  `https://archive.mesa3d.org/mesa-24.0.9.tar.xz`, checks it against a pinned
  SHA-256 and stops if it does not match. The tarball is kept in
  `build/deps/` (or `$CHIMERA_DEPS_DIR`; `MESA_TARBALL=<file>` uses one you
  already have) and unpacked into `build/mesa/`. From it the script
  cross-builds Mesa for the guest: softpipe behind the gallium OSMesa front
  end, static, no LLVM. The output is `build/mesa/build-guest2/`.
- If the system has no `meson` with the Python modules `mako` and
  `packaging`, the same script makes a private venv at
  `$HOME/.cache/chimera-mesa-build-venv` (or `$MESA_BUILD_VENV`). With the
  packages above it does not.
- Everything else is source: this repository and its four submodules,
  `extern/duckstation`, `extern/arcadeduck`, `extern/zlib` and `extern/zstd`.
  Each emulator's other dependencies come from its own tree (`dep/`).
  DuckStation's per-title database (`data/resources/gamedb.yaml`) and
  ArcadeDuck's record of the boards and their rom sets
  (`data/resources/arcadedb.yaml`) are compiled into the core.
- The arcade legs' test tools read that record: `python3-yaml` (PyYAML), for
  `waterbox/tests/make-project.py` on an arcade project and
  `waterbox/tests/make-arcade-set.py`. The build itself does not need it.

## Get the sources

```sh
git clone https://github.com/ToolAssisted-run/chimera-core-duckstation.git
cd chimera-core-duckstation
git submodule update --init
```

`extern/duckstation` is pinned to `14ab642`, the last commit upstream offered
under the GPL-3.0. **The pin does not move**, and nothing later may be copied
in: `docs/PLAN.md` ("The pin") says why. `extern/arcadeduck` is pinned to
ArcadeDuck's `main`; it is GPL-3.0 and alive, and that pin may move
(`docs/PLAN.md`, "ArcadeDuck", says what a bump has to check).

Chimera, with the miniBox submodule:

```sh
git clone https://github.com/ToolAssisted-run/chimera.git "$chimera"
git -C "$chimera" submodule update --init extern/chimera-common-minibox
```

That is enough for the core, the package and the core gate. The frontend gate
and the contract tests need Chimera built, and for that:

```sh
git -C "$chimera" submodule update --init --recursive
```

Where the scripts look when they are not told:

| Script | Option | Otherwise |
| --- | --- | --- |
| `meson.build` | `-Dminibox_dir=<minibox>` | a Chimera checkout beside this repository or beside its parent, else an error |
| `waterbox/setup-mesa.sh`, `waterbox/setup-guest.sh` | `-m <minibox>` | `$MINIBOX_DIR`, else the same two places, else an error |
| `waterbox/build-package.sh` | `-r <chimera>`, `-m <minibox>` | Chimera: the same two places. miniBox: `$MINIBOX_DIR`, else `<chimera>/extern/chimera-common-minibox` |
| `waterbox/run-gate.sh` | `$CHIMERA_ROOT` | the same two places, else the `engine:project` check says SKIP |
| `waterbox/tests/run-frontend.sh` | `--chimera-root <chimera>` | `$CHIMERA_ROOT`, else the same two places |

Nothing is guessed from the home directory. Pass the paths, as CI does.

## Build miniBox

The host library, and the C++ guest toolchain:

```sh
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

`build/meson-cpp/guest-sysroot` is the sysroot `setup-mesa.sh` and
`setup-guest.sh` require. `run-wbx` links miniBox's host library from the
same build.

## Build the core

### Patches

Both emulators are pinned to pristine upstream. `waterbox/apply-patches.sh`
applies the 11 numbered patch files in `patches/` to `extern/duckstation`
and the 13 in `patches-arcade/` to `extern/arcadeduck`, each in file-name
order. There is no separate step: `meson.build` runs the script at configure
time, so both `meson setup build/meson-native` and `setup-guest.sh` apply
them. The script applies a whole series or none of it; run by hand on patched
trees it prints `already applied: extern/duckstation, all 11 patches` and the
same for the other. `git status` then shows both submodules as modified,
which is expected.

### One binary, two machines

The two emulators began as the same commit and define the same names, so
they are never linked against each other. Each - its three libraries, its
dependencies and its own copy of the adapter (`waterbox/cinterface.cpp` and
the files beside it, compiled once per tree) - is joined into one relocatable
object by `waterbox/link-machine.sh`, in which every symbol is made local but
one: a table of the guest ABI (`waterbox/machine.h`). `waterbox/dispatch.c`
is the ABI the frontend calls and passes each call to the table of the
machine the project's `machine` setting names. The step fails if an object
offers any name but its one.

### The guest Mesa

Once per checkout (and again when miniBox's guest toolchain changes):

```sh
bash waterbox/setup-mesa.sh -m "$mb"
```

It needs `bash`, and the network the first time. `MESA_BUILD_CONFIGURE_ONLY=1`
checks the recipe without compiling. Without a guest Mesa the core can still
be built, with the software renderer only (`MESA_GUEST_DIR=` for
`build-package.sh`, or no `-Dmesa_guest_dir` for `setup-guest.sh`); that is
not the published package.

### The native reference and the sandbox driver

The same sources compiled for the host, as `run-native`, and `run-wbx`, which
runs a `core.wbx` in miniBox. The gate compares the two.

```sh
meson setup build/meson-native -Dminibox_dir="$mb"
ninja -C build/meson-native
```

### The guest core

```sh
sh waterbox/setup-guest.sh -m "$mb" -- -Dmesa_guest_dir="$PWD/build/mesa"
ninja -C build/meson-guest core.wbx
```

`setup-guest.sh` writes the cross file for miniBox's musl toolchain
(`build/guest-cross.ini`) and configures `build/meson-guest`.
`build-package.sh` does this step itself when `build/meson-guest` has no
`build.ninja`; an existing directory keeps the options it was configured
with.

### Build options

`meson_options.txt` describes each. In short:

| Option | Default | What it is for |
| --- | --- | --- |
| `minibox_dir` | (found) | the miniBox checkout |
| `mesa_guest_dir` | empty | a guest Mesa: with one, the hardware renderer is built |
| `gl_bridge` | false | native builds only: `run-wbx` gets the GPU bridge's host half (needs `libegl-dev`), for the gate's GPU checks |
| `host_clock` | false | diagnostic, native only: upstream's host clock, the control for the `clock:virtual` check |
| `recompiler` | false | upstream's two code generators; not built, not gated (the interpreter runs a 3D race at eight times real speed) |
| `arcade` | true | the arcade boards: ArcadeDuck's tree beside the console's. Off, the core is the console alone |

The GPU checks want a third build directory:

```sh
meson setup build/meson-gl -Dminibox_dir="$mb" -Dgl_bridge=true
ninja -C build/meson-gl run-wbx
```

## Build the package

```sh
./waterbox/build-package.sh -m "$mb" -r "$chimera"
```

It builds `core.wbx` if needed, checks it with miniBox's `check-wbx.sh`, and
writes `$chimera/build/Cores/duckstation.chimeraCore`: `core.wbx`,
`waterbox.config`, `default_keybinds.json`, `file_slots.json`, the licence
texts of every component (`waterbox/package-licenses.json`) and `build.json`,
which records what built it. The package is written twice and must hash the
same both times.

A package built by hand is stamped `<commit>+local`, or `<commit>-dirty+local`
with changes in the tree. CI stamps the commit (`CORE_VERSION`).

## Install it into Chimera

`build-package.sh -r "$chimera"` has already put the package in the cores
folder of a Chimera source checkout. For a release bundle, copy
`duckstation.chimeraCore` into the `Cores` folder beside `Chimera.exe` (or
the folder set in File > Core Manager). Chimera downloads nothing.

## Run the gates

### The core gate

```sh
CHIMERA_ROOT="$chimera" ./waterbox/run-gate.sh
```

Native against sandbox, check by check. It must end with `0 failed`, and a
SKIP is not a pass. What runs depends on what content the machine has:

| With | Named by | Checks |
| --- | --- | --- |
| nothing | - | `build:fresh`, `keybinds`, `nobios:refuses`, `gamedb:entry` |
| a BIOS | `tests/roms/bios/PSX_4.1(A).bin`, or `CHIMERA_PS1_BIOS` | `bios:compare`, `bios:rerecord`, `bios:rewind`, `bios:deterministic`, `clock:virtual`, `input:reaches`, `lag:counted`, `console:reset`, `cards:export`, `cards:import`, `cards:refused`, and with a guest Mesa `gl:softpipe`, `gl:machine`, `gl:scale`, `gl:rerecord`, `gl:rewind` |
| a second BIOS, European | `tests/roms/bios/PSX_4.1(E).bin`, or `CHIMERA_PS1_BIOS_PAL` | `region:rom` |
| a disc | `tests/roms/NHL Blades of Steel 2000 (USA).cue` and its image, or `CHIMERA_PS1_DISC` | `cue:alone`, `disc:compare`, `disc:rerecord`, `disc:rewind`, `disc:nameless` |
| Chimera | `$CHIMERA_ROOT` with `build/meson-linux/chimera-run` and the package installed | `engine:project` |
| a 3D game | `tests/roms/Need for Speed 5 - Porsche Unleashed [U] [SLUS-01104].cue` and its image, or `CHIMERA_PS1_GAME3D` | `play:compare`, `play:rewind`; with both discs, `disc:swap` |
| a GL context | `build/meson-gl/run-wbx` (`-Dgl_bridge=true`) | `gpu:bridge`, `gpu:machine`, `gpu:picture-after-load`, `gpu:rerecord`, `gpu:play`, `gpu:enhancements` |

The arcade boards' checks need no PlayStation BIOS and run whether or not
there is one (`./waterbox/run-gate.sh -a` runs them alone):

| With | Named by | Checks |
| --- | --- | --- |
| nothing | - | `arcadedb:entry`, `arcade:noset` |
| a Namco System 11 set | `tests/roms/arcade/sets/tekkenac.zip` (Tekken, Asia, TE4/VER.C) or `CHIMERA_ARCADE_NS11_SET`, and `tests/roms/arcade/namcoc76.zip` or `CHIMERA_ARCADE_NS11_BIOS` | `ns11:compare`, `ns11:rerecord`, `ns11:rewind`, `ns11:memory`, `ns11:refused`; with Chimera `ns11:engine`; with a guest Mesa `ns11:gl`; with a GL context `ns11:gpu` |
| a Konami GQ set | `tests/roms/arcade/sets/cryptklr.zip` (Crypt Killer) or `CHIMERA_ARCADE_GQ_SET`, and its hard disk `tests/roms/arcade/420uaa04.chd` or `CHIMERA_ARCADE_GQ_DISK` | `gq:compare`, `gq:rewind` |

The press scripts are written for those two sets. A set must be complete
(non-merged) and under the names ArcadeDuck's record has, which are a current
MAME release's; `waterbox/tests/make-arcade-set.py` rebuilds one from
archives that hold the same roms under older names, by CRC, and invents
nothing.

All 48 take about eleven minutes on a 20-thread desktop. The GPU checks draw
on whatever this machine's EGL hands out and name it; on a machine with no
card that is llvmpipe, and what they prove is the bridge, not a card.
`docs/PLAN.md` ("The gate") has what each check compares and how each was
seen to fail.

The content is named, never "the first file in the folder". A BIOS and the
games are yours to provide; `tests/roms/` is ignored by git wholesale.

### The frontend gate

```sh
./waterbox/tests/run-frontend.sh --chimera-root "$chimera"
```

The package through the real frontend, headless under Mono and Xvfb:
`menu:frontend`, `settings:frontend`, `disc:frontend`, `play:frontend`,
`arcade:frontend`, `keybinds`. It needs Chimera built (below), the package
installed, `run-native`, and the BIOS; `disc:frontend` needs the disc,
`play:frontend` the 3D game and `arcade:frontend` the System 11 set - the
two whose movies press anything. Without a BIOS all six say SKIP. Its scratch
is `waterbox/tests/work/`.

The same projects run on Windows by hand: a Windows bundle
(`tools/build-bundle.sh --platform windows` in the Chimera checkout), the
package in its `Cores` folder, and `Chimera.exe --headless --config=<a
scratch config> --project=<project> --firmware=bios=<file>
--lua=frontend-ram.lua` with `MINIHAWK_JOB` naming the job file, compared
with the Linux reference. `docs/PLAN.md` ("The frontend gate") has what that
found.

### Chimera's contract tests

They need no content. With Chimera built:

```sh
cd "$chimera"
CHIMERA_CORES_DIR="$chimera/build/Cores" dotnet test \
  source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj -c Release \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

### Building Chimera for them

```sh
cd "$chimera"
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux && meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release
```

### What CI proves

No BIOS, no rom set and no game may be on a public runner, so CI runs the six
checks that need nothing (four of the console's, two of the boards'), the
contract tests, and the frontend gate with every check skipped. That proves
the build of both emulators from a fresh clone, the sandbox starting the
guest, each machine's refusal with nothing to run, and the package. It
proves nothing about the console or the boards. Run the gates with your own content before a commit that touches
the machine, and say in the commit what ran.

## Files the core needs at run time

- **A BIOS**, always: one of the 20 releases `waterbox.config` lists by
  SHA-1, chosen by the `bios` setting (default `v4.1-a`, the SCPH-7001's).
  Chimera's project wizard asks for the file. Outside the frontend, the two
  runners take it as a file named `bios` in the work folder.
- **A disc**, optionally: a `.cue` with the files it names, a `.chd`, a
  `.bin`, `.img` or `.iso` of one data track, a `.pbp` or an `.ecm` - or a
  program (`.exe`, `.psexe`, `.ps-exe`) in place of one. Up to eight discs go
  in the Disc slot (`waterbox/file_slots.json`). With none the console boots
  to its own menu.
- **Memory cards** are part of the machine and of every savestate; they come
  in and go out through Chimera's save data, as `memcard1.mcd` and
  `memcard2.mcd`.

An arcade board needs instead:

- **A rom set**: the game's MAME set as a `.zip` under MAME's name for it,
  complete (non-merged) and of a current MAME release, in the Rom Set slot;
  a game's hard disk or CD beside it as a `.chd`.
- **The board's firmware**, where it is a separate archive: `namcoc76.zip`
  for Namco System 11. Konami GQ's is inside the game's set.
- **Board memory**, optionally: what the board keeps with the power off, as
  the `.nv` files Export Save Data writes (`at28c16.nv`).

For the runners that is a folder with the set and the firmware archive under
their own names, `slots` (`{"romset": ["tekkenac.zip"]}`) and `settings`
(`{"machine": "namco_system11"}`).

A work folder for `run-native` and `run-wbx` holds what the frontend would
mount: `bios`, the disc files under their own names, `slots`
(`{"disc": ["game.cue"]}`) and optionally `settings` (flat JSON;
`{"verbose": true}` turns upstream's log on).

```sh
build/meson-native/run-native <folder> --frames 600 --screenshot out.tga
build/meson-native/run-wbx build/meson-guest/core.wbx <folder> --frames 600
```

## Troubleshooting

- **`apply-patches.sh` fails at configure.** A submodule holds something
  that is neither its pin nor its whole series; the script names it. `git -C
  <submodule> checkout . && git -C <submodule> clean -fd`, then configure
  again - after turning any edit made there into a patch.
- **`link-machine.sh: ... offers [...]`.** One emulator's object exports more
  than its table: a symbol that could not be made local. `-fno-gnu-unique`
  is what stops GCC making them; a new compiler flag or a new dependency
  brought one back.
- **`build:fresh` fails.** A source is newer than `core.wbx`: rebuild the
  guest. The gate will not compare yesterday's core.
- **A flag changed and nothing rebuilt.** A change to the guest sysroot or
  to a cross-file flag rebuilds no object. Delete `build/meson-guest` and
  configure again.
- **`guest Mesa not built`.** Run `setup-mesa.sh`, or build the
  software-only core with `MESA_GUEST_DIR=`.
- **The sandbox dies.** miniBox writes the guest's last words to
  `minibox-diag.log` in the current directory. A syscall number there is a
  call the sandbox does not have; `docs/PLAN.md` ("Sharp edges") lists the
  ones this port met.
- **`run-wbx` under a memory cap.** The gate runs every guest through
  `systemd-run --user --scope -p MemoryMax=8G` when it can. Do the same by
  hand: a guest that runs away should not take the machine with it.
- **The GPU checks SKIP.** `build/meson-gl/run-wbx` is missing, or EGL gave
  no context.
