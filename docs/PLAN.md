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

**What can a gate run on a public runner?** Today, nothing of the console:
the BIOS is somebody's and so is every game, so CI proves the build, the
package and the refusal without a BIOS (see "On a runner"). OpenBIOS and a
test program this repository builds are the route to machine legs in CI, and
have not been tried.

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
- **M3 - input, lag, savestates as gate legs.** DONE 2026-10-09: two ports
  and the six more a multitap in each gives, every controller upstream has
  in any of them, and the console's Reset button (see "The other
  controllers, the multitap and the widescreen hack" for what each was
  proved on, which for the guns is not a game).
- **M4 - discs and memory cards.** DONE 2026-10-09: up to eight discs with a
  selector and a lid, memory cards in the machine's memory with import and
  export. Not tried: a game that asks for its second disc, a save a game
  wrote, a PAL game. A Japanese console with a Japanese game and a European
  console at its menu have been (see "Other consoles").
- **M5 - the package and the frontend gate.** DONE 2026-10-09: the four
  declarations, `build-package.sh`, Chimera's contract tests, a project
  through `chimera-run`, and the frontend gate under Mono and, by hand, on
  Windows (see "The frontend gate").
- **M6 - the hardware renderer.** DONE 2026-10-09: it runs inside the
  sandbox on Mesa softpipe and, through the GPU bridge, on a real card (a
  GTX 1060), at 1x to 8x, with states, rewind and an exact picture after a
  load, and with true colour, texture filtering, downsampling, PGXP, crop
  and the widescreen hack as settings.
- **M7 - speed.** NOT NEEDED so far: the interpreter runs a 3D race at eight
  times real time in the sandbox (see "A game played"). The code generators
  stay unbuilt, and with them the question of write protection.
- **M8 - the arcade boards.** DONE 2026-10-09 for two of them: ArcadeDuck
  compiled whole beside the console in one `core.wbx`, with Namco System 11
  and Konami GQ each proved on a game (see "ArcadeDuck"). The Sony ZN boards
  and Konami GV are compiled in and not offered: no complete set of theirs
  has been booted.
- **M9 - documents, CI, publishing.** The documents and the workflow are
  written (2026-10-09) and the workflow's steps were run here from a fresh
  clone before it ever ran on GitHub (see "On a runner"). The owner asked
  for the repository to be published the same day ("wrap up the
  publication").

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
  so the first bridged run logged all three as "4.50". Reported to the main
  session, which found the same in Dolphin and Flycast and fixed the
  generator (miniBox bd8daaf: a place per name). This core carried a copy per
  name of its own for a day; it was dropped once the core was built against
  that miniBox, so a build against an older one would log three identical
  strings again - `gpu:bridge` names the driver it found, which is where that
  would show.
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

### On a graphics card (2026-10-09)

GeForce GTX 1060 6GB, NVIDIA 581.42, Windows, `chimera-run.exe --gpu` (engine
8d4b1f5), NHL Blades of Steel 2000, core 2fecb5a. The package is the one built
on Linux; nothing is built for Windows.

| run | result |
|---|---|
| software, 1800 frames | 3 s; Main RAM is the Linux native reference's |
| opengl-hw 1x, 1800 frames | 4 s; the same RAM |
| opengl-hw 4x | 13 s; the same RAM; frame 2560x896, the right way up |
| opengl-hw 8x | 49 s (37 frames a second, with every frame read back at 5120x1792); the same RAM |
| `--settle-probe 1700,12` at 4x, a state every 100 frames | all 12 frames drawn after the load: 0.00% of pixels differ |
| the same with `CHIMERA_NO_STATE_SAVING=1` | all 12 frames: 96.49% of pixels differ |
| `--settle-probe-unseen` with `--draw-every-frame`, 4x | told: exact; untold: 96.49% |
| `--settle-probe-state`, 4x | exact |
| `--settle-probe` at 1x | exact |

So the readback is not to be flipped on a real driver either, the driver's
strings are its own ("GL_VENDOR: NVIDIA Corporation"), and what `StateSaving`
carries is what makes the picture after a load: the scene probed is a 2D logo
the game leaves in VRAM and does not redraw, which is the hardest case for a
renderer that lost its VRAM and the reason the untold control is wrong for as
long as it was watched.

The same card, later the same day, on the race (core 254e418; Need for
Speed: Porsche Unleashed from a project whose movie carries the presses,
6500 frames, the last 1800 of them driving):

| run | result |
|---|---|
| software | 11 s |
| opengl-hw 4x, nothing turned on | 70 s with the project's states, 54 s with none taken; Main RAM is the software run's; 1280x896 |
| 4x with true colour, bilinear filtering and PGXP | 62 s; RAM differs from software's, as PGXP was measured to make it |
| 8x with true colour, JINC2, PGXP and its depth buffer | 209 s, 31 frames a second; 2560x1792 |
| `--settle-probe 6400,12`, 4x with true colour, bilinear and PGXP, a state every 100 frames, the scene moving | all 12 frames drawn after the load: 0.00% of pixels differ |
| the same with `CHIMERA_NO_STATE_SAVING=1` | 97.60% of pixels wrong in the first frame, 87.77% in the fourth, wrong in all 12 |
| the same probe with the PGXP depth buffer on | exact |

So the carried VRAM is what makes a 3D scene's picture after a load as well,
enhancements on. Speed: at 4x the race runs at twice real time on this card
and at 8x at half of it; the frame is read back and handed over every frame,
and at 8x that is 18 MB a frame. The software renderer is five times faster
than the hardware one at 4x here, because the processor is the cheap part.

What that does NOT show: a game that reads its picture back, a state made in
one process and loaded in another on the card, or any other card.

## Discs, the lid and the cards (2026-10-09)

**Several discs.** The project's Disc slot takes up to eight, and their order
is the selector's. The controls are the ones the Opera and Flycast cores
have: `Previous Disc` and `Next Disc` move a selector and touch nothing;
`Disc Swap` is the lid, up for as long as it is held (upstream's
`System::RemoveMedia`: the drive reports its shell open and raises the
interrupt a game waiting for a disc watches for) and closing on the selected
disc (`System::InsertMedia`). With the selector untouched it closes on the
next one, so the button alone steps through a set. Which disc is in, which is
selected and whether the lid is up are in the state. The drive exports say
which is which, and its light is lit on a frame a sector was read (patch
0011, one hook in `CDROM::DoSectorRead`).

Measured with the two games as one project's two discs: Disc Swap held from
frame 1300 for 120 frames, and upstream's log says the CD was removed at
21.73 s and "Inserted media from Need for Speed 5 ... (SLUS-01104)" at
23.74 s; native and sandbox agree, and a state kept at 1250 and restored at
1600 replays the swap. They are not a set, so nothing asked for the second
disc: this proves the drive, not a game's disc change.

**Memory cards** are upstream's non-persistent kind: 128 KB of the machine's
memory, in every state. `memcard1` is on by default and `memcard2` off. The
save-data exports hand each present card over as `memcard1.mcd` /
`memcard2.mcd`; a project's Save Data slot brings one in under the same name,
copied into the card after boot and before the guest is sealed. Three things
are refused rather than ignored: a file that is not a 131072-byte card image,
a card under any other name, and a card for a slot that is switched off.
Measured: a formatted card exports identically from both flavors and from
the engine; a card with sixteen bytes marked in block 5 goes in and comes
back out byte for byte.

**Reset** is the button on the case (`System::ResetSystem`), on the frame it
is first held.

**A movie's row has only the controls the machine has.** The engine asks the
core which declared wires are live (`IsButtonActive`, `IsAxisActive`) and a
movie has columns for those alone: two console columns with one disc and four
with several, fourteen buttons and no sticks for a digital pad, nothing for an
empty port. A test project written with every declared column stopped
parsing the day the console's buttons were declared ("unparseable movie entry
at frame 0"). `tests/make-project.py` now takes the row from the engine: a
project of no frames is asked to record one.

## Other consoles (2026-10-09)

Everything before this had run on one ROM, the SCPH-7001's (v4.1 A). Two
more were tried by hand, and the second found a defect.

**A Japanese console with a Japanese game.** BIOS v4.0 J (SCPH-7000) and
Ganbare Goemon - Ooedo Daikaiten (Japan, SLPM-86774): the disc makes the
console NTSC-J, the game plays its opening - character cards, then the
animated titles - and 3000 frames are identical native and sandboxed in
video, audio, lag, the machine every frame and memory. The same disc on the
American ROM also boots (upstream warns that the ROM's region is not the
console's and carries on); that is upstream's behaviour and was left alone.

**A European console at its menu - and the region of an empty tray.** BIOS
v4.1 E (SCPH-7002) with nothing in the drive booted as a NORTH AMERICAN
console: "auto" reads the region off the disc, and with no disc upstream
defaults to NTSC-U. Upstream can afford to, because it then picks a ROM to
suit; here the project picks the ROM. The European ROM put the video chip in
PAL mode (a 640x536 picture) on a console clocked for NTSC, and the frame
rate declared to the frontend was 59.83 while the machine kept neither that
nor PAL's. Fixed in the adapter: with "auto" and an empty tray the console's
region is the ROM's (`BIOS::LoadImageFromFile`, the image's own record), so
that console is PAL at 53203425/1069484, 49.75 fields a second. With a disc
nothing changed: the disc decides. Gate leg `region:rom` (a second,
European BIOS; SKIP without one), seen to fail with the rule switched off:
"European BIOS: field rate [53693175/897619]".

Still not tried: a PAL game (none is on this machine), and what a game that
changes video standard while running does to a frame rate the frontend read
once at boot.

## A game played (2026-10-09)

Need for Speed: Porsche Unleashed (USA), SLUS-01104: one image of a data
track and audio tracks, with films (MDEC), 2D menus and a 3D race. A script of
presses - Start to skip the films, Cross every 150 frames through the
quick-race menus on their defaults, Cross held from frame 5500 as the
accelerator - takes it from boot to half a minute of driving France, Riviera
in 7200 frames. `fast_boot` on.

| run | result |
|---|---|
| native against sandbox, 7200 frames | video, audio, lag, the per-frame machine digest and Main RAM identical; 11.4 s native, 14.4 s sandboxed for two minutes of machine time |
| the same with nothing pressed | a different run (the films play on), so the presses reached the game |
| sandbox, kept at frame 4000 in the menus and restored at 6800 in the race | the straight run |
| opengl-hw 1x and 4x, nothing turned on (llvmpipe) | sound, lag, machine digest and Main RAM are the software renderer's |
| + true colour | picture differs; machine and RAM the same |
| + bilinear texture filtering | picture differs; machine and RAM the same |
| + PGXP | picture differs, and so does the machine: 702 lag frames against 718, another digest, other RAM |

**PGXP is not only a picture setting.** With it on, which polygons are
culled is decided with the precise coordinates, and the time the emulated GPU
is busy is charged per polygon drawn - so the game sees a GPU that finishes at
other moments and runs differently. That is upstream's behaviour, not this
port's, and it is why every enhancement here is a setting a project pins and
a movie cites rather than a preference: the one that looked most like a pure
picture option is the one that moves the machine.

**The defaults are the console.** Upstream's hardware renderer draws in
24-bit colour unless told not to; here `trueColor` is off, like every other
enhancement, and what `opengl` or `opengl-hw` draws by default is what the
console drew at a higher resolution. Same rule as interlace, and as much the
porter's decision.

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
  bytes (as first published; eight sockets of every controller since - see
  "The other controllers, the multitap and the widescreen hack"). A stick axis is declared 0..255 with 128 at rest and is the byte the
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
with a Chimera checkout, with a GL context for the bridge, with the 3D game -
and then the arcade boards, which need no PlayStation BIOS and are run
whether or not there is one (`-a` runs them alone). 57 legs, sixteen minutes
here, all of them passing on 2026-10-09: 42 are the console's, with the
SCPH-7001 BIOS, NHL Blades of Steel 2000 (USA) and Need for Speed: Porsche
Unleashed (USA) - five of them the hardware renderer's on softpipe, sandbox
against itself, and six the bridge's, drawn by llvmpipe - and 15 are the
arcade boards', with Tekken (`tekkenac`) and Crypt Killer (`cryptklr`).
Eight of the 57 came with the other controllers and the wider panel, and
have their own account further down.

The arcade legs passed the first time they were run, all of them, which is
the best reason there is to break each one. Four builds did it:

| broken on purpose, in the arcade machine | legs that went red |
| --- | --- |
| the panel's presses dropped | `ns11:compare` and `gq:compare` ("played and idle are the SAME run") |
| a bit of RAM changed at frame 1000 in the sandbox only | the same two ("flavors DIFFER") |
| nothing exported as save data | `ns11:memory` (export), `ns11:engine` ("EEPROM DIFFERS") |
| any board takes any set | `ns11:refused` |
| the renderer setting ignored | `ns11:gl` ("pictures the SAME"), `ns11:gpu` |
| a count the machine reads kept in memory no state carries | `ns11:rewind`, `gq:rewind`, `ns11:gl` ("rewind DIFFERS") |
| a brought EEPROM checked and not copied in | `ns11:memory` ("what went in did NOT come back") |
| the card's VRAM not fetched before a state | `ns11:gpu` ("told and straight DIFFER") |
| a load that adds one to a byte of RAM | `ns11:rerecord`, with every rewind leg |
| the record of the boards left out of the core | `arcadedb:entry`, `arcade:noset`, and everything after them |
| the monitor's hidden lines handed over all the same | `ns11:crop` ("default [512x240], want 512x228") |

The ninth row was first tried as a load that FLIPS a bit, and
`ns11:rerecord` stayed green: five hundred loads are five hundred flips, and
an even number of flips is none. The same trap had been met once already in
this gate, on the console (the table below). A control that can cancel
itself is not a control.

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
| presses dropped, on the 3D game | `play:compare` ("pressed and idle are the SAME run"), and `gpu:enhancements`, because films are all that is then drawn and neither filtering nor PGXP changes a film |
| `StateLoaded` writing to RAM, again | `play:rewind`, with every other rerecord and rewind leg |
| sound dropped under the GL renderers, again | `gpu:play` |
| the true-colour setting ignored, alone | its picture is the plain one while bilinear's is not (by hand, the leg's own comparison) |
| the Reset button not connected | `console:reset` |
| no card exported | `cards:export`, and `engine:project` on the card it compares |
| a brought card not read, alone | the card that went in does not come back, and a file that is not a card is no longer refused (by hand, the two legs' own comparisons) |
| the lid never moving | `disc:swap` ("second disc inserted: 0; swapped and idle are the SAME run") |
| a binding for a button that is not declared | `keybinds` |
| an empty tray a North American console whatever the ROM | `region:rom` ("European BIOS: field rate [53693175/897619]") |
| a source newer than `core.wbx` | `build:fresh` |
| a project with `fast_boot` against the reference without | the `engine:project` comparison, by hand; in the gate that leg was seen red through its stale-package arm |

## The frontend gate

`waterbox/tests/run-frontend.sh --chimera-root <chimera>`: the installed
package through the real frontend, headless under Mono, from projects. Six
legs, a minute and a half, passing on 2026-10-09: the console with an empty
tray for 600 frames, a PAL console (a setting that has to arrive, and has to
make another machine), the game from a cue sheet and its image for 1500
frames, a movie that presses its way into a race for 6000, the OTHER machine
in the package - Tekken on Namco System 11, coins and a fight in its movie,
on the arcade panel's own wire, for 2400 - each ending on the first megabyte
of the native reference's Main RAM - and the package's bindings
becoming the frontend's. Seen red: a binding changed in the repository after
the package was built (`keybinds` names it). The others carry their
controls: the PAL machine must differ from the NTSC one, the disc run from
the empty console, the played game from the same game left alone.

`arcade:frontend` was red twice before it was green, both times for a real
reason: the board's firmware reached `chimera-run` as one mangled argument
(a bash array cannot be set in front of a command), and then the frontend
refused the project outright - "the project's firmware is not on hand" -
because a MAME archive is firmware with no declared hash and the test
project pinned an empty one. A project pins the hash of the archive it was
made with, as the wizard does (`make-project.py --firmware=<id>=<file>`).

**`play:frontend` is the only leg that presses anything**, and was added
when the scenario was taken to Windows: the first four hold nothing down, so
none of them says a row of a movie reaches the pad, or reaches it on the
frame it names. It is the core gate's played game (Start past the films,
Cross through the menus, Cross held on the grid) written as a movie's rows
by `make-project.py --press=<frame>:<count>:<button>`, against a reference
given the same presses by wire index. Seen red twice: with no presses in the
movie (the leg says the frontend's RAM is the idle game's), and with every
menu press one frame late (RAM differs). A first attempt at the second
control moved only the held accelerator by a frame and stayed GREEN - the
race polls the pad too slowly to tell - so what the leg can see was
measured on the reference first: of the presses in the script, a one-frame
shift of the menu press at 2550 changes memory at frame 3000 and a shift of
the hold at 5500 does not.

**On Windows (2026-10-09).** A Windows bundle of Chimera 3b5ed5b, this
package, the five projects the Linux run had just made and copies of the
BIOS and the games, in a folder of its own under `%TEMP%`, run by
`Chimera.exe --headless` with a scratch config and data folder. All five
agree with Linux: the three idle machines and the played movie end on the
Linux native reference's first megabyte byte for byte, the PAL console and
the disc differ from the empty NTSC one, the bindings are adopted. The
600-frame runs take four seconds, the 6000-frame race fifteen. That is the
frontend on the software renderer; the card's results are the
`chimera-run.exe` ones above.

What Windows found was in the harness, not the core:

- **The script counted frames from wherever it started.** `frontend-ram.lua`
  advanced N times. Under Mono its first turn comes before frame 0; on
  Windows the frontend has already played two frames, so N advances asked
  for two frames past the end of the movie, where a headless run pauses for
  good: the process sat idle with its window up and no file written. The
  script now runs to a frame NUMBER, and says so if it starts past it.
- **A control leaves its files behind.** The gate's work folder is one
  folder. The last thing to write `play.chimeraProject` before the copy to
  Windows was the control with late presses, so the first Windows run of
  that leg "diverged" at frame 2600, deterministically. It cost an hour of
  bisecting before the movie itself was read. It turned into evidence all
  the same: the Windows frontend's memory was, byte for byte, the
  reference's with every press one frame late - the engine and the frontend
  put a row on exactly the frame it names. Run the real gate last, and read
  the artefact before the machine.

Two things the frontend found that the engine's runner had not:

- **An empty slot still mounts its name.** A project with nothing in its Disc
  slot arrives with a file called `disc` that has nothing in it. The adapter
  read that as "a file that is no disc image this core knows" and refused to
  boot; it is an empty tray.
- **The frontend reads a movie by its LogKey.** `chimera-run` took rows whose
  key named controls the machine did not have; the frontend's movie
  controller indexed past its array on the first frame. The key and the rows
  are now made from one list of live controls, and the engine's own recorded
  row is the check on that list.

Chimera's contract tests pass against the package alone (12 of 12) and fail
with a binding for an undeclared button added to a copy of it (11 of 12,
naming `duckstation/PlayStation Controllers: P1 Turbo Nonsense`).

What the gate does not stand in for (docs/gates.md, E): two games, both
NTSC-U on an NTSC-U BIOS, one of them played for half a minute. No game wrote
a save, no game asked for its second disc, no PAL or Japanese console was
booted, and every bridged leg was drawn by llvmpipe - the card's results are
measured by hand and written above.

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

## On a runner (2026-10-09)

`.github/workflows/chimera.yml` is one job: Chimera checked out at `main`
and built, miniBox and the guest toolchain, the guest Mesa, the native
reference, the package stamped with the commit, the core gate, Chimera's
contract tests against the package, the frontend gate, the artifact - and
then Chimera's reusable publish workflow.

**What it proves is the first tier and no more.** With no BIOS the core gate
runs `build:fresh`, `keybinds`, `nobios:refuses`, `gamedb:entry`,
`arcadedb:entry` and `arcade:noset` and says SKIP 43 times; the frontend
gate says SKIP six times and is run all the
same, so that the script is known to find the frontend, the package and the
reference. The contract tests need no content. The workflow's header says
this, and so does every skipped line.

**It has not run on GitHub.** The steps were run here instead, the way a
runner would: a fresh clone of this repository in a scratch folder, Chimera
cloned inside it at `chimera-checkout`, `HOME` pointing at an empty folder,
and GCC 13.3 first on the path - the compiler of `ubuntu-latest`, where
everything before today had been built with this machine's GCC 14.2. miniBox
and its guest toolchain, Mesa, both flavors and the package built with no
change to any script; the gate said 4 ok, 0 failed, 31 skipped, and the
tree was clean but for the patched submodule. Not covered by that run: the
apt package list (this machine already has everything), the build of Chimera
and its .NET solution, the contract tests against that exact package, and
the publish job.

**The compiler is part of the package, so the GCC 13 build was gated with
content too**: in that same fresh clone, with `build/meson-gl` added and the
BIOS and both games named by their environment variables, the core gate
said 35 ok, 0 failed, 0 skipped - every leg there was that day, and the
count the GCC 14 build had. (Its
`engine:project` check ran the GCC 13 package through the engine built in
the other checkout; its GPU checks drew on llvmpipe.)

**The pin is fetched from upstream's repository.** `extern/duckstation`
names `stenzek/duckstation` and a commit its history still holds. A
repository whose owner relicensed it may one day rewrite or remove that
history, and a core whose pin can no longer be fetched cannot be built. A
mirror of the pinned commit under the project's own account would end the
dependency; creating one is the owner's (see "What is the owner's").

## ArcadeDuck: the arcade boards (chimera#221, second half; 2026-10-09)

`StillJC/ArcadeDuck` (GPL-3.0-only, first published 2026-09-26) is the
arcade boards built out of a PlayStation: Sony ZN-1 and ZN-2 and the boards
built on them (Capcom, Taito FX-1, Tecmo TPS, Raizing, Acclaim, Video System,
Atlus, Time Warner), Namco System 11, Konami GV and GQ. The owner put it in
scope of the request ("ArcadeDuck is part of the request", 2026-10-09).

### What it is, measured against the pin

- Its base is the pin itself: compared with each of the 260 commits up to
  `14ab642`, its tree differs least from `14ab642`, and it has that commit's
  change (`unique_aligned_ptr` in `common/align.h`). It has none of the files
  the two commits after the pin added.
- It is not a superset of DuckStation. It deleted the console's controllers,
  memory cards, multitap and pad code (30 files), and says of itself that it
  is not meant to replace DuckStation for retail games.
- It adds 121 files - 57,000 lines under `src/core/arcade`, an M37710 and a
  68000 - and changes the machine's own files: `bus.cpp` by about 800 lines,
  `gpu.cpp` 840, `system.cpp` 1440, `cpu_pgxp.cpp` 355, `gpu_sw_backend.cpp`
  290, `spu.cpp` 170, `gte.cpp` 155.
- Its author builds it on Windows with MSVC only. On Linux with GCC, with
  this repository's flags, every one of its sources compiled unchanged.

### One binary, two machines

The first plan was the PlayStation 2 core's: the arcade sources beside
upstream, and ArcadeDuck's changes to the machine's files re-expressed as
hook sites behind a define. It was dropped before a line of it was written,
for what the measurements above say: four thousand changed lines in the
bus, the GPU and the system are not hook sites, they are another emulator,
and the console would have been running on a mixture of the two that neither
author ever ran.

So ArcadeDuck is compiled WHOLE, from a submodule of its own
(`extern/arcadeduck`, pinned at `1e3f7ad`, its `main`), with the same flags
and the same source shape (`waterbox/sources.sh <group> arcade`) and its own
patch series (`patches-arcade/`). The two emulators define the same names -
thousands, classes of one name and two layouts among them - so neither can
be linked against the other. Each is joined into ONE relocatable object
first (`waterbox/link-machine.sh`): its libraries, its dependencies and its
own copy of the adapter, every reference inside it settled there, and then
every symbol it defines made local but one - a table of the guest ABI
(`waterbox/machine.h`). `waterbox/dispatch.c` is the guest ABI the frontend
calls, and passes each call to the table of the machine the project's
`machine` setting names. What is left undefined in each object - the C
library, the C++ runtime, Mesa, the sandbox's own calls - is shared and
resolved in the final link.

Three things that made it work, each of which failed first:

- **`--force-group-allocation`.** Inline functions and templates travel in
  COMDAT groups, which a relocatable link keeps as groups to be merged by
  name later - with the other emulator's.
- **`-fno-gnu-unique`.** GCC gives the static variable of an inline function
  a binding of its own, one definition per process whatever anybody says,
  which `objcopy --keep-global-symbol` leaves global. Thirteen were left;
  with the flag they are ordinary weak symbols and go local with the rest.
  The link step checks that each object offers exactly its one name.
- **The adapter is compiled twice.** `cinterface.cpp` and the files beside
  it include the emulator's headers, so there is a copy for each tree; what
  differs between the machines - what is plugged in, what is booted, what is
  saved - is in `machine-console.inc` and `machine-arcade.inc`.

The console is built the same way, through the same step, so that neither
machine can lean on a name of the other's by accident. Its behaviour did not
move: the Japanese game's 3000-frame digests were the same before and after
the adapter was split, and the console's whole gate passed on the
two-machine core before a single arcade leg existed.

The cost: `core.wbx` grew from 87 MB to 98 MB, and a build compiles 550
files instead of 280.

### The patch series for the arcade tree

Eight of the console's first eleven patches applied to ArcadeDuck's tree
unchanged, because its base is the pin. The other three were ported by hand,
and three are the arcade tree's own (the console's twelfth, a gun aimed at
the picture, is not there: an arcade gun is told its aim by `ChimeraSetGun`):

- `0004` (memory without shared memory): one hunk moved, because RAM there
  is as large as the board makes it (`g_ram_size`).
- `0007` is not the console's (there is no `pad.cpp`): the panel is told
  what is held, by name, through three calls added to `arcade_input.cpp`
  (`ChimeraSetDigital`, `ChimeraSetOperator`, `ChimeraSetGun`; and since,
  `ChimeraSetDriving` and `ChimeraAddTrackball`), and a frame
  is not a lag frame when a board asked what was held (`IsDigitalPressed`,
  `GetAnalogValue`, Konami GQ's `ReadGQInput`).
- `0010` (what the card holds outlives a load): the same four hooks, with
  VRAM read back to the height the board has (`g_vram_height`: 1024 lines on
  System 11, twice the console's).
- `0012` (what a board keeps is the machine's memory): see "Board memory".
- `0013` (a CHD is found where every file is): `GetCHDImageSHA1` opened its
  file with the library's own `fopen`, by the path MAME lays a rom folder
  out with (`<set>/<disk>.chd`).

What ArcadeDuck asks a desktop for and a sandbox does not have is answered
in `waterbox/stubs/arcade.cpp`: a cabinet's lamp and coin-counter outputs
(told to a socket or a window message), crosshair and bezel pictures, a game
library - and the link cable between Tecmo's Gallop Racer cabinets, which is
TCP: every socket call says no, and the board runs alone.

### The panel

An arcade machine has a wire of its own (`waterbox.config`, `machines`):
four players of a stick, Start, a coin slot, six buttons and a gun's trigger
and reload, then Service and Test; and a gun's aim per player, 0..65535
across the picture and down it. Which of it a cabinet has is the game's:
ArcadeDuck's database names a controller type and a layout per port, the
adapter gives its input code exactly that (as its own frontend would, by the
`ArcadeControllerPortN` settings) and reports the layout's controls as the
live wires - Tekken's panel is a stick, four buttons, Start and a coin slot
for each of two players, and that is all its movies have columns for.

That was the first published panel. The wire is wider since (see "The arcade
panel", below): thirty buttons and eight axes a player, with a driving
cabinet's wheel and pedals and a trackball among them.

### Board memory

A board has memory that outlives the power switch - an EEPROM with the
operator's settings and the high scores, a flash card - and ArcadeDuck keeps
each as a file under `<data>/nvram/<set>/`, read at start and written on
change or at shutdown. A core has no disk, and what a board remembers has to
rewind with the game.

So nothing under `nvram/` is a file (patch 0012). It never exists to be
read, which makes a board start from its own blank; and a write there is a
declaration - "this array is my non-volatile memory, and this is its name".
All nine call sites in ArcadeDuck pass the board's own live array, a static
or a member of the runtime it keeps for the session, so what is read through
the pointer is what the board holds NOW (System 11 only writes its file at
shutdown: a copy taken at the write would have been the blank one for the
whole session). The adapter copies a project's saved file into the array
before the first frame (the Board memory slot) and hands the array out as
save data, under the board's own name for it with `.nv` on the end:
`at28c16.nv` for a System 11 board.

A bump of ArcadeDuck's pin has to re-read those call sites: one that passed
a temporary buffer would leave a dangling pointer behind it.

### The picture's edge

A cabinet's monitor did not show every line its board draws, and ArcadeDuck
records per game how many it hid at the top and the bottom - Tekken: eleven
and one. It takes them off the picture on its way to a window and stretches
what is left. A core hands rows over, so the adapter leaves those rows out
and the raster is that much shorter (Tekken is 512x228), under the default
crop only; `crop: none` is the whole raster, 512x240, for somebody who wants
to see what the monitor hid. The first published build did not do this and
had eleven black lines above Tekken's picture. A gun's aim is still a
fraction of the whole picture ArcadeDuck's input code knows, not of the
cropped one; for a game whose crop is a line or two that is under a line of
error, and no game with a larger one has been aimed at.

### Sound level

ArcadeDuck raises what a board's sound chip mixed by 12 dB on its way to a
sound card - a cabinet's amplifier, in effect - and holds the peaks with a
limiter. The 12 dB is kept, because a board 12 dB quieter than the emulator
it comes from would be a different-sounding machine; the limiter is a sound
card's business and is not, so a peak the gain pushes past full scale stops
there. It is done in whole numbers (10^(12/20) is 130451/32768): two C
libraries can disagree about `pow`, and none about a multiplication.

### The licences

ArcadeDuck's own files are GPL-3.0-only. What it bundles under
`src/core/arcade`: QSound HLE (ValleyBell, BSD-3-Clause), ymfm (Aaron Giles,
BSD-3-Clause), a Z80 (superzazu, MIT), an ADSP-2181 (from MAME by way of
modem-dsp-emu, BSD-3-Clause), and devices adapted from MAME - the MN10200,
ZSG-2 and TMS57002 - each marked BSD-3-Clause, the licence those MAME files
carry; the M37710 is MAME's (BSD-3-Clause) and Musashi's 68000 is MIT. All of
it may be distributed with a GPL-3.0 package, and
`waterbox/package-licenses.json` names each. Unlike the console's, this pin
MAY move: ArcadeDuck is alive and GPL. What a bump has to check is that
nothing in it was taken from DuckStation after the relicence.

### What ran, and what did not

Two boards, each on one game, both on 2026-10-09:

- **Namco System 11: Tekken** (Asia, TE4/VER.C - the set `tekkenac`). From
  power-on: the Namco logo, the attract mode, two coins (this set wants two a
  credit), Start, the first character, and the first fight with punches and
  kicks. Native and sandboxed it is identical over 2400 frames in video,
  audio, lag, the machine every frame and memory; a state saved and loaded
  around every frame changes nothing; kept at frame 650 and restored at 2200
  in the fight it replays the straight run. Its EEPROM leaves as
  `at28c16.nv` and comes back in. A project of it through Chimera's engine,
  and through the frontend under Mono, ends on the native reference's memory.
  On the hardware renderer inside the sandbox the board is the same board
  and a rewind is exact, picture included; through the GPU bridge at 2x the
  picture after a load is the straight run's, and is not when the core is
  left untold. The software renderer runs it at about 270 frames a second in
  the sandbox.
- **Konami GQ: Crypt Killer** (`cryptklr`, with its hard disk
  `420uaa04.chd`). The board's self-test (a minute of it), the attract mode,
  a credit, Start and a light gun firing through the first stage: identical
  native and sandboxed over 5600 frames, and across a rewind from before the
  coins into the game. (As first published this leg put in ONE coin, and the
  board's own setting is two a credit: Start did nothing, the run differed
  from the attract mode's by half a credit on the screen, and the leg said
  a game had been played. Found when a third player would not join; see
  "The arcade panel".)

Not tried, and not to be said to work:

- **Every other board.** Sony ZN-1 and ZN-2 and all that is built on them,
  and Konami GV: compiled in, never booted (see below).
- **A real graphics card.** The bridged leg drew on llvmpipe. The console's
  results on the GTX 1060 say nothing about the arcade tree's GPU, which is
  ArcadeDuck's changed one.
- **Windows**, beyond the package being the same file.
- **Above 4x on a System 11 board.** Its VRAM is 1024 lines, twice the
  console's, and at 8x the buffer the card writes into is 256 MB where the
  memory layout has 192 MB of room for it. Not measured.
- **A game that reads a wheel, a trackball or sensors.** The wire is there
  (see "The arcade panel") and no set that reads it has run here.

### What the owner's sets could and could not do

The sets put in the arcade folder on 2026-10-09 are from an older MAME
release: the right bytes under older names, split rather than non-merged.
`waterbox/tests/make-arcade-set.py` rebuilds a set under the names
ArcadeDuck's database has, by CRC, from archives that hold the same roms,
and invents nothing. Two came out whole - `tekkenac` (Tekken, Asia,
TE4/VER.C) from `tekken.zip`, and `cryptklr` with its hard disk - and those
are the two boards offered.

The five sets for the ZN boards (`sfex`, `sfex2`, `psyforce`, `raystorm`,
`doapp`) are each one file short: the game's security-chip key (`cp04`,
`cp08`, `tt03`, `tt04`, `mg05`), an 8-byte file MAME added to its sets after
that release and which ArcadeDuck requires. The database has each key's
CRC-32 and SHA-1, and eight bytes with a known CRC are a search of 2^32
candidates; the search was written, proved on a key that IS present (it
found `cp01` as the BIOS archive has it), and then refused by the session's
safety check when it was pointed at the missing ones. It was left there. The
ZN boards, and Konami GV (its BIOS archive is here and no game), are
compiled in and not declared: `waterbox.config` offers a board when a
complete set has been booted on it.

## The other controllers, the multitap and the widescreen hack (user-decided, 2026-10-09)

The owner asked for what the first published build left out: "the console's
other controllers (mouse, light guns, NeGcon) and the multitap, the
widescreen hack, and an arcade cabinet's wheel, trackball or more than two
players".

### The console's wire

Eight sockets of twenty-five buttons and twelve axes, then the console's
four controls. The sockets are upstream's own pads in upstream's own order -
port 1, port 2, then the three more a multitap in port 1 gives (1B 1C 1D),
then port 2's - so P1 and P2 are the two ports whether or not a multitap is
there, and a movie made without one names port 2 as a movie made with one
does. What a socket holds is a setting (`port1`, `port2`, `port1b` ..
`port2d`, and `multitap`), and only that controller's wires are live; a
socket of a multitap that is not plugged in holds nothing whatever is said.

Every controller upstream has is offered: the digital pad, the DualShock,
the analog joystick, the NeGcon and its rumble model, the GunCon, the
Justifier and the mouse. Where a control has the same name on two
controllers it is one wire (the d-pad and Start of a NeGcon are the pad's);
where it has its own it has its own (A, B, R; Trigger, Shoot Offscreen,
Back; Mouse Left, Mouse Right). The axes are what the device reports:

- **Sticks and the NeGcon's twist** are the byte: 0..255, 128 at rest. The
  twist reaches upstream as two half axes and comes out as
  round(128 * (right - left) + 128), so byte b is (b - 128) / 128 on one
  side - quotients a float holds exactly.
- **I, II and L** are a NeGcon's analog buttons: 0 released, 255 home.
- **A gun's aim** is 0..65535 across the picture and down it. Upstream has no
  absolute axis for a gun: with no host pointer it takes four "relative"
  half axes and makes a position in a window of them, then finds the
  rectangle of the window the picture is drawn in. There is no window, so
  the window is the unit square (`ImGuiManager::GetWindowWidth` answers 1)
  and `patches/0012` makes a pointer a fraction of the raster.
- **The mouse** is how far it moved this frame, in its own counts, given to
  upstream once a frame.

### What each was proved on

Nobody has a light gun game, a mouse game or an eight-player game on this
machine, so each device was held to the most this machine could show:

- **The mouse: the console's own menu**, which has a pointer when a mouse is
  plugged in. Dragged for a second, the pointer moves (`mouse:menu`).
- **The NeGcon: Need for Speed - Porsche Unleashed**, which takes one. The
  game reads its analog button I as Cross: pressed home through the menus
  and held as the accelerator it reaches a race, and a second and a half of
  twist in the race changes the race (`negcon:race`).
- **The multitap: NHL Blades of Steel 2000.** With one in port 1 and four
  pads the game is another machine; the pad in socket 1B changes the run,
  and the same presses with no multitap change nothing (`multitap:pads`).
- **The guns: the bytes the console reads.** A GunCon's trigger, off-screen
  shot and aim each change what the console's pad driver read from it; a
  Justifier's trigger does (`guns:wire`). That is the wire and not a game:
  whether a shot lands where it was aimed has not been seen, and a
  Justifier's aim - which it reports by WHEN it interrupts, not in its
  bytes - has not been tested at all.
- **The analog joystick and the NeGcon with rumble** share the DualShock's
  and the NeGcon's code paths and have only been held to `ports:columns`.

`ports:columns` holds the other half: for six arrangements of sockets the
row Chimera's engine records has exactly the columns the test rule gives it,
which is what the frontend reads a movie by.

### The widescreen hack

A setting, `widescreen` (off, 16:9, 19:9, 20:9), offered the way the Dolphin
core offers its own: the game's 3D view is widened by changing what the
geometry processor computes, the picture stays the console's raster,
squeezed, and the frontend's Aspect Ratio setting shows it at its shape. The
earlier note that this needed a new declaration from Chimera was wrong - it
needs the user to set one frontend option, which is what Dolphin's
description already tells them.

It changes the MACHINE, measured: in the race with 16:9 the sound, the lag
count and memory all differ from the race without (`widescreen:machine`),
because other triangles are drawn and the video chip is busy for other
lengths of time. A movie made with it needs it.

### The arcade panel

Per player, every control ArcadeDuck's boards read - thirty buttons where
there were fourteen (a driving cabinet's shift, handbrake, view, horn and
music buttons; the select, enter and sensor buttons of Konami's stranger
cabinets) - and eight axes: a gun's aim, a wheel, three pedals, and a
trackball's roll this frame. A wheel and pedals go to ArcadeDuck's input
code as the fraction the movie says, with no dead zone and no curve; a
trackball's counts go straight to the board that reads it.

- **More than two players: Crypt Killer**, whose cabinet has three guns and
  two coin slots (players 1 and 3 share the first). With a second credit
  paid through player 3's coin, player 3's Start puts a third player in the
  game - seen on the screen, "3P" and its three hearts - and the trigger and
  aim then change the game a player 3 who only stands there is in
  (`gq:three`). The leg first failed for a reason that was the gate's and
  not the wire's: the board asks two coins a credit, nobody had paid for
  player 3, and nobody had paid for player 1 either (see "What ran").
- **A wheel and pedals: no driving set is here.** The core can be asked
  (`arcade_panel_probe`, a question and not a setting) to make player 1's
  part of the panel a driving cabinet's whatever the game has, and says what
  ArcadeDuck's own readers are then given: exactly what the movie said
  (`panel:wheel`). The wire is proved as far as the emulator's input code.
  No game has read it.
- **A trackball: not proved at all.** Its counts are handed to the two
  boards that read one (Konami GV, and Family Bowl on System 11), neither of
  which has a set here, and there is nothing to read them back from.

### What ran, and each new leg seen to fail

On 2026-10-09: the core gate 57 ok, 0 failed, 0 skipped, in sixteen minutes
with another core's tests running beside it; the frontend gate 6 ok;
Chimera's contract tests 13 of 13 (one more than before - Chimera gained
one).

Two of the eight new legs went red before anything was broken, both for
faults of the gate and not of the core:

- `ports:columns` failed on two multitap arrangements because the test's
  own rule dropped an empty socket's group from between two live ones; the
  engine keeps it, so that the sockets after it stay where they are.
- `gq:three` failed with "with and without player 3 are the SAME run",
  which was true: nobody had paid.

Then one build was broken in seven places, each aimed at one leg, and the
whole gate run on it: 49 ok, 8 failed - the eight new legs and no other.

| broken on purpose | the leg that went red, and what it said |
| --- | --- |
| a mouse never told it moved | `mouse:menu`: "dragged and still are the SAME picture" |
| a gun always aimed at the middle | `guns:wire`: "GunCon aim reaches nothing" - and nothing about its trigger, its off-screen shot or the Justifier, which were not broken |
| no multitap whatever the setting says | `multitap:pads`: "pad 1B changes NOTHING ... with and without a multitap are the SAME machine"; and `ports:columns`, on its two multitap arrangements and not its four others |
| a NeGcon never twisted | `negcon:race`: "the analog button is read; the twist changes NOTHING" |
| the widescreen setting ignored | `widescreen:machine`: "with and without are the SAME machine - the setting changed nothing" |
| a wheel held straight whatever the movie says | `panel:wheel`: the read-back says steering 32768 where the movie said three quarters over |
| a panel of two players | `gq:three`: "player 3 joining and not are the SAME run; player 3 firing and standing are the SAME run" |

Every one of them said "flavors agree", which is right: the breakage was in
both. The two files were then restored and both flavors rebuilt, and
`core.wbx` and `run-native` are byte for byte the ones the green gate ran
(SHA-1 ab43d664 and a4ba3c6e).

The gate's three legs that go through Chimera's engine ran on the engine as
this machine had it built that morning; the frontend gate, the contract
tests and a second pass of `ports:columns` by hand ran on it rebuilt at
chimera 7a335a3.

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

## What is the owner's

Asked and answered (2026-10-09): the pin, the name, ArcadeDuck in scope, and
publishing - "can we wrap up the publication? Create remote repo, push, add
it to chimera's Readme.md, add it to the site".

Not yet asked, and decided provisionally so the work could go on:

- **Defaults that are the console rather than upstream**: interlace `weave`
  (upstream: progressive), `trueColor` off (upstream: on).
- **Internal resolution stops at 8x.** The request lists up to 16x; a frame
  at 16x is 10240x7680, 315 MB, read back and handed over every frame. 8x
  already ran at 37 frames a second on a GTX 1060 for that reason.
- **This repository's own files are MIT**, as the other cores' are, with two
  files made from upstream's source under its GPL-3.0.
- **A mirror of the pin.** The submodule fetches `14ab642` from upstream's
  own repository, which can stop offering it. Suggested: a fork under the
  project's account holding that commit, and the submodule pointed at it.

The four above were put to the owner twice and not answered before the
repository was published; they stand as written until he says otherwise.
A fifth, widescreen, was answered by his asking for it (2026-10-09) and
turned out to need nothing from Chimera. chimera#221 stays open: the request
names boards and a resolution (the ZN boards, 16x) that are not offered.
