// chimera-core-duckstation: the guest ABI.
//
// One file between DuckStation and the frontend: Init builds the machine out
// of what the project mounted, FrameAdvance runs it for exactly one frame, and
// the rest hand over what that frame looked and sounded like and what the
// machine's memory holds. The same file is compiled into the native reference
// (run-native) and into the sandboxed core (core.wbx); the gate compares the
// two, so nothing here may be decided differently for one of them.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "core/bus.h"
#include "core/controller.h"
#include "core/cpu_core.h"
#include "core/game_database.h"
#include "core/gpu.h"
#include "core/host.h"
#include "core/pad.h"
#include "core/settings.h"
#include "core/spu.h"
#include "core/system.h"
#include "core/timing_event.h"

#include "util/audio_stream.h"
#include "util/gpu_device.h"
#include "util/gpu_texture.h"
#include "util/input_manager.h"

#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/memory_settings_interface.h"

// waterbox/audio-stream.cpp
extern "C" int ChimeraAudioTake(int16_t* out, int max_frames);
// waterbox/memory-device.cpp
const u8* ChimeraMemoryTextureBits(GPUTexture* texture, u32* pitch);
// patches: "the sandbox file system is flat" - a second name for one file
extern "C" void ChimeraFileAlias(const char* from, const char* to);
// waterbox/stubs/frontend.cpp: say on stderr what upstream would have drawn
extern "C" int chimera_verbose;

// set from upstream's pad code, so it has a name outside this file's namespace
static int g_inputRead;

namespace {

// ---- what a frame leaves behind ---------------------------------------------

// The largest picture a PlayStation puts out is 640 columns by 480 lines of
// VRAM, in a raster a little wider than that when no overscan is cropped.
constexpr int MAX_WIDTH = 768;
constexpr int MAX_HEIGHT = 576;
constexpr int MAX_AUDIO_FRAMES = 2048; // waterbox.config's audio.samplesPerFrame

char g_loadError[512];
uint32_t* g_video;
int g_videoWidth = 320;
int g_videoHeight = 240;
int16_t* g_soundOut;
int g_nsamples;
int g_vsyncNum = 53693175;
int g_vsyncDen = 897619;
bool g_booted;

std::unique_ptr<MemorySettingsInterface> g_settingsLayer;

// ---- the wire ----------------------------------------------------------------

// What waterbox.config declares, in its order: per port the DualShock's
// seventeen buttons and its four stick bytes. A digital pad is the first
// sixteen; a port with nothing in it is none of them.
constexpr int PORTS = 2;
constexpr int PORT_BUTTONS = 17;
constexpr int PORT_AXES = 4;
constexpr int WIRE_ANALOG = 16;

// wire order -> upstream's bind index (the same numbers for the digital pad
// and the DualShock, which is the order the pad's own report has them in)
constexpr u8 kBind[PORT_BUTTONS] = {
  4,  // Up
  6,  // Down
  7,  // Left
  5,  // Right
  3,  // Start
  0,  // Select
  15, // Square
  14, // Cross
  13, // Circle
  12, // Triangle
  10, // L1
  11, // R1
  8,  // L2
  9,  // R2
  1,  // L3
  2,  // R3
  16, // Analog
};

ControllerType g_portType[PORTS] = {ControllerType::None, ControllerType::None};
u8 g_held[PORTS][PORT_BUTTONS];
u8 g_stick[PORTS][PORT_AXES] = {{128, 128, 128, 128}, {128, 128, 128, 128}};

ControllerType ParsePort(const char* key, ControllerType dflt)
{
  char v[24] = "";
  if (wbx_setting_str(key, v, sizeof(v)) < 0)
    return dflt;
  if (!strcmp(v, "none"))
    return ControllerType::None;
  if (!strcmp(v, "digital"))
    return ControllerType::DigitalController;
  if (!strcmp(v, "dualshock"))
    return ControllerType::AnalogController;
  return dflt;
}

// One stick byte, put where the pad will report it from. Upstream's pad takes
// each direction of a stick as a separate half axis, scales it to 0..255 and
// merges the pair: 127 + (positive + 1) / 2, or 127 - negative / 2. Inverted,
// every byte has one pair that produces it, and the half-unit added below is
// what makes the float survive the truncation on the other side.
void SetStickByte(Controller* c, u32 positive, u32 negative, u8 value)
{
  const u32 pos = (value >= 128) ? (2u * value - 255u) : 0u;
  const u32 neg = (value >= 128) ? 0u : (2u * (127u - value));
  const u32 first = 17u; // AnalogController::Button::Count
  c->SetBindState(first + negative, (static_cast<float>(neg) + 0.5f) / 255.0f);
  c->SetBindState(first + positive, (static_cast<float>(pos) + 0.5f) / 255.0f);
}

void Fail(const char* what, const Error& error)
{
  snprintf(g_loadError, sizeof(g_loadError), "%s: %s", what, error.GetDescription().c_str());
}

bool Exists(const char* name)
{
  return FileSystem::FileExists(name);
}

// ---- a file with no name ------------------------------------------------------

// A file the frontend was handed directly - dropped on it, or named on a
// command line - is mounted as "disc": no project, no slot map, no extension.
// DuckStation decides what an image is by its extension, so the bytes are
// asked instead and the file is given the name it should have had. Returns
// the name to boot, or null with g_loadError saying why not.
const char* NameTheBareFile()
{
  static char name[32];
  u8 head[16] = {};
  std::FILE* f = std::fopen("disc", "rb");
  if (!f)
    return nullptr;
  const size_t got = std::fread(head, 1, sizeof(head), f);
  std::fclose(f);

  static const u8 kSync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
  const char* ext;
  if (got >= 8 && !std::memcmp(head, "MComprHD", 8))
    ext = "chd";
  else if (got >= 8 && !std::memcmp(head, "PS-X EXE", 8))
    ext = "exe";
  else if (got >= 4 && !std::memcmp(head, "\0PBP", 4))
    ext = "pbp";
  else if (got >= 4 && !std::memcmp(head, "ECM\0", 4))
    ext = "ecm";
  else if (got >= 12 && !std::memcmp(head, kSync, 12))
    ext = "bin";
  else
  {
    // Not a raw sector and nothing with a signature. A cue sheet is the
    // likely one: text, and useless alone, because the files it names were
    // not mounted with it.
    bool text = (got > 0);
    for (size_t i = 0; i < got; i++)
      text = text && (head[i] == 0x09 || head[i] == 0x0A || head[i] == 0x0D || (head[i] >= 0x20 && head[i] < 0x7F) ||
                      head[i] >= 0x80);
    snprintf(g_loadError, sizeof(g_loadError),
             text ? "this is a cue sheet, and it was opened without the files it names. Make a project and put the "
                    ".cue in its Disc slot: the frontend then brings every file the sheet refers to." :
                    "this file is not a disc image this core knows: not a CHD, a PBP, an ECM, a PlayStation "
                    "executable, or raw 2352-byte CD sectors.");
    return nullptr;
  }

  snprintf(name, sizeof(name), "disc.%s", ext);
  ChimeraFileAlias(name, "disc");
  return name;
}

// ---- the picture ------------------------------------------------------------

// The frame is the console's raster, not the texture: the visible rectangle
// of VRAM sits somewhere inside a screen whose size the video mode decides,
// and a game that letterboxes or shifts its picture does it by moving that
// rectangle. Upstream composes this with a shader on its way to a window; here
// it is a copy. Lines the machine did not draw keep what they had, which for
// an interlaced mode is the other field - a weave, the way a television did.
void ComposeFrame()
{
  GPU::ChimeraDisplay d;
  g_gpu->ChimeraGetDisplay(&d);

  int width = std::clamp<int>(d.display_width, 1, MAX_WIDTH);
  int height = std::clamp<int>(d.display_height, 1, MAX_HEIGHT);
  if (width != g_videoWidth || height != g_videoHeight)
  {
    // a new raster: nothing of the old one belongs in it
    g_videoWidth = width;
    g_videoHeight = height;
    std::fill_n(g_video, MAX_WIDTH * MAX_HEIGHT, 0xFF000000u);
  }

  u32 pitch = 0;
  const u8* const bits = d.texture ? ChimeraMemoryTextureBits(d.texture, &pitch) : nullptr;
  if (!bits)
  {
    // the display is off: a black screen, and it IS the picture
    std::fill_n(g_video, MAX_WIDTH * MAX_HEIGHT, 0xFF000000u);
    return;
  }

  // An interlaced field is every other line of the raster.
  const int step = d.interlaced ? 2 : 1;
  const int first = d.interlaced ? static_cast<int>(d.field & 1u) : 0;

  if (!d.interlaced)
  {
    // a progressive frame replaces the whole raster, borders included
    for (int y = 0; y < height; y++)
      std::fill_n(g_video + static_cast<size_t>(y) * MAX_WIDTH, width, 0xFF000000u);
  }

  for (int row = 0; row < d.view_height; row++)
  {
    const int y = static_cast<int>(d.origin_top) + row * step + first;
    if (y < 0 || y >= height)
      continue;

    const u32* src = reinterpret_cast<const u32*>(bits + static_cast<size_t>(d.view_y + row) * pitch) + d.view_x;
    u32* dst = g_video + static_cast<size_t>(y) * MAX_WIDTH;
    for (int col = 0; col < d.view_width; col++)
    {
      const int x = static_cast<int>(d.origin_left) + col;
      if (x >= 0 && x < width)
        dst[x] = src[col] | 0xFF000000u;
    }
  }
}

// ---- settings ---------------------------------------------------------------

void ApplySettings(SettingsInterface& si)
{
  // Upstream's defaults first, so that anything not said below is what a
  // fresh DuckStation would do.
  g_settings.Save(si, false);

  // The renderer that needs no GPU, drawing on the thread that asked.
  si.SetStringValue("GPU", "Renderer", Settings::GetRendererName(GPURenderer::Software));
  si.SetBoolValue("GPU", "UseThread", false);
  si.SetBoolValue("GPU", "DisableShaderCache", true);

  // Nothing is read ahead, and nothing is read on another thread.
  si.SetIntValue("CDROM", "ReadaheadSectors", 0);
  si.SetBoolValue("CDROM", "LoadImageToRAM", false);

  // The interpreter until the code generators are a milestone of their own.
  si.SetStringValue("CPU", "ExecutionMode", Settings::GetCPUExecutionModeName(CPUExecutionMode::Interpreter));
  si.SetStringValue("CPU", "FastmemMode", Settings::GetCPUFastmemModeName(CPUFastmemMode::Disabled));

  // A deinterlacer and a chroma filter are shaders upstream; the frame is
  // composed here instead (ComposeFrame).
  si.SetStringValue("Display", "DeinterlacingMode",
                    Settings::GetDisplayDeinterlacingModeName(DisplayDeinterlacingMode::Disabled));
  si.SetBoolValue("Display", "24BitChromaSmoothing", false);

  // What is in each port. A DualShock in the first and nothing in the second
  // is what a fresh DuckStation has; a game its database says cannot use a
  // DualShock is given the digital pad instead, by upstream, at boot.
  g_portType[0] = ParsePort("port1", ControllerType::AnalogController);
  g_portType[1] = ParsePort("port2", ControllerType::None);
  for (int port = 0; port < PORTS; port++)
  {
    const std::string section = Controller::GetSettingsSection(static_cast<u32>(port));
    si.SetStringValue(section.c_str(), "Type", Controller::GetControllerInfo(g_portType[port])->name);
    // A stick byte is the byte the movie says: no curve, no dead zone, and no
    // stick standing in for the d-pad - that last one is an emulator's
    // convenience, and a pad in digital mode does not report its sticks.
    si.SetFloatValue(section.c_str(), "AnalogSensitivity", 1.0f);
    si.SetFloatValue(section.c_str(), "AnalogDeadzone", 0.0f);
    si.SetBoolValue(section.c_str(), "AnalogDPadInDigitalMode", false);
    si.SetIntValue(section.c_str(), "InvertLeftStick", 0);
    si.SetIntValue(section.c_str(), "InvertRightStick", 0);
  }
  si.SetStringValue("MemoryCards", "Card1Type", Settings::GetMemoryCardTypeName(MemoryCardType::NonPersistent));
  si.SetStringValue("MemoryCards", "Card2Type", Settings::GetMemoryCardTypeName(MemoryCardType::None));
  si.SetStringValue("ControllerPorts", "MultitapMode", Settings::GetMultitapModeName(MultitapMode::Disabled));

  si.SetStringValue("Audio", "Backend", AudioStream::GetBackendName(AudioBackend::Null));

  // No host device is a source of input: the frontend says what is held.
  for (u32 i = 0; i < static_cast<u32>(InputSourceType::Count); i++)
    si.SetBoolValue("InputSources", InputManager::InputSourceToString(static_cast<InputSourceType>(i)), false);

  // What a game database says about a game is the machine's business; what a
  // user's per-game file says is not, and there is no such file.
  si.SetBoolValue("Main", "ApplyGameSettings", true);
  si.SetBoolValue("Main", "ApplyCompatibilitySettings", true);
  si.SetBoolValue("Main", "SaveStateOnExit", false);
  si.SetBoolValue("Main", "ConfirmPowerOff", false);
  si.SetBoolValue("Main", "StartPaused", false);
  si.SetBoolValue("Main", "StartFullscreen", false);
  si.SetBoolValue("Main", "EnableDiscordPresence", false);
  si.SetBoolValue("Cheevos", "Enabled", false);

  // The whole boot, as the console does it, unless the project says otherwise.
  si.SetBoolValue("BIOS", "PatchFastBoot", wbx_setting_bool("fast_boot", 0) != 0);
  si.SetBoolValue("BIOS", "TTYLogging", wbx_setting_bool("verbose", 0) != 0);

  const bool verbose = wbx_setting_bool("verbose", 0) != 0;
  chimera_verbose = verbose ? 1 : 0;
  si.SetBoolValue("Logging", "LogToConsole", verbose);
  si.SetBoolValue("Logging", "LogToFile", false);
  si.SetBoolValue("Logging", "LogToDebug", false);
  si.SetBoolValue("Logging", "LogToWindow", false);
  si.SetStringValue("Logging", "LogLevel", Settings::GetLogLevelName(verbose ? LOGLEVEL_VERBOSE : LOGLEVEL_ERROR));

  // Which market the console was made for. "auto" is upstream's own: the
  // disc decides, and an empty tray is a North American console.
  {
    char region[16] = "auto";
    wbx_setting_str("region", region, sizeof(region));
    ConsoleRegion r = ConsoleRegion::Auto;
    if (!strcmp(region, "ntsc-u"))
      r = ConsoleRegion::NTSC_U;
    else if (!strcmp(region, "ntsc-j"))
      r = ConsoleRegion::NTSC_J;
    else if (!strcmp(region, "pal"))
      r = ConsoleRegion::PAL;
    si.SetStringValue("Console", "Region", Settings::GetConsoleRegionName(r));
  }

  // one firmware, whatever the region: the project chose it
  si.SetStringValue("BIOS", "SearchDirectory", "");
  si.SetStringValue("BIOS", "PathNTSCU", "bios");
  si.SetStringValue("BIOS", "PathNTSCJ", "bios");
  si.SetStringValue("BIOS", "PathPAL", "bios");
}

void SetFolders()
{
  // Everything the machine is made of is mounted at the root of the guest's
  // file system. There is nowhere else to look, and nowhere to write.
  EmuFolders::AppRoot = "./";
  EmuFolders::DataRoot = "./";
  EmuFolders::Bios = "./";
  EmuFolders::Cache = "./";
  EmuFolders::Cheats = "./";
  EmuFolders::Covers = "./";
  EmuFolders::Dumps = "./";
  EmuFolders::GameIcons = "./";
  EmuFolders::GameSettings = "./";
  EmuFolders::InputProfiles = "./";
  EmuFolders::MemoryCards = "./";
  EmuFolders::Resources = "./";
  EmuFolders::SaveStates = "./";
  EmuFolders::Screenshots = "./";
  EmuFolders::Shaders = "./";
  EmuFolders::Textures = "./";
  EmuFolders::UserResources = "./";
  EmuFolders::Videos = "./";
}

} // namespace

// The clock every Common::Timer in upstream reads (patches: "time is the
// machine's"): how long the console has been running, by its own crystal.
// Nothing here asks the host what time it is, so the throttle upstream runs at
// the end of every frame finds the frame ended exactly when it was due.
extern "C" uint64_t ChimeraClockNanoseconds(void)
{
  const uint64_t ticks = static_cast<uint64_t>(TimingEvents::GetGlobalTickCounter());
  const uint64_t hz = static_cast<uint64_t>(System::g_ticks_per_second);
  return (ticks / hz) * UINT64_C(1000000000) + ((ticks % hz) * UINT64_C(1000000000)) / hz;
}

// patches: "a lag frame is a frame nobody polled". A pad answered the console.
extern "C" void chimera_input_was_read(void)
{
  g_inputRead = 1;
}

// Host::FrameDone: the machine has finished a frame and is still inside it.
extern "C" void ChimeraFrameDone(void)
{
}

extern "C" {

ECL_EXPORT const char* GetLoadError(void)
{
  return g_loadError;
}

ECL_EXPORT int Init(void)
{
  g_loadError[0] = '\0';

  g_video = static_cast<uint32_t*>(alloc_invisible(sizeof(uint32_t) * MAX_WIDTH * MAX_HEIGHT));
  g_soundOut = static_cast<int16_t*>(alloc_invisible(sizeof(int16_t) * 2 * MAX_AUDIO_FRAMES));
  std::fill_n(g_video, MAX_WIDTH * MAX_HEIGHT, 0xFF000000u);

  SetFolders();

  g_settingsLayer = std::make_unique<MemorySettingsInterface>();
  Host::Internal::SetBaseSettingsLayer(g_settingsLayer.get());
  ApplySettings(*g_settingsLayer);
  EmuFolders::LoadConfig(*g_settingsLayer);
  SetFolders();

  Error error;
  if (!System::Internal::ProcessStartup(&error) || !System::Internal::CPUThreadInitialize(&error))
  {
    Fail("the machine would not initialise", error);
    return 0;
  }

  // The per-title database, loaded HERE rather than where upstream first asks
  // for it: before the guest is sealed, so it costs nothing in every state
  // afterwards, and before the machine is asked for anything else, so a core
  // with an EMPTY database can be caught with no BIOS and no disc at all.
  // "gamedb_probe" is a question put to the core, not a setting: it is not in
  // waterbox.config, it changes nothing, and a movie must not carry it.
  GameDatabase::EnsureLoaded();
  {
    char probe[32] = "";
    if (wbx_setting_str("gamedb_probe", probe, sizeof(probe)) > 0)
    {
      const GameDatabase::Entry* const entry = GameDatabase::GetEntryForSerial(probe);
      std::fprintf(stderr, "duckstation: gamedb probe: %s = %s\n", probe, entry ? entry->title.c_str() : "(not on record)");
    }
  }

  if (!Exists("bios"))
  {
    snprintf(g_loadError, sizeof(g_loadError),
             "no bios: a PlayStation needs its ROM, and this core carries none. Add one to the project's firmware.");
    return 0;
  }

  // The disc, if the project has one. A PlayStation with an empty tray is a
  // PlayStation sitting at its own menu, which is a machine worth booting.
  SystemBootParameters params;
  char name[512];
  if (wbx_slot_first("disc", name, sizeof(name)) != nullptr && name[0] != '\0')
  {
    // a project: the file is mounted under its own name, extension and all
    params.filename = name;
  }
  else if (Exists("disc"))
  {
    const char* const bare = NameTheBareFile();
    if (!bare)
      return 0;
    params.filename = bare;
  }

  params.override_fullscreen = false;
  params.override_start_paused = false;

  if (!System::BootSystem(std::move(params), &error))
  {
    Fail("the machine would not boot", error);
    return 0;
  }

  // The field rate, exactly: the video crystal over the ticks in a line times
  // the lines in a field, as the machine booted. 59.826 Hz on an NTSC
  // console, 49.761 on a PAL one.
  {
    GPU::ChimeraDisplay d;
    g_gpu->ChimeraGetDisplay(&d);
    int64_t num = g_gpu->GetCRTCFrequency();
    int64_t den = static_cast<int64_t>(d.horizontal_total) * d.vertical_total;
    if (den > 0)
    {
      int64_t a = num, b = den;
      while (b != 0)
      {
        const int64_t t = a % b;
        a = b;
        b = t;
      }
      g_vsyncNum = static_cast<int>(num / a);
      g_vsyncDen = static_cast<int>(den / a);
    }
  }

  g_booted = true;
  return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
  if (index < 0 || index >= PORTS * PORT_BUTTONS)
    return;

  const int port = index / PORT_BUTTONS;
  const int wire = index % PORT_BUTTONS;
  const u8 held = state ? 1 : 0;
  const u8 was = g_held[port][wire];
  g_held[port][wire] = held;

  Controller* const c = Pad::GetController(static_cast<u32>(port));
  if (!c)
    return;

  if (wire == WIRE_ANALOG)
  {
    // The Analog button is a toggle on the pad itself, and upstream toggles
    // every time it is TOLD the button is down: it is told once per press.
    if (held && !was && c->GetType() == ControllerType::AnalogController)
      c->SetBindState(kBind[wire], 1.0f);
    return;
  }

  c->SetBindState(kBind[wire], held ? 1.0f : 0.0f);
}

/* Which of the declared wires the machine can see: the frontend greys out the
 * rest. What a port holds is decided at boot (a setting, and upstream's game
 * database on top of it), so the controller that is there is asked. */
ECL_EXPORT int IsButtonActive(int32_t index)
{
  if (index < 0 || index >= PORTS * PORT_BUTTONS)
    return 0;

  const Controller* const c = Pad::GetController(static_cast<u32>(index / PORT_BUTTONS));
  if (!c)
    return 0;

  const int wire = index % PORT_BUTTONS;
  switch (c->GetType())
  {
    case ControllerType::AnalogController:
      return 1;
    case ControllerType::DigitalController:
      // a digital pad has no stick to click and no Analog button
      return (wire != 14 && wire != 15 && wire != WIRE_ANALOG) ? 1 : 0;
    default:
      return 0;
  }
}

ECL_EXPORT int IsAxisActive(int32_t index)
{
  if (index < 0 || index >= PORTS * PORT_AXES)
    return 0;

  const Controller* const c = Pad::GetController(static_cast<u32>(index / PORT_AXES));
  return (c && c->GetType() == ControllerType::AnalogController) ? 1 : 0;
}

ECL_EXPORT void SetAxis(int32_t index, int32_t value)
{
  if (index < 0 || index >= PORTS * PORT_AXES)
    return;

  const int port = index / PORT_AXES;
  const int axis = index % PORT_AXES;
  const u8 byte = static_cast<u8>(std::clamp<int32_t>(value, 0, 255));
  if (g_stick[port][axis] == byte)
    return;
  g_stick[port][axis] = byte;

  Controller* const c = Pad::GetController(static_cast<u32>(port));
  if (!c || c->GetType() != ControllerType::AnalogController)
    return;

  // AnalogController::HalfAxis: LLeft LRight LDown LUp RLeft RRight RDown RUp.
  // A stick byte grows to the right and downwards.
  switch (axis)
  {
    case 0:
      SetStickByte(c, 1, 0, byte);
      break;
    case 1:
      SetStickByte(c, 2, 3, byte);
      break;
    case 2:
      SetStickByte(c, 5, 4, byte);
      break;
    default:
      SetStickByte(c, 6, 7, byte);
      break;
  }
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
  g_inputRead = 0;
  g_nsamples = 0;
  if (!g_booted)
    return;

  // One frame: Host::PumpMessagesOnCPUThread pauses the system at the end of
  // the frame the machine draws, and Execute returns when it is paused.
  System::PauseSystem(false);
  System::Execute();

  ComposeFrame();
  g_nsamples = ChimeraAudioTake(g_soundOut, MAX_AUDIO_FRAMES);
}

/* For the gate, not for the frontend: where the machine is, as one number.
 * Its clock and where its processor is about to execute, which two runs that
 * have parted company cannot share for long. */
ECL_EXPORT uint64_t ChimeraMachineDigest(void)
{
  uint64_t h = static_cast<uint64_t>(TimingEvents::GetGlobalTickCounter());
  h = (h * UINT64_C(1099511628211)) ^ CPU::g_state.pc;
  h = (h * UINT64_C(1099511628211)) ^ CPU::g_state.npc;
  for (u32 i = 0; i < static_cast<u32>(CPU::Reg::count); i++)
    h = (h * UINT64_C(1099511628211)) ^ CPU::g_state.regs.r[i];
  return h;
}

ECL_EXPORT void SetRenderingEnabled(int on)
{
}

ECL_EXPORT uint32_t* GetVideoBgra(void)
{
  // the frame is kept at the buffer's own pitch; hand it over packed
  static uint32_t* packed;
  if (!packed)
    packed = static_cast<uint32_t*>(alloc_invisible(sizeof(uint32_t) * MAX_WIDTH * MAX_HEIGHT));
  for (int y = 0; y < g_videoHeight; y++)
  {
    std::memcpy(packed + static_cast<size_t>(y) * g_videoWidth, g_video + static_cast<size_t>(y) * MAX_WIDTH,
                sizeof(uint32_t) * g_videoWidth);
  }
  return packed;
}

ECL_EXPORT int GetVideoWidth(void)
{
  return g_videoWidth;
}
ECL_EXPORT int GetVideoHeight(void)
{
  return g_videoHeight;
}

ECL_EXPORT int16_t* GetAudio(void)
{
  return g_soundOut;
}
ECL_EXPORT int GetAudioSampleCount(void)
{
  return g_nsamples;
}

ECL_EXPORT int GetVsyncNumerator(void)
{
  return g_vsyncNum;
}
ECL_EXPORT int GetVsyncDenominator(void)
{
  return g_vsyncDen;
}

ECL_EXPORT int InputWasRead(void)
{
  return g_inputRead;
}

// ---- memory domains ---------------------------------------------------------

ECL_EXPORT int GetMemoryDomainCount(void)
{
  return 2;
}

ECL_EXPORT const char* GetMemoryDomainName(int which)
{
  static const char* const names[] = {"Main RAM", "BIOS ROM"};
  return (which >= 0 && which < 2) ? names[which] : "";
}

ECL_EXPORT uint8_t* GetMemoryDomainPtr(int which)
{
  switch (which)
  {
    case 0:
      return Bus::g_ram;
    case 1:
      return Bus::g_bios;
    default:
      return nullptr;
  }
}

ECL_EXPORT int64_t GetMemoryDomainSize(int which)
{
  switch (which)
  {
    case 0:
      return Bus::g_ram_size;
    case 1:
      return Bus::BIOS_SIZE;
    default:
      return 0;
  }
}

ECL_EXPORT int GetMemoryDomainWritable(int which)
{
  return which == 0;
}

ECL_EXPORT int32_t GetSaveDataFileCount(void)
{
  return 0;
}
ECL_EXPORT const char* GetSaveDataFileName(int32_t i)
{
  return "";
}
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i)
{
  return 0;
}
ECL_EXPORT const uint8_t* GetSaveDataFileBuffer(int32_t i)
{
  return nullptr;
}

} // extern "C"
