# DuckStation as a Chimera core: plan and log

## What this is

A PlayStation for Chimera, built from `stenzek/duckstation` (submodule,
pinned) in miniBox's sandbox. Chimera already runs the PlayStation through
ares; this core exists for what ares does not have - a hardware renderer with
internal resolution scaling, PGXP and true colour - and was asked for in
chimera#221. The same issue asks for the arcade boards built out of the
PlayStation, by way of the ArcadeDuck fork; that is the last milestone here.

## The pin (user-decided, 2026-10-09)

Upstream is pinned at `14ab642ff3d5d489d388c8062530aa2f7c9a1372`
(2024-09-01, "Common: Add aligned_unique_ptr") and **the pin never moves**.

DuckStation was GPL-3.0 until commit `7f4e5d55db` (2024-09-01, "Misc: Update
copyright headers"), which replaced `LICENSE` with CC BY-NC-ND 4.0 and every
file header's `(GPL-3.0 OR CC-BY-NC-ND-4.0)` with `CC-BY-NC-ND-4.0`. Its
README says the rest: "You are not permitted to publish modified versions of
this repository", patches included, and "pre-configured settings and packages
are considered modifications". A core is upstream with patches, rebuilt for a
sandbox and published as a binary package - precisely what that licence
forbids. Current DuckStation cannot be a core.

The grant on what was already published stands, so the question is which
commit is the last clean one, and it is not the parent of the relicensing
commit. Measured on the tree of each commit:

| commit | date | what it is | licence headers under `src/` |
|---|---|---|---|
| `14ab642` | 2024-09-01 | Common: Add aligned_unique_ptr | 451 `(GPL-3.0 OR CC-BY-NC-ND-4.0)`, 5 `GPL-3.0`, 2 `(GPL-3.0 OR PolyForm-Strict-1.0.0)`, 2 `GPL-2.0+`, 2 `LGPL-3.0+`, 2 `Apache-2.0 WITH LLVM-exception`; none without a GPL-compatible option |
| `3fbdcdb` | 2024-09-01 | AudioStream: Rewrite and integrate expander | adds `util/audio_stream.{h,cpp}` marked `CC-BY-NC-ND-4.0` only |
| `aa955b8` | (authored 2023-12-23) | GPU/SW: Split out rasterizer | adds four `core/gpu_sw_rasterizer*` files marked `CC-BY-NC-ND-4.0` only |
| `7f4e5d55db` | 2024-09-01 | Misc: Update copyright headers | `LICENSE` becomes CC BY-NC-ND 4.0 |

So `14ab642` is the last commit whose `LICENSE` is the GPL-3.0 and whose
every source header offers it. The cost is permanent: nothing upstream wrote
after that date may be brought in, by merge or by hand, because every line of
it is No-Derivatives. A fix this core needs is a fix this repository writes.

The owner's decisions on the request (2026-10-09): go ahead on this pin; the
core keeps the DuckStation name (the author's wishes about forks and packages
are not a constraint where the licence allows the thing); and ArcadeDuck's
arcade boards are part of the request, not a separate one.

## The eight questions (docs/new-core.md), answered from the source

**Is it a new core?** Yes. ares already answers for the PS1 as a machine;
what is asked for is a different emulator's renderer and enhancements, which
no declaration on the ares core could give.

**May it be distributed, modified?** At the pin, yes: GPL-3.0. The bundled
dependencies this build compiles (fmt, fast_float, simpleini, rapidyaml,
xxhash, libchdr, lzma, Dear ImGui) carry their own permissive or public-domain
terms; zlib and zstd come from their own repositories. All of it is declared
in `waterbox/package-licenses.json`.

**One thread on one clock?** Yes. The software renderer's worker thread
(`GPU/UseThread`) and the CD read-ahead thread (`CDROM/ReadaheadSectors`) are
settings and both are off; the rest of upstream's threads belong to
screenshots, capture and the interface, none of which is compiled. The
machine's time is counted in ticks of the console's own crystal
(`TimingEvents`), so the virtual clock is one function (see "Time").

**Where does the picture come from?** All three routes exist. The software
renderer draws into an array that is the console's VRAM, which is route one;
the OpenGL renderer (`util/opengl_device.cpp`, `core/gpu_hw.cpp`) gives
routes two and three. The pin has Vulkan, Direct3D and Metal devices too and
none of them is built.

**Does it recompile?** It has two x86-64 code generators beside an
interpreter and a cached interpreter. Fastmem has a lookup-table mode and a
disabled mode besides the mmap one, so nothing has to fault; what remains is
the write protection upstream puts on RAM pages holding compiled code (see
"Sharp edges"). The first milestones interpret.

**What must the user bring?** A PlayStation BIOS. Upstream recognises the
free OpenBIOS as well (`bios.cpp`), which is what a public runner could boot.

**What can a gate run on a public runner?** Not decided yet. The BIOS is
somebody's and so is every game; OpenBIOS and a test program this repository
builds are the route to machine legs in CI.

**How big is it?** Small. 2 MB of RAM (8 MB on a development console), 1 MB
of VRAM, 512 KB of BIOS. The first sandboxed runs used a layout borrowed from
the PlayStation 2 core; the driver's resident size peaked at about 700 MB in a
plain run, which has not been broken down yet. The layout is to be measured
and declared.

## Milestones

- **M0 - a native reference that boots.** DONE 2026-10-09. A curated source
  set (`waterbox/sources.sh`), meson for both flavors, a Host implementation,
  a graphics device made of memory, a sound device that is a buffer.
- **M1 - virtual time.** DONE 2026-10-09 (patch 0003; gate leg
  `clock:virtual`).
- **M2 - the guest build.** DONE 2026-10-09: `core.wbx` links, passes
  `check-wbx.sh`, and agrees with the native reference.
- **M3 - input, lag, savestates as gate legs.** DONE 2026-10-09 for two
  ports of digital pad or DualShock. The other controllers upstream has
  (mouse, GunCon, NeGcon, Justifier, analog joystick) and the multitap are
  not wired.
- **M4 - discs and memory cards.** PARTLY: one disc, by name from a project
  or nameless when opened directly. Not yet: several discs and the swap,
  memory cards through the save-data channel (a card is in port 1 but lives
  and dies with the session), a PAL or Japanese game.
- **M5 - the package.** DONE 2026-10-09 for what exists: the four
  declarations, `build-package.sh`, Chimera's contract tests, and a project
  through `chimera-run`. The frontend gate under Mono is not written.
- **M6 - the hardware renderer.** PARTLY, 2026-10-09: it runs inside the
  sandbox on Mesa softpipe and, through the GPU bridge, on a host's OpenGL,
  at 1x to 8x, with states, rewind and an exact picture after a load. Not
  yet: a measurement on a real graphics card (everything bridged so far was
  drawn by llvmpipe), and the enhancements beyond resolution - PGXP, true
  colour, texture filtering, widescreen - as settings.
- **M7 - speed**: the code generators, if the interpreter is not enough.
- **M8 - the arcade boards** (ArcadeDuck; see below).
- **M9 - documents, CI, publishing.**

## How it is put together

**The source set.** `waterbox/sources.sh` describes what is compiled by what
is left out: achievements, the big-picture interface and the on-screen
display, the game list, the GDB and PINE servers, texture replacement, media
capture, post-processing, host input and audio devices, and every graphics
device but (later) OpenGL. Upstream's three libraries are built as three
static libraries because each is compiled against a precompiled header of its
own (`util/pch.h`, `core/pch.h`) and its sources use names that header
brings; meson force-includes the same header.

**Compiler.** Upstream builds with clang only. GCC needs `-fpermissive` for
one thing so far: `_mm_stream_load_si128` takes a non-const pointer in GCC's
headers.

**What stands in for the missing parts.** `waterbox/stubs/frontend.cpp`
answers every function of an excluded file the way a desktop DuckStation with
that feature turned off would. `waterbox/stubs/cpuinfo.h` replaces the
cpuinfo library with constants: nothing about the processor a run happened on
may reach the run. Dear ImGui is compiled (eight core files include its
header for their debug windows) but no context is ever made; the one direct
call on the present path is behind `CHIMERA_WATERBOX` (patch 0006).

**The picture.** The software renderer converts the visible rectangle of
VRAM into a texture of the graphics device. That device is
`waterbox/memory-device.cpp` (patch 0001 makes `RenderAPI::None` create it):
textures are bytes, a draw is a no-op. The frame handed to the frontend is
the console's raster - `display_width` by `display_height` from the CRTC
state, with the texture placed at `display_origin` - composed in
`cinterface.cpp`. An interlaced field is written to every other line and the
other lines keep the previous field: a weave, done on the bytes. Upstream's
deinterlacers and its 24-bit chroma smoothing are shaders and are off.

**Sound.** `waterbox/audio-stream.cpp` is upstream's `AudioStream` class
with none of upstream's source: a buffer the SPU fills in its own 64-frame
chunks and the adapter empties once a frame. No stretching, no expansion, and
`EmptyBuffer` does nothing - a stream emptied on a state load would make a
frame's sound depend on whether a state was loaded before it.

**Time (patch 0003).** Every clock read in the compiled sources goes through
`Common::Timer`. With `CHIMERA_VIRTUAL_CLOCK` its value is
`ChimeraClockNanoseconds()` - the machine's global tick counter over its
tick rate - and every sleep returns at once. Upstream's frame throttle then
finds each frame ended exactly when it was due. `-Dhost_clock=true` builds a
native reference on the host clock, for a gate leg to be seen failing
against.

**A frame.** `FrameAdvance` unpauses the system and calls `System::Execute`;
`Host::PumpMessagesOnCPUThread`, which upstream calls at the end of every
frame, pauses it, and `Execute` returns. This is upstream's own frame-step
path, unpatched.

**Memory (patch 0004).** Upstream maps one shared-memory object twice so RAM
has a protectable view and an unprotected one. With
`CHIMERA_NO_SHARED_MEMORY` RAM, BIOS and the handler tables come from the
heap, both pointers are the same block, and nothing is ever protected.

**Files (patch 0005).** A guest sees a flat list of mounted files. With
`CHIMERA_FLAT_FILESYSTEM` every path is reduced to its last component where
it becomes a file, every write through a path is refused as read-only, and a
directory listing finds nothing. The POSIX calls with no system call behind
them in the sandbox (`getcwd`, `access`, `mkdir`, `lstat`) are answered in
`waterbox/stubs/posix-stubs.cpp`, for both flavors.

**Resources.** `gamedb.yaml`, the per-title database, is assembled into the
core (`waterbox/resources.S`). The disc-hash database and the cheat database
are not the machine's and are absent.

## The hardware renderer (2026-10-09)

**An OpenGL inside the sandbox.** `waterbox/setup-mesa.sh` builds Mesa 24.0.9
(SHA-256 pinned, the release and options every GL core here uses) for the
guest: softpipe behind OSMesa, static, no LLVM. With `-Dmesa_guest_dir` the
core compiles upstream's OpenGL device and links it; patch 0008 makes
`OpenGLContext::Create` hand out `waterbox/gl-osmesa.cpp`'s context instead of
asking a window system. Upstream's device, pipelines, stream buffers and the
hardware renderer compiled and ran unchanged. The native reference has no GL:
Mesa is built for the guest only, so its legs are the sandbox against itself.

**The frame is read back.** The hardware renderer's display texture is a
rectangle of its VRAM texture, at the internal resolution. `ComposeFrame`
downloads it (upstream's `GPUDownloadTexture`), swaps RGBA to BGRA and places
it in the raster as it does the software renderer's, everything multiplied by
the scale - which it works out from the texture's view of the visible VRAM
rectangle, because the renderer does not say. Upstream's screenshot code flips
what it reads from a GL device; doing the same turned the picture upside
down, so it is not flipped. That was seen on softpipe and has to be seen
again on a real driver.

**What the renderer changes, measured on the BIOS shell, 1200 frames:**

| renderer | host time | audio, lag, `machineHash` | Main RAM |
|---|---|---|---|
| software | 4.4 s | reference | reference |
| opengl 1x | 48 s | identical | different |
| opengl 2x | 194 s | identical | different again |

The processor runs the same instructions at the same times under all three -
the renderer does not perturb the machine's clock. Its MEMORY differs,
because the shell reads its own picture back and the pixels it gets are the
renderer's (upstream's hardware renderer keeps 24-bit colour and does not
dither, by default). NHL Blades of Steel 2000 does not read back in its first
1800 frames: its RAM under opengl 2x is the software reference's, byte for
byte. So the renderer and the internal resolution are part of the machine a
movie is made on, as docs/new-core.md says a picture that feeds the machine
must be, and both are declared as settings a project pins.

**Interlace: the console's behaviour is the default, not upstream's.**
Upstream defaults `GPU/DisableInterlacing` to true: in an interlaced mode it
draws every line every frame, where a PlayStation draws only the field it is
not showing. That is a steadier picture and different video memory. Here the
setting is `interlacing`, `weave` by default (the console; the frame handed
over is the field woven with the one before) with `progressive` as the
enhancement. This is a decision made while porting, not one the owner was
asked: it follows the rule that a default is the machine and an enhancement is
a choice, and it is the kind of thing a first user may want reversed.

The woven frame is kept from one frame to the next, so it is in the state.
The first version kept it in memory no state carries and no leg noticed,
because under upstream's default nothing was ever woven; with interlace on,
the same mistake put back on purpose fails `disc:rewind`.

**States.** Everything softpipe holds is the guest's memory, so a state
carries the renderer whole: `gl:rerecord` and `gl:rewind` compare the picture
too and pass.

## The GPU bridge (2026-10-09)

`opengl-hw` is the same renderer with its GL calls leaving the sandbox
(chimera docs/gpu-bridge.md). What this core had to do:

- **The guest half** is generated from miniBox's shared table for the 122
  entry points this build names (`waterbox/gl-entry-points.txt`: every `glad_gl*`
  symbol upstream's objects reference that the table has, plus `glGetStringi`,
  which only glad itself calls and which a scan of upstream's objects missed).
  Four OpenGL ES names upstream references are not in the table and are never
  called on desktop GL. `waterbox/gl-bridged.cpp` is the context object.
- **No mapped buffers** (patch 0009): the stream buffers take upstream's
  `glBufferData` path and pixel buffer objects are off when bridged, because
  a mapping is a pointer into the driver.
- **`glGetString` had one buffer.** miniBox's generated wrapper copied every
  answer into the same static buffer; upstream asks for vendor, renderer and
  version, reads all three afterwards and picks driver workarounds from them,
  so the first bridged run logged all three as "4.50". `gl-bridged.cpp` keeps
  a copy per name. Reported to the main session, which found the same in
  Dolphin and Flycast and fixed the generator (miniBox bd8daaf); the copy here
  is then redundant and harmless, and stays until this core is built against
  that miniBox.
- **A load is a new context** (patch 0010, `CheckGLContext`). The context id
  is compared at the top of every frame; when it has moved, or the engine said
  a state was loaded and this core had not yet looked (chimera issue 126),
  upstream's own "recreate the GPU" runs: every object of the old context is
  let go of, a device and a renderer are made, and the renderer is restored
  from guest memory. Upstream's version of that begins by reading VRAM back
  from the card, which after a load is another machine's VRAM; a flag makes it
  skip the read and trust the copy in guest memory.
- **VRAM lives on the card, so a state has to be given it.** Under the
  hardware renderer the console's video memory is one texture, at the
  internal resolution and in 24-bit colour, and it is the machine's.
  `StateSaving` fetches two things before every state taken while a bridged
  context is live: upstream's 1024x512 15-bit copy (what the machine can read
  back, and what upstream's restore uploads), and the texture itself, into a
  block of this core's own memory allocated during Init. Rows are copied into
  that block only when they differ, so untouched VRAM costs a state's delta
  nothing; the buffer the card writes into is in no state. After the rebuild
  the texture is uploaded over upstream's 15-bit restore.

Measured on llvmpipe, the BIOS menu, a state kept at frame 1100 and restored
at 1130:

| what a state carried | frames after the load that differ from a straight run |
|---|---|
| upstream's 15-bit copy only | 2 of the first 2, 40% of their pixels by up to 4 levels of 255; exact from the third |
| the texture, 1x | none |
| the texture, 3x | none |
| nothing (the core left untold) | the picture differs; the processor, sound and polls do not |

The two wrong frames were the 24-bit picture coming back as 15-bit and taking
both interlaced fields to be redrawn. Through the engine's own host half
(`chimera-run --gpu`, llvmpipe) the game at 4x ran 1800 frames in 14 s and
ended on the native software reference's Main RAM, with a 2560x896 frame.

What a state does NOT carry: the PGXP depth buffer (not offered as a setting
yet; cleared after a load, as upstream does on a settings change).

NOT MEASURED: any of this on a graphics card. llvmpipe proves the bridge and
the rebuild run; it is not a driver.

## The package

`waterbox/build-package.sh -r <chimera>` builds `duckstation.chimeraCore`:
2.9 MB without the hardware renderer, 33 MB with it, nearly all of the
difference Mesa (the PlayStation 2 and Dreamcast cores are the same size for
the same reason). It needs `waterbox/setup-mesa.sh` run first.

- **System id `PS1`**, the one the ares core uses for the same console.
  `.cue`, `.chd`, `.pbp`, `.ecm` and the executable extensions are claimed;
  `.bin`, `.iso` and `.img` are not, because they are a dozen machines' disc
  images.
- **The BIOS is one firmware id, `bios`, and a setting says which dump.**
  Twenty retail ROMs are declared, v1.0 (Japan) to v4.5 (Europe), each with
  the SHA-1 measured from a dump and matched by MD5 against upstream's own
  table (`bios.cpp`). Default `v4.1-a`, the dump the ares core asks for, so a
  user who has resolved one has resolved the other.
- **Region** is its own setting (`auto` reads the disc), because the BIOS
  and the console's market are two things: upstream warns about a mismatch
  and runs.
- **The wire** is per port the DualShock's seventeen buttons and four stick
  bytes. A stick axis is declared 0..255 with 128 at rest and is the byte the
  pad reports: upstream takes half axes as floats and merges them, so the
  adapter inverts that (`SetStickByte`) and every byte is reachable. Upstream's
  1.33 stick sensitivity, its dead zone and its "left stick is the d-pad in
  digital mode" convenience are all off. The Analog button is told to
  upstream once per press, because upstream toggles on every telling.
- **A file with no name.** A disc opened directly is mounted as `disc` with
  no extension, and upstream decides what an image is by extension. The
  adapter reads the first bytes (CHD, PBP, ECM, PS-X EXE, raw 2352-byte
  sectors) and gives the file a second name through an alias in the flat file
  system (patch 0005). A cue sheet opened alone is refused with a message
  that says to make a project.
- **The field rate** is reported exactly: the video crystal over ticks per
  line times lines, 53693175/897619 (59.826 Hz) on an NTSC console.

Not verified: the default bindings with a physical pad (the stick axes in
particular - a byte that grows downwards against a host axis that grows
upwards), and nothing at all on Windows.

## The gate

`waterbox/run-gate.sh`, in tiers: with nothing, with a BIOS, with a disc,
with a Chimera checkout, with a GL context for the bridge. 26 legs, four
minutes here, all of them passing on 2026-10-09 with the SCPH-7001 BIOS and
NHL Blades of Steel 2000 (USA). Five are the hardware renderer's on softpipe,
sandbox against itself, and four are the bridge's, drawn by llvmpipe.

`gpu:picture-after-load` carries its own control: the same rewind is run with
`CHIMERA_NO_STATE_SAVING=1`, which leaves the core untold as it does in the
engine, and the leg fails unless that picture DIFFERS while the machine does
not.

The digests compared are video and audio hashed every frame, lag frames, the
memory domains hashed once at the end, and `machineHash` - the machine's tick
counter, program counter and registers folded in every frame, because a
difference in memory that heals before the end is otherwise invisible. That
is not a guess: the first sandbox-only sabotage flipped a byte of RAM at
frame 100 and the disc leg passed, because the game later loaded over it.

Every leg has been seen to fail (docs/gates.md, B), 2026-10-09:

| broken on purpose | legs that went red |
|---|---|
| the database resource not found | `gamedb:entry` |
| `SetButton` dropping the press | `input:reaches` ("idle and pressed are the SAME run") |
| the poll hook doing nothing | `lag:counted` (1300 of 1300) |
| no alias for the nameless file | `disc:nameless` |
| the cue message reworded | `cue:alone` |
| the missing-BIOS check removed | `nobios:refuses` |
| the sandbox writing one byte of RAM a frame that the reference does not | `bios:compare`, `disc:compare`, `disc:nameless`, `input:reaches` |
| a counter the machine reads kept in memory no state carries | `bios:rewind`, `disc:rewind` - and NOT the two rerecord legs, which save and load with nothing in between and cannot see it |
| `StateLoaded` writing to RAM | `bios:rerecord`, `disc:rerecord`, both rewind legs |
| the wall clock written into the reference's RAM | `bios:deterministic`, both compare legs |
| the reference built with `-Dhost_clock=true` | `clock:virtual`, alone, and the gate took 4m24s instead of 80s |
| the renderer setting ignored | `gl:softpipe`, `gl:scale` |
| eight samples of sound dropped a frame under the GL renderer | `gl:machine`, `gpu:machine` |
| the woven frame kept in memory no state carries | `disc:rewind` (and nothing at all while interlace was off by default) |
| the card's VRAM fetched and never given back | `gpu:picture-after-load`, `gpu:rerecord` |
| the bridge offered and ignored | `gpu:bridge` (softpipe drew), and `gpu:picture-after-load` through its control ("the untold picture is the SAME") |
| a binding for a button that is not declared | `keybinds` |
| a source newer than `core.wbx` | `build:fresh` |
| a project with `fast_boot` against the reference without | the `engine:project` comparison, by hand; in the gate that leg was seen red through its stale-package arm |

Chimera's contract tests pass against the package alone (12 of 12) and fail
with a binding for an undeclared button added to a copy of it (11 of 12,
naming `duckstation/PlayStation Controllers: P1 Turbo Nonsense`).

What the gate does not stand in for (docs/gates.md, E): one game, booted to
its publisher logos with a pad exercised at random. No menu was navigated, no
3D scene was drawn, no save was written. An NTSC-U disc on an NTSC-U BIOS is
the only combination tried.

## What was measured (2026-10-09)

BIOS: SCPH-7001 v4.1 (sha1 `14df4f6c1e367ce097c11deae21566b4fe5647a9`).
Game: NHL Blades of Steel 2000 (USA), SLUS-00825, one MODE2/2352 track.
Interpreter, software renderer.

| run | frames | result |
|---|---|---|
| BIOS shell, native, twice | 1200 | identical digests; 2.9 s of host time |
| BIOS shell, native against sandbox | 1200 | video, audio, Main RAM identical |
| game, native against sandbox, pad exercised | 2400 | video, audio, Main RAM identical; 4.5 s native, 6.2 s sandboxed |
| game, sandbox, a state saved and loaded around every frame | 900 | identical to the straight run |
| game from a project (cue and bin) through `chimera-run` | 1800 | Main RAM byte-identical to the native reference; 2.6 s |
| game opened directly (the bin alone) through `chimera-run` | 1800 | the same RAM |

The interpreter runs this game's boot at about six times real time on this
machine, sandboxed, so speed is not what the code generators are needed for
yet. A 3D scene has not been measured.

## ArcadeDuck (chimera#221, second half)

`StillJC/ArcadeDuck` (GPL-3.0, first published 2026-09-26, six commits, no
upstream history) adds PlayStation-derived arcade boards: Sony ZN-1/ZN-2 and
the boards built on them, Namco System 11, Konami GV and GQ. Measured against
the pin:

- Its base is the pin itself: compared with each of the 260 commits up to
  `14ab642`, its tree differs least from `14ab642`, and it has that commit's
  change (`unique_aligned_ptr` in `common/align.h`). It has none of the files
  the two commits after the pin added.
- It is not a superset of DuckStation. It deleted the console's controllers,
  memory cards, multitap and pad code (30 files), and says of itself that it
  is not meant to replace DuckStation for retail games.
- It adds 121 files (`src/core/arcade/`, an M37710 and a 68000 core, sound
  chips) and changes the machine's own files: `bus.cpp` by about 800 lines,
  `gpu.cpp` 840, `system.cpp` 1440, `cpu_pgxp.cpp` 355, `gpu_sw_backend.cpp`
  290, `spu.cpp` 170, `gte.cpp` 155.

So the arcade boards cannot be had by pinning ArcadeDuck instead. They go in
the way the PlayStation 2 core carries NAMCO's boards: ArcadeDuck's arcade
sources beside upstream, and the changes it made to the machine's files
re-expressed as one patch of hook sites behind a define, with the console
untouched when the define is off. Not started; the licences of the chip
emulations it bundles (several come from MAME, whose files are licensed one
by one) have to be read first.

## Sharp edges

- **Write protection on code pages.** `Bus::SetRAMCodePage` protects a RAM
  page a block was compiled from and relies on the fault to invalidate it.
  Patch 0004 removes the protection. The interpreter compiles nothing, so it
  is unaffected; the cached interpreter and both code generators are NOT yet
  safe to turn on, and M7 has to show every writer checks `g_ram_code_bits`.
- **`g_scm_*` and `__DATE__`.** Upstream stamps its build from git and from
  the compiler's clock. Both are constants here, or two builds of one commit
  would be two packages.
- **This machine has two Chimera checkouts.** `~/chimera` is the main
  session's; this core builds and tests against `~/chimera-newcores`
  (`-r`, `CHIMERA_ROOT`, `-m`). The scripts find a sibling checkout by
  themselves, and here the sibling is the wrong one: say which.
- **`chimera-run --frames` does nothing without `--record`.** A bare run
  with an empty movie plays zero frames and dumps the machine as it booted;
  a "divergence" was chased that was frame 0 compared with frame 1800.
- **The repository's own files are MIT, with two exceptions.**
  `waterbox/host.cpp` and `waterbox/audio-stream.cpp` are made from upstream's
  source and say GPL-3.0 at the top. `LICENSE` has the note.
