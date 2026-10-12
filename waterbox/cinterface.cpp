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
#include <vector>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "core/bios.h"
#include "core/bus.h"
#include "core/cpu_core.h"
#include "core/game_database.h"
#include "core/gpu.h"
#include "core/gpu_hw.h"
#include "core/host.h"
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

#ifdef CHIMERA_ARCADE
#include "core/arcade/arcade_control_registry.h"
#include "core/arcade/arcade_database.h"
#include "core/arcade/arcade_input.h"
#include "core/arcade/arcade_machine_handler.h"
#else
#include "core/controller.h"
#include "core/memory_card.h"
#include "core/memory_card_image.h"
#include "core/pad.h"
#endif

#include "machine.h"

// waterbox/audio-stream.cpp
extern "C" int ChimeraAudioTake(int16_t* out, int max_frames);
// waterbox/memory-device.cpp
const u8* ChimeraMemoryTextureBits(GPUTexture* texture, u32* pitch);
// patches: "the sandbox file system is flat" - a second name for one file
extern "C" void ChimeraFileAlias(const char* from, const char* to);
#ifdef CHIMERA_GUEST_GL
// waterbox/gl-bridged.cpp and the generated wrappers (miniBox source/gl)
#include "gl-bridge.h"
extern "C" bool chimera_gl_bridge_start(chimera_gl_bridge_fn bridge);
extern "C" bool chimera_gl_bridged();
// patches: "what the card holds outlives a load" - read by GPU_HW::DoState
extern "C" {
bool chimera_gl_objects_stale = false;
}
#endif
#ifdef CHIMERA_GUEST_VK
// miniBox source/vk/vk-bridge.h: the guest half of the Vulkan bridge. Said
// here and not included, because that header brings the Vulkan headers of
// miniBox with it and upstream compiles against its own.
extern "C" {
typedef uint64_t (*chimera_vk_bridge_fn)(uint64_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e);
bool chimera_vk_install(chimera_vk_bridge_fn bridge);
uint64_t chimera_vk_context_id(void);
void chimera_vk_forget_mappings(void);
}
#endif
// waterbox/stubs/frontend.cpp: say on stderr what upstream would have drawn
extern "C" int chimera_verbose;

// set from upstream's pad code, so it has a name outside this file's namespace
static int g_inputRead;

namespace {

// ---- what a frame leaves behind ---------------------------------------------

// The largest picture a PlayStation puts out is 640 columns by 480 lines of
// VRAM (576 on a PAL console), in a raster a little wider than that when no
// overscan is cropped. The hardware renderer draws it at a whole multiple.
constexpr int MAX_WIDTH = 768;
constexpr int MAX_HEIGHT = 576;
constexpr int MAX_SCALE = 8; // waterbox.config's video.width and height are these three multiplied out
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
int g_scale = 1;          // the internal resolution the project asked for
size_t g_videoCapacity;   // pixels g_video has room for

std::unique_ptr<MemorySettingsInterface> g_settingsLayer;
std::string g_rendererWanted = "software";

// ---- the machine ---------------------------------------------------------------

// This file is compiled twice: against extern/duckstation for the console, and
// against extern/arcadeduck for the arcade boards (meson.build, "one binary,
// two machines"). What differs between them - what is plugged in, what is
// booted, what is saved - is behind these, in machine-console.inc and
// machine-arcade.inc.
void MachineApplySettings(SettingsInterface& si);
bool MachineBoot(Error* error);
void MachineBeforeFrame();
void MachineAfterFrame();

void Fail(const char* what, const Error& error)
{
  snprintf(g_loadError, sizeof(g_loadError), "%s: %s", what, error.GetDescription().c_str());
}

bool Exists(const char* name)
{
  return FileSystem::FileExists(name);
}


// ---- the picture ------------------------------------------------------------

// What a GL device's frame is read back through. It is an object of that
// device, so it goes when the device does (RebuildForNewContext).
std::unique_ptr<GPUDownloadTexture> g_download;
bool g_renderEnabled = true;

#ifdef CHIMERA_GUEST_GL
// ---- a real GPU, and the contexts its objects belong to -----------------------
//
// The GL objects the hardware renderer holds are NAMES a context handed out,
// and they live in guest memory - so a state carries them into whatever
// session loads it, where the driver has something else under each name or
// nothing at all. The bridge says which context the calls are landing on
// (chimera_gl_context_id; 0 for the softpipe, which is "nothing can have
// moved"), and that number moves on every load. Compared at the top of every
// frame, before anything is drawn.
//
// The frame-0 case (chimera issue 126): the number is first written by the
// check itself, so the state taken right after Init carries 0 while a device
// already exists. Loading it must rebuild too, which is why the engine's own
// word for "the machine's memory was replaced" (StateLoaded) is kept beside it.
uint64_t g_glContext = 0;
bool g_glStateLoaded = false;

// Which of the two bridges the hardware renderer draws through. The Renderer
// setting says ("vulkan-hw" is Vulkan, anything else that is not "software"
// is OpenGL), and each bridge is there only if the host handed it over. What
// is said above about OpenGL's names is true of Vulkan's handles, and more
// so: a load there is always a new device.
#ifdef CHIMERA_GUEST_VK
bool g_vulkanBridge = false; // SetVulkanBridge was called, and the bridge took the callback
chimera_vk_bridge_fn g_vulkanHost = nullptr;
bool g_vulkanQuiet = false;

// Every Vulkan command goes through here on its way to the host. While the
// renderer lets go of a device that a load has already destroyed (the
// rebuild, below), nothing crosses: each command answers "done" at once. The
// objects are gone on the host's side whatever is said of them, and a wait
// on one of their fences then ends instead of failing.
uint64_t VulkanBridge(uint64_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
  if (g_vulkanQuiet && op >= 100)
    return 0;
  return g_vulkanHost(op, a, b, c, d, e);
}

bool WantsVulkan()
{
  return g_rendererWanted == "vulkan-hw";
}
#else
bool WantsVulkan()
{
  return false;
}
#endif

// A real graphics card is drawing: the bridge the setting names was handed over.
bool OnTheCard()
{
#ifdef CHIMERA_GUEST_VK
  if (WantsVulkan())
    return g_vulkanBridge;
#endif
  return chimera_gl_bridged();
}

// Which context (OpenGL) or device (Vulkan) the calls are landing on.
uint64_t LiveContext()
{
#ifdef CHIMERA_GUEST_VK
  if (WantsVulkan())
    return g_vulkanBridge ? chimera_vk_context_id() : 0;
#endif
  return chimera_gl_context_id();
}

bool HardwareDevice()
{
  if (!g_gpu_device)
    return false;
  const RenderAPI api = g_gpu_device->GetRenderAPI();
  return api == RenderAPI::OpenGL || api == RenderAPI::Vulkan;
}

// The console's VRAM as the card holds it, kept where a state can carry it.
// The hardware renderer's VRAM is one texture, at the internal resolution
// and in 24-bit colour; upstream's own copy of it (g_vram) is the console's
// 1024x512 in 15-bit, which is what the MACHINE can read back but not what
// the picture is made of. Carrying only that put the picture right again two
// frames after a load, with 40% of the pixels off by up to four levels in
// between (measured on the BIOS menu, 2026-10-09). So the texture itself is
// fetched before a state and uploaded after a load.
//
// g_vramKept is this core's own memory, allocated during Init at the size the
// project's resolution needs, and in the state. g_vramFetch is where the card
// writes; it is in no state, and rows are copied across only when they
// differ, so a part of VRAM nobody drew to costs a state's delta nothing.
u8* g_vramKept;
u8* g_vramFetch;
u32 g_vramKeptWidth, g_vramKeptHeight; // 0 until a state has been taken with it
u32 g_vramRoomWidth, g_vramRoomHeight;

GPU_HW* HardwareRenderer()
{
  return (g_gpu && g_gpu->IsHardwareRenderer()) ? static_cast<GPU_HW*>(g_gpu.get()) : nullptr;
}

void FetchVRAMFromTheCard()
{
  GPU_HW* const hw = HardwareRenderer();
  if (!hw || !g_vramKept)
    return;

  u32 width, height;
  hw->ChimeraVRAMTextureSize(&width, &height);
  if (width == 0 || width > g_vramRoomWidth || height > g_vramRoomHeight)
    return;

  const u32 pitch = width * 4u;
  if (!hw->ChimeraDownloadVRAMTexture(g_vramFetch, pitch))
    return;

  for (u32 y = 0; y < height; y++)
  {
    const u8* const src = g_vramFetch + static_cast<size_t>(y) * pitch;
    u8* const dst = g_vramKept + static_cast<size_t>(y) * pitch;
    if (g_vramKeptWidth != width || std::memcmp(dst, src, pitch) != 0)
      std::memcpy(dst, src, pitch);
  }
  g_vramKeptWidth = width;
  g_vramKeptHeight = height;
}

void GiveVRAMBackToTheCard()
{
  GPU_HW* const hw = HardwareRenderer();
  if (!hw || !g_vramKept || g_vramKeptWidth == 0)
    return;

  u32 width, height;
  hw->ChimeraVRAMTextureSize(&width, &height);
  if (width != g_vramKeptWidth || height != g_vramKeptHeight)
    return; // a state from another resolution: the console's own copy has to do
  hw->ChimeraUploadVRAMTexture(g_vramKept, width * 4u);
}

// The rebuild is upstream's own "the device went away": everything the old
// context owned is let go of first, a device is made, the renderer is made,
// and VRAM is uploaded from guest memory - which a state DOES carry, because
// StateSaving fetched it from the card before the state was taken. What is
// lost is what upstream loses loading one of its own states: detail above the
// console's resolution, until the game draws it again.
void RebuildForNewContext(uint64_t was, uint64_t now)
{
  if (chimera_verbose)
    std::fprintf(stderr, "duckstation: GL objects came from context %llu, now %llu; rebuilding\n",
                 static_cast<unsigned long long>(was), static_cast<unsigned long long>(now));

  g_download.reset();
  chimera_gl_objects_stale = true;
#ifdef CHIMERA_GUEST_VK
  // until the old device has been let go of (ChimeraOldDeviceGone, which
  // upstream's recreate calls when it has), nothing of Vulkan crosses
  g_vulkanQuiet = WantsVulkan();
#endif
  const bool ok = System::ChimeraRecreateGPU();
  chimera_gl_objects_stale = false;
#ifdef CHIMERA_GUEST_VK
  g_vulkanQuiet = false;
#endif
  if (!ok)
  {
    std::fprintf(stderr, "duckstation: the renderer could not be rebuilt on the new context\n");
    return;
  }

  // upstream has uploaded the console's own copy; now the card's
  GiveVRAMBackToTheCard();
}

void CheckGLContext()
{
  if (!HardwareDevice())
    return;

  const bool after_load = g_glStateLoaded;
  g_glStateLoaded = false;
  const uint64_t live = LiveContext();
  if (live == 0 || live == g_glContext)
    return;

  if (g_glContext != 0 || after_load)
    RebuildForNewContext(g_glContext, live);
  g_glContext = live;
}
#endif

// The frame is the console's raster, not the texture: the visible rectangle
// of VRAM sits somewhere inside a screen whose size the video mode decides,
// and a game that letterboxes or shifts its picture does it by moving that
// rectangle. Upstream composes this with a shader on its way to a window; here
// it is a copy. Lines the machine did not draw keep what they had, which for
// an interlaced mode is the other field - a weave, the way a television did.
//
// The software renderer's texture is bytes already (waterbox/memory-device.cpp).
// The hardware renderer's is on a GL device - Mesa's softpipe inside the
// sandbox, or a real card across the bridge - and is read back from it, at
// whatever multiple of the console's resolution it was drawn at.
void ComposeFrame()
{
  GPU::ChimeraDisplay d;
  g_gpu->ChimeraGetDisplay(&d);

#ifdef CHIMERA_ARCADE
  // A cabinet's monitor did not show every line its board draws, and
  // ArcadeDuck records, per game, how many it hid at the top and the bottom
  // (Tekken: eleven and one). It takes them off the picture on its way to a
  // window and stretches what is left; a core hands rows over, so the rows are
  // left out and the raster is that much shorter - the frontend gives it its
  // 4:3 shape either way. Only under "overscan", as there: "none" is the whole
  // raster, for somebody who wants to see what the monitor hid.
  if (System::IsArcadeVideoTiming() && g_settings.display_crop_mode == DisplayCropMode::Overscan && d.texture &&
      d.vram_height > 0 && d.view_height > 0)
  {
    const u32 lines = d.vram_height;
    const u32 crop_top = std::min<u32>(g_settings.display_arcade_monitor_crop_top, lines - 1);
    const u32 crop_bottom = std::min<u32>(g_settings.display_arcade_monitor_crop_bottom, lines - crop_top - 1);
    // the view is those lines at the renderer's scale (half of them a field,
    // in an interlaced mode): the same share of its rows goes
    const s32 top_rows = static_cast<s32>((static_cast<u64>(crop_top) * static_cast<u32>(d.view_height)) / lines);
    const s32 bottom_rows = static_cast<s32>((static_cast<u64>(crop_bottom) * static_cast<u32>(d.view_height)) / lines);
    if ((crop_top + crop_bottom) > 0 && (top_rows + bottom_rows) < d.view_height &&
        (crop_top + crop_bottom) < d.display_height)
    {
      d.view_y += top_rows;
      d.view_height -= top_rows + bottom_rows;
      d.display_height -= crop_top + crop_bottom;
    }
  }
#endif

  // How many texture pixels there are to one of the console's. The renderer
  // says nothing about it directly; the texture's view of the visible VRAM
  // rectangle does. 24-bit pictures (video) are never scaled.
  int scale = 1;
  if (d.texture && d.vram_width > 0 && d.view_width >= static_cast<s32>(d.vram_width))
    scale = std::clamp<int>(d.view_width / static_cast<int>(d.vram_width), 1, MAX_SCALE);

  int width = std::clamp<int>(d.display_width, 1, MAX_WIDTH) * scale;
  int height = std::clamp<int>(d.display_height, 1, MAX_HEIGHT) * scale;
  if (static_cast<size_t>(width) * height > g_videoCapacity)
  {
    // cannot happen while the scale is clamped to what was allocated for
    width = MAX_WIDTH;
    height = MAX_HEIGHT;
    scale = 1;
  }
  if (width != g_videoWidth || height != g_videoHeight)
  {
    // a new raster: nothing of the old one belongs in it
    g_videoWidth = width;
    g_videoHeight = height;
    std::fill_n(g_video, static_cast<size_t>(width) * height, 0xFF000000u);
  }

  if (!d.texture || d.view_width <= 0 || d.view_height <= 0)
  {
    // the display is off: a black screen, and it IS the picture
    std::fill_n(g_video, static_cast<size_t>(width) * height, 0xFF000000u);
    return;
  }

  // The texture's bytes: where they already are, or read back from the device.
  u32 pitch = 0;
  const u8* bits = ChimeraMemoryTextureBits(d.texture, &pitch);
  std::unique_ptr<GPUDownloadTexture>& download = g_download;
  bool swap_rb = false;
  bool flip = false;
  int row0 = d.view_y, col0 = d.view_x;
  if (!bits)
  {
    static std::vector<u32> texels;
    const GPUTexture::Format format = d.texture->GetFormat();
    if (format != GPUTexture::Format::RGBA8 && format != GPUTexture::Format::BGRA8)
    {
      std::fill_n(g_video, static_cast<size_t>(width) * height, 0xFF000000u);
      return;
    }
    if (!download || download->GetWidth() != static_cast<u32>(d.view_width) ||
        download->GetHeight() != static_cast<u32>(d.view_height) || download->GetFormat() != format)
    {
      download = g_gpu_device->CreateDownloadTexture(d.view_width, d.view_height, format);
      texels.resize(static_cast<size_t>(d.view_width) * d.view_height);
    }
    pitch = static_cast<u32>(d.view_width) * 4u;
    if (!download)
      return;
    download->CopyFromTexture(0, 0, d.texture, d.view_x, d.view_y, d.view_width, d.view_height, 0, 0, true);
    if (!download->ReadTexels(0, 0, d.view_width, d.view_height, texels.data(), pitch))
      return;

    bits = reinterpret_cast<const u8*>(texels.data());
    swap_rb = (format == GPUTexture::Format::RGBA8);
    // Not flipped. OpenGL counts rows from the bottom and upstream's own
    // screenshot code flips what it reads back, but the renderer draws VRAM
    // into its texture top row first, and a read of that texture comes back
    // the way it went in: flipping here turned the picture upside down
    // (seen on softpipe, 2026-10-09; to be seen again on a real driver).
    flip = false;
    row0 = 0;
    col0 = 0;
  }

  // An interlaced field is every other line of the raster.
  const int step = d.interlaced ? 2 : 1;
  const int first = d.interlaced ? static_cast<int>(d.field & 1u) : 0;

  if (!d.interlaced)
  {
    // a progressive frame replaces the whole raster, borders included
    std::fill_n(g_video, static_cast<size_t>(width) * height, 0xFF000000u);
  }

  const int left = static_cast<int>(d.origin_left) * scale;
  const int top = static_cast<int>(d.origin_top) * scale;
  for (int row = 0; row < d.view_height; row++)
  {
    const int y = top + row * step + first;
    if (y < 0 || y >= height)
      continue;

    const int src_row = flip ? (d.view_height - 1 - row) : row;
    const u32* src = reinterpret_cast<const u32*>(bits + static_cast<size_t>(row0 + src_row) * pitch) + col0;
    u32* dst = g_video + static_cast<size_t>(y) * width;
    for (int col = 0; col < d.view_width; col++)
    {
      const int x = left + col;
      if (x < 0 || x >= width)
        continue;
      const u32 p = src[col];
      dst[x] = swap_rb ? (0xFF000000u | ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p >> 16) & 0xFFu)) : (p | 0xFF000000u);
    }
  }
}

// ---- settings ---------------------------------------------------------------

void ApplySettings(SettingsInterface& si)
{
  // Upstream's defaults first, so that anything not said below is what a
  // fresh DuckStation would do.
  g_settings.Save(si, false);

  // The renderer. "software" needs no GPU of any kind and is the one the
  // native reference has; "opengl" is the hardware renderer, drawing through
  // the OpenGL this core was built with.
  {
    char renderer[16] = "software";
    wbx_setting_str("renderer", renderer, sizeof(renderer));
    g_rendererWanted = renderer;
#ifdef CHIMERA_GUEST_GL
    // "opengl" and "opengl-hw" are one renderer. The second asks for a real
    // graphics card; whether there is one is the host's to say (SetGpuBridge),
    // and without one the softpipe draws, as it does for the first.
    // "vulkan-hw" is the same hardware renderer on DuckStation's Vulkan
    // device. That one has no softpipe behind it: with no Vulkan handed over
    // Init refuses, and says so.
    GPURenderer which = GPURenderer::HardwareOpenGL;
    if (strcmp(renderer, "software") == 0)
      which = GPURenderer::Software;
#ifdef CHIMERA_GUEST_VK
    else if (strcmp(renderer, "vulkan-hw") == 0)
      which = GPURenderer::HardwareVulkan;
#endif
    si.SetStringValue("GPU", "Renderer", Settings::GetRendererName(which));
#else
    si.SetStringValue("GPU", "Renderer", Settings::GetRendererName(GPURenderer::Software));
#endif
    // "1x" .. "8x": strtol stops at the letter
    g_scale = std::clamp<int>(static_cast<int>(wbx_setting_long("internalResolution", 1)), 1, MAX_SCALE);
    if (strcmp(renderer, "software") == 0)
      g_scale = 1; // the software renderer draws the console's own pixels

    // The hardware renderer's enhancements. Every one is off unless the
    // project turns it on: what this core draws by default is what the
    // console drew, at whatever resolution, and an enhancement is a choice a
    // project records. (Upstream's own defaults differ in one: it draws in
    // 24-bit colour unless told not to.) They do nothing under "software".
    si.SetBoolValue("GPU", "TrueColor", wbx_setting_bool("trueColor", 0) != 0);
    si.SetBoolValue("GPU", "ScaledDithering", wbx_setting_bool("scaledDithering", 1) != 0);
    si.SetBoolValue("GPU", "Debanding", false);

    static const struct { const char* ours; const char* upstream; } kFilters[] = {
      {"nearest", "Nearest"},
      {"bilinear", "Bilinear"},
      {"bilinearBinAlpha", "BilinearBinAlpha"},
      {"jinc2", "JINC2"},
      {"jinc2BinAlpha", "JINC2BinAlpha"},
      {"xbr", "xBR"},
      {"xbrBinAlpha", "xBRBinAlpha"},
    };
    const auto filter = [](const char* key) {
      char v[24] = "nearest";
      wbx_setting_str(key, v, sizeof(v));
      for (const auto& f : kFilters)
      {
        if (strcmp(v, f.ours) == 0)
          return f.upstream;
      }
      return "Nearest";
    };
    si.SetStringValue("GPU", "TextureFilter", filter("textureFiltering"));
    si.SetStringValue("GPU", "SpriteTextureFilter", filter("spriteTextureFiltering"));

    {
      char v[16] = "disabled";
      wbx_setting_str("downsampling", v, sizeof(v));
      si.SetStringValue("GPU", "DownsampleMode",
                        strcmp(v, "box") == 0 ? "Box" : (strcmp(v, "adaptive") == 0 ? "Adaptive" : "Disabled"));
    }

    // PGXP: the geometry the game computed, kept at the precision it was
    // computed at instead of rounded to the console's whole pixels.
    const bool pgxp = wbx_setting_bool("pgxp", 0) != 0;
    si.SetBoolValue("GPU", "PGXPEnable", pgxp);
    si.SetBoolValue("GPU", "PGXPCulling", pgxp && wbx_setting_bool("pgxpCulling", 1) != 0);
    si.SetBoolValue("GPU", "PGXPTextureCorrection", pgxp && wbx_setting_bool("pgxpTextureCorrection", 1) != 0);
    si.SetBoolValue("GPU", "PGXPColorCorrection", pgxp && wbx_setting_bool("pgxpColorCorrection", 0) != 0);
    si.SetBoolValue("GPU", "PGXPDepthBuffer", pgxp && wbx_setting_bool("pgxpDepthBuffer", 0) != 0);
    si.SetBoolValue("GPU", "PGXPCPU", pgxp && wbx_setting_bool("pgxpCpu", 0) != 0);
    si.SetBoolValue("GPU", "PGXPVertexCache", false);
    si.SetBoolValue("GPU", "PGXPPreserveProjFP", false);
    si.SetBoolValue("GPU", "WidescreenHack", false);

    // How much of the raster is handed over. It is the picture's edge and
    // nothing the machine can see.
    {
      char v[16] = "overscan";
      wbx_setting_str("crop", v, sizeof(v));
      si.SetStringValue("Display", "CropMode",
                        strcmp(v, "none") == 0 ? "None" : (strcmp(v, "borders") == 0 ? "Borders" : "Overscan"));
    }
    si.SetIntValue("GPU", "ResolutionScale", g_scale);
  }
  si.SetBoolValue("GPU", "UseThread", false);
  si.SetBoolValue("GPU", "DisableShaderCache", true);

  // Nothing is read ahead, and nothing is read on another thread.
  si.SetIntValue("CDROM", "ReadaheadSectors", 0);
  si.SetBoolValue("CDROM", "LoadImageToRAM", false);

  // The interpreter until the code generators are a milestone of their own.
  si.SetStringValue("CPU", "ExecutionMode", Settings::GetCPUExecutionModeName(CPUExecutionMode::Interpreter));
  si.SetStringValue("CPU", "FastmemMode", Settings::GetCPUFastmemModeName(CPUFastmemMode::Disabled));

  // Interlace. A console in an interlaced mode draws only the lines of the
  // field it is not showing, and shows one field a frame; upstream's default
  // is to pretend it does neither ("DisableInterlacing"), which is a sharper
  // picture and a different machine - what is in VRAM is not what a
  // PlayStation would have put there. The console's own behaviour is the
  // default here and "progressive" is the enhancement, said as such.
  {
    char interlacing[16] = "weave";
    wbx_setting_str("interlacing", interlacing, sizeof(interlacing));
    si.SetBoolValue("GPU", "DisableInterlacing", strcmp(interlacing, "progressive") == 0);
  }

  // A deinterlacer and a chroma filter are shaders upstream; the frame is
  // composed here instead (ComposeFrame).
  si.SetStringValue("Display", "DeinterlacingMode",
                    Settings::GetDisplayDeinterlacingModeName(DisplayDeinterlacingMode::Disabled));
  si.SetBoolValue("Display", "24BitChromaSmoothing", false);

  // what is plugged in, and where the machine was made
  MachineApplySettings(si);

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
#ifndef CHIMERA_ARCADE
  EmuFolders::MemoryCards = "./";
#endif
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

// patches: "the drive says when it read". The console read a sector.
static int g_driveLightNow;
extern "C" void chimera_drive_read(void)
{
  g_driveLightNow = 1;
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

#ifdef CHIMERA_ARCADE
#include "machine-arcade.inc"
#else
#include "machine-console.inc"
#endif

// ---- the guest ABI, as this machine answers it ----------------------------------
//
// Not exported from here: waterbox/dispatch.c is what the frontend calls, and
// it comes through the table at the end of this file.

static const char* GetLoadError(void)
{
  return g_loadError;
}

static int Init(void)
{
  g_loadError[0] = '\0';

  g_soundOut = static_cast<int16_t*>(alloc_invisible(sizeof(int16_t) * 2 * MAX_AUDIO_FRAMES));

  SetFolders();

  g_settingsLayer = std::make_unique<MemorySettingsInterface>();
  Host::Internal::SetBaseSettingsLayer(g_settingsLayer.get());
  ApplySettings(*g_settingsLayer);

#ifndef CHIMERA_GUEST_GL
  if (g_rendererWanted != "software")
  {
    snprintf(g_loadError, sizeof(g_loadError),
             "this build of the core has no OpenGL in it, and the project asks for the '%s' renderer.",
             g_rendererWanted.c_str());
    return 0;
  }
#endif
#ifdef CHIMERA_GUEST_GL
  // (a build with no hardware renderer has refused it already, just above)
  if (g_rendererWanted == "vulkan-hw" && !OnTheCard())
  {
    snprintf(g_loadError, sizeof(g_loadError),
             "the renderer setting is vulkan-hw, and no Vulkan was handed over (this computer may have no Vulkan "
             "driver, or this Chimera is older than its Vulkan bridge).");
    return 0;
  }
#endif

  // The frame, at the scale the project asked for. It is the picture the
  // frontend is shown and nothing the machine reads, but the weave keeps the
  // other field in it from one frame to the next - so it is in the state.
#ifdef CHIMERA_GUEST_GL
  // Room for the card's VRAM, when a card is drawing (see FetchVRAMFromTheCard).
  // Taken here, during Init, and never later.
  if (OnTheCard() && g_rendererWanted != "software")
  {
    g_vramRoomWidth = 1024u * static_cast<u32>(g_scale);
#ifdef CHIMERA_ARCADE
    // a board's VRAM is 1024 lines on some of them, twice the console's
    g_vramRoomHeight = 1024u * static_cast<u32>(g_scale);
#else
    g_vramRoomHeight = 512u * static_cast<u32>(g_scale);
#endif
    const size_t bytes = static_cast<size_t>(g_vramRoomWidth) * g_vramRoomHeight * 4u;
    g_vramKept = static_cast<u8*>(std::calloc(bytes, 1));
    g_vramFetch = static_cast<u8*>(alloc_invisible(bytes));
  }
#endif

  g_videoCapacity = static_cast<size_t>(MAX_WIDTH) * MAX_HEIGHT * g_scale * g_scale;
  g_video = static_cast<uint32_t*>(std::malloc(sizeof(uint32_t) * g_videoCapacity));
  std::fill_n(g_video, g_videoCapacity, 0xFF000000u);
  EmuFolders::LoadConfig(*g_settingsLayer);
  SetFolders();

  Error error;
  if (!System::Internal::ProcessStartup(&error) || !System::Internal::CPUThreadInitialize(&error))
  {
    Fail("the machine would not initialise", error);
    return 0;
  }

  if (!MachineBoot(&error))
    return 0;

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

static void FrameAdvance(uint64_t packed)
{
  g_inputRead = 0;
  g_nsamples = 0;
  if (!g_booted)
    return;

#ifdef CHIMERA_GUEST_GL
  CheckGLContext();
#endif

  // the machine's own controls, before the frame they are held for
  MachineBeforeFrame();
  g_driveLightNow = 0;

  // One frame: Host::PumpMessagesOnCPUThread pauses the system at the end of
  // the frame the machine draws, and Execute returns when it is paused.
  System::PauseSystem(false);
  System::Execute();

  // Turbo skips the readback, never the drawing: what the renderer draws
  // stays in VRAM, and a frame not drawn is a picture lost, not deferred.
  MachineAfterFrame();
  if (g_renderEnabled)
    ComposeFrame();
  g_nsamples = ChimeraAudioTake(g_soundOut, MAX_AUDIO_FRAMES);
}

/* Told after every load of the machine - a savestate, a branch file, a
 * greenzone restore - with the machine stopped and before it runs again. The
 * one thing this core keeps that a load invalidates is its claim about which
 * GL context its objects came from. */
static void StateLoaded(void)
{
#ifdef CHIMERA_GUEST_GL
  g_glStateLoaded = true;
#endif
}

/* Told before every state the engine takes, the machine stopped at a frame's
 * end. Under a real GPU the console's VRAM is a texture on the card and in no
 * state, so it is brought into guest memory here; the rebuild after a load
 * uploads it again. Not for the softpipe, whose textures are guest memory
 * already, and not when the objects are another context's - a state taken
 * after a load and before the next frame, whose copy is the one the load
 * brought. */
static void StateSaving(void)
{
#ifdef CHIMERA_GUEST_GL
  if (!g_booted || !OnTheCard() || !HardwareDevice())
    return;
  const uint64_t live = LiveContext();
  if (live == 0 || live != g_glContext)
    return;
  g_gpu->RestoreDeviceContext();
  g_gpu->ChimeraReadBackVRAM();
  FetchVRAMFromTheCard();
#endif
}

/* The seam to a real GPU. The host registers one callback with the sandbox
 * and hands its guest-visible address here BEFORE Init, because the device is
 * made during Init and which one depends on whether a GPU is on offer. Without
 * this call - every ordinary run - the softpipe draws.
 *
 * A machine drawn this way is not the deterministic one: the GPU is outside
 * the sandbox, outside the savestate and different on every machine. */
#ifdef CHIMERA_GUEST_GL
static void SetGpuBridge(uint64_t addr)
{
  if (!chimera_gl_bridge_start(reinterpret_cast<chimera_gl_bridge_fn>(addr)))
    std::fprintf(stderr, "duckstation: the GPU bridge was offered and refused\n");
}
#endif

/* The same seam, for Vulkan: handed over before Init by a frontend whose
 * computer has a Vulkan driver. The Renderer setting decides whether it is
 * used ("vulkan-hw"). */
#ifdef CHIMERA_GUEST_VK
static void SetVulkanBridge(uint64_t addr)
{
  g_vulkanHost = reinterpret_cast<chimera_vk_bridge_fn>(addr);
  g_vulkanBridge = g_vulkanHost && chimera_vk_install(VulkanBridge);
  if (!g_vulkanBridge)
    std::fprintf(stderr, "duckstation: the Vulkan bridge was offered and refused\n");
}

/* Upstream's "recreate the device" says so when the old device has been let
 * go of (patches: "the Vulkan device is Chimera's"): the copies of mapped
 * memory nothing will unmap now are given back, and the calls cross again
 * for the device about to be made. */
extern "C" void ChimeraOldDeviceGone(void)
{
  if (!g_vulkanQuiet)
    return;
  chimera_vk_forget_mappings();
  g_vulkanQuiet = false;
}
#endif

/* For the gate, not for the frontend: where the machine is, as one number.
 * Its clock and where its processor is about to execute, which two runs that
 * have parted company cannot share for long. */
static uint64_t ChimeraMachineDigest(void)
{
  uint64_t h = static_cast<uint64_t>(TimingEvents::GetGlobalTickCounter());
  h = (h * UINT64_C(1099511628211)) ^ CPU::g_state.pc;
  h = (h * UINT64_C(1099511628211)) ^ CPU::g_state.npc;
  for (u32 i = 0; i < static_cast<u32>(CPU::Reg::count); i++)
    h = (h * UINT64_C(1099511628211)) ^ CPU::g_state.regs.r[i];
  return h;
}

static void SetRenderingEnabled(int on)
{
  g_renderEnabled = (on != 0);
}

static uint32_t* GetVideoBgra(void)
{
  return g_video;
}

static int GetVideoWidth(void)
{
  return g_videoWidth;
}
static int GetVideoHeight(void)
{
  return g_videoHeight;
}

static int16_t* GetAudio(void)
{
  return g_soundOut;
}
static int GetAudioSampleCount(void)
{
  return g_nsamples;
}

static int GetVsyncNumerator(void)
{
  return g_vsyncNum;
}
static int GetVsyncDenominator(void)
{
  return g_vsyncDen;
}

static int InputWasRead(void)
{
  return g_inputRead;
}

// ---- memory domains ---------------------------------------------------------

static int GetMemoryDomainCount(void)
{
  return 2;
}

static const char* GetMemoryDomainName(int which)
{
  static const char* const names[] = {"Main RAM", "BIOS ROM"};
  return (which >= 0 && which < 2) ? names[which] : "";
}

static uint8_t* GetMemoryDomainPtr(int which)
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

static int64_t GetMemoryDomainSize(int which)
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

static int GetMemoryDomainWritable(int which)
{
  return which == 0;
}

// ---- the table --------------------------------------------------------------------

extern "C" {
#ifdef CHIMERA_ARCADE
extern const ChimeraMachine chimera_machine_arcade = {
#else
extern const ChimeraMachine chimera_machine_console = {
#endif
  GetLoadError,
  Init,
  SetButton,
  IsButtonActive,
  IsAxisActive,
  SetAxis,
  FrameAdvance,
  StateLoaded,
  StateSaving,
#ifdef CHIMERA_GUEST_GL
  SetGpuBridge,
#else
  nullptr,
#endif
#ifdef CHIMERA_GUEST_VK
  SetVulkanBridge,
#else
  nullptr,
#endif
  ChimeraMachineDigest,
  SetRenderingEnabled,
  GetVideoBgra,
  GetVideoWidth,
  GetVideoHeight,
  GetAudio,
  GetAudioSampleCount,
  GetVsyncNumerator,
  GetVsyncDenominator,
  InputWasRead,
  GetMemoryDomainCount,
  GetMemoryDomainName,
  GetMemoryDomainPtr,
  GetMemoryDomainSize,
  GetMemoryDomainWritable,
  GetSaveDataFileCount,
  GetSaveDataFileName,
  GetSaveDataFileSize,
  GetSaveDataFileBuffer,
  GetDriveCount,
  GetDriveName,
  GetDriveLight,
  GetDriveMediaCount,
  GetDriveMediaName,
  GetDriveMediaSelected,
  GetDriveMediaInserted,
};
}

