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
// waterbox/stubs/frontend.cpp: say on stderr what upstream would have drawn
extern "C" int chimera_verbose;

namespace {

// ---- what a frame leaves behind ---------------------------------------------

// The largest picture a PlayStation puts out is 640 columns by 480 lines of
// VRAM, in a raster a little wider than that when no overscan is cropped.
constexpr int MAX_WIDTH = 768;
constexpr int MAX_HEIGHT = 576;
constexpr int MAX_AUDIO_FRAMES = 44100;

char g_loadError[512];
uint32_t* g_video;
int g_videoWidth = 320;
int g_videoHeight = 240;
int16_t* g_soundOut;
int g_nsamples;
int g_inputRead;
int g_vsyncNum = 60;
int g_vsyncDen = 1;
bool g_booted;

std::unique_ptr<MemorySettingsInterface> g_settingsLayer;

void Fail(const char* what, const Error& error)
{
  snprintf(g_loadError, sizeof(g_loadError), "%s: %s", what, error.GetDescription().c_str());
}

bool Exists(const char* name)
{
  return FileSystem::FileExists(name);
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

  si.SetStringValue("Pad1", "Type", Controller::GetControllerInfo(ControllerType::DigitalController)->name);
  si.SetStringValue("Pad2", "Type", Controller::GetControllerInfo(ControllerType::DigitalController)->name);
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
    params.filename = name;
  else if (Exists("disc"))
    params.filename = "disc";

  params.override_fullscreen = false;
  params.override_start_paused = false;

  if (!System::BootSystem(std::move(params), &error))
  {
    Fail("the machine would not boot", error);
    return 0;
  }

  const float hz = g_gpu->ComputeVerticalFrequency();
  if (hz < 55.0f)
  {
    // PAL: 49.76 Hz upstream computes; the frontend is told the rational the
    // console's own counters give (see docs/PLAN.md, "The field rate").
    g_vsyncNum = 50;
    g_vsyncDen = 1;
  }

  g_booted = true;
  return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
  // wire: sixteen digital-pad buttons per port, upstream's own order
  const u32 port = static_cast<u32>(index) / 16u;
  const u32 bind = static_cast<u32>(index) % 16u;
  if (Controller* const c = Pad::GetController(port))
    c->SetBindState(bind, state ? 1.0f : 0.0f);
}

ECL_EXPORT void SetAxis(int32_t index, int32_t value)
{
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
