// The parts of DuckStation that talk to a person, for a core that has a
// frontend already. Every function here is one upstream's own sources call
// and this build does not compile the definition of (waterbox/sources.sh says
// which files and why). They are answered the way a desktop DuckStation with
// the feature turned off would answer.

#include "scmversion/scmversion.h"

#include "util/input_manager.h"

// What upstream's build stamps from git. A core's version is the package's
// (build-package.sh), and a string that changed with every build would make
// two builds of one commit differ.
//
// This file is compiled once for each emulator, and each says its own pin.
#ifdef CHIMERA_ARCADE
const char* g_scm_hash_str = "1e3f7ad3a3f6a96697274da9a46f68c8ac558070";
const char* g_scm_branch_str = "chimera";
const char* g_scm_tag_str = "arcadeduck-1e3f7ad";
const char* g_scm_date_str = "2026-09-27T09:18:12-0400";
#else
const char* g_scm_hash_str = "14ab642ff3d5d489d388c8062530aa2f7c9a1372";
const char* g_scm_branch_str = "chimera";
const char* g_scm_tag_str = "0.1-7463-g14ab642ff";
const char* g_scm_date_str = "2024-09-01T13:02:58+1000";
#endif

BEGIN_HOTKEY_LIST(g_common_hotkeys)
END_HOTKEY_LIST()

#include "core/achievements.h"
#include "core/fullscreen_ui.h"
#include "core/game_list.h"
#include "core/host.h"
#include "core/imgui_overlays.h"
#include "core/texture_replacements.h"

#include "util/cd_image.h"
#include "util/image.h"
#include "util/imgui_manager.h"
#include "util/input_source.h"
#include "util/media_capture.h"
#include "util/page_fault_handler.h"
#include "util/platform_misc.h"
#include "util/postprocessing.h"
#include "util/postprocessing_shader.h"

#include "common/crash_handler.h"
#include "common/error.h"

#include <cstdio>
#include <cstdlib>

// ---- achievements: a network service --------------------------------------

bool Achievements::Initialize()
{
  return false;
}
void Achievements::UpdateSettings(const Settings& old_config)
{
}
void Achievements::ResetClient()
{
}
bool Achievements::ConfirmSystemReset()
{
  return true;
}
bool Achievements::Shutdown(bool allow_cancel)
{
  return true;
}
void Achievements::OnSystemPaused(bool paused)
{
}
void Achievements::FrameUpdate()
{
}
void Achievements::IdleUpdate()
{
}
bool Achievements::DoState(StateWrapper& sw)
{
  return true;
}
void Achievements::GameChanged(const std::string& path, CDImage* image)
{
}
bool Achievements::ResetHardcoreMode(bool is_booting)
{
  return false;
}
void Achievements::DisableHardcoreMode()
{
}
bool Achievements::ConfirmHardcoreModeDisable(const char* trigger)
{
  return true;
}
void Achievements::ConfirmHardcoreModeDisableAsync(const char* trigger, std::function<void(bool)> callback)
{
  callback(true);
}
bool Achievements::IsHardcoreModeActive()
{
  return false;
}
bool Achievements::IsActive()
{
  return false;
}

// ---- the big-picture interface and the on-screen display ------------------

bool FullscreenUI::IsInitialized()
{
  return false;
}
void FullscreenUI::CheckForConfigChanges(const Settings& old_settings)
{
}
void FullscreenUI::OnSystemStarted()
{
}
void FullscreenUI::OnSystemPaused()
{
}
void FullscreenUI::OnSystemResumed()
{
}
void FullscreenUI::OnSystemDestroyed()
{
}
void FullscreenUI::Shutdown()
{
}
void FullscreenUI::Render()
{
}

bool SaveStateSelectorUI::IsOpen()
{
  return false;
}
void SaveStateSelectorUI::RefreshList(const std::string& serial)
{
}
void SaveStateSelectorUI::Clear()
{
}
void SaveStateSelectorUI::ClearList()
{
}

void ImGuiManager::RenderTextOverlays()
{
}
void ImGuiManager::RenderDebugWindows()
{
}
void ImGuiManager::RenderOverlayWindows()
{
}
void ImGuiManager::DestroyOverlayTextures()
{
}
void ImGuiManager::SetGlobalScale(float global_scale)
{
}
void ImGuiManager::SetShowOSDMessages(bool enable)
{
}
bool ImGuiManager::Initialize(float global_scale, bool show_osd_messages, Error* error)
{
  // No context is made, so nothing upstream may call into Dear ImGui itself:
  // every path that would is behind one of the functions in this file.
  return true;
}
void ImGuiManager::Shutdown()
{
}
// The window a pointer is aimed in is the unit square: a light gun's aim
// arrives as a fraction of the picture, and upstream multiplies it by these
// (patches: "a gun is aimed at the picture").
float ImGuiManager::GetWindowWidth()
{
  return 1.0f;
}
float ImGuiManager::GetWindowHeight()
{
  return 1.0f;
}
void ImGuiManager::WindowResized(float width, float height)
{
}
void ImGuiManager::NewFrame()
{
}
void ImGuiManager::RenderOSDMessages()
{
}
float ImGuiManager::GetGlobalScale()
{
  return 1.0f;
}
void ImGuiManager::UpdateMousePosition(float x, float y)
{
}
bool ImGuiManager::ProcessPointerButtonEvent(InputBindingKey key, float value)
{
  return false;
}
bool ImGuiManager::ProcessPointerAxisEvent(InputBindingKey key, float value)
{
  return false;
}
bool ImGuiManager::ProcessHostKeyEvent(InputBindingKey key, float value)
{
  return false;
}
bool ImGuiManager::ProcessGenericInputEvent(GenericInputBinding key, float value)
{
  return false;
}
void ImGuiManager::SetSoftwareCursor(u32 index, std::string image_path, float image_scale, u32 multiply_color)
{
}
bool ImGuiManager::HasSoftwareCursor(u32 index)
{
  return false;
}
void ImGuiManager::ClearSoftwareCursor(u32 index)
{
}
void ImGuiManager::SetSoftwareCursorPosition(u32 index, float pos_x, float pos_y)
{
}
void ImGuiManager::RenderSoftwareCursors()
{
}

// What upstream would have written on the screen is said on stderr when the
// project asks for it (the "verbose" setting turns the log on), and nowhere
// otherwise: a message drawn into the picture would be in every screenshot
// and every dump, and a movie's picture is the machine's.
extern "C" int chimera_verbose;
int chimera_verbose = 0;

void Host::AddOSDMessage(std::string message, float duration)
{
  if (chimera_verbose)
    std::fprintf(stderr, "duckstation: %s\n", message.c_str());
}
void Host::AddKeyedOSDMessage(std::string key, std::string message, float duration)
{
  if (chimera_verbose)
    std::fprintf(stderr, "duckstation: %s\n", message.c_str());
}
void Host::AddIconOSDMessage(std::string key, const char* icon, std::string message, float duration)
{
  if (chimera_verbose)
    std::fprintf(stderr, "duckstation: %s\n", message.c_str());
}
void Host::RemoveKeyedOSDMessage(std::string key)
{
}
void Host::ClearOSDMessages()
{
}
void Host::DisplayLoadingScreen(const char* message, int progress_min, int progress_max, int progress_value)
{
}

// ---- a library of somebody's games -----------------------------------------

bool GameList::IsGameListLoaded()
{
  return false;
}
void GameList::AddPlayedTimeForSerial(const std::string& serial, std::time_t last_time, std::time_t add_time)
{
}
std::string GameList::GetCustomTitleForPath(const std::string_view path)
{
  return {};
}
#ifndef CHIMERA_ARCADE
std::optional<DiscRegion> GameList::GetCustomRegionForPath(const std::string_view path)
{
  return std::nullopt;
}
#endif

// ---- pictures swapped in from a host folder ---------------------------------

void TextureReplacements::SetGameID(std::string game_id)
{
}
void TextureReplacements::Reload()
{
}
const TextureReplacements::ReplacementImage* TextureReplacements::GetVRAMReplacement(u32 width, u32 height,
                                                                                    const void* pixels)
{
  return nullptr;
}
void TextureReplacements::DumpVRAMWrite(u32 width, u32 height, const void* pixels)
{
}
void TextureReplacements::Shutdown()
{
}

// ---- filters between the frame and a window ---------------------------------

PostProcessing::Chain PostProcessing::DisplayChain(PostProcessing::Config::DISPLAY_CHAIN_SECTION);
PostProcessing::Chain PostProcessing::InternalChain(PostProcessing::Config::INTERNAL_CHAIN_SECTION);

PostProcessing::Chain::Chain(const char* section) : m_section(section)
{
}
PostProcessing::Chain::~Chain() = default;
bool PostProcessing::Chain::IsActive() const
{
  return false;
}
bool PostProcessing::Chain::CheckTargets(GPUTexture::Format target_format, u32 target_width, u32 target_height,
                                         ProgressCallback* progress)
{
  return false;
}
bool PostProcessing::Chain::Apply(GPUTexture* input_color, GPUTexture* input_depth, GPUTexture* final_target,
                                  const GSVector4i final_rect, s32 orig_width, s32 orig_height, s32 native_width,
                                  s32 native_height)
{
  return false;
}
void PostProcessing::Initialize()
{
}
void PostProcessing::UpdateSettings()
{
}
void PostProcessing::Shutdown()
{
}

// ---- a recording of the screen: the frontend makes those ---------------------

std::optional<MediaCaptureBackend> MediaCapture::ParseBackendName(const char* str)
{
  return std::nullopt;
}
const char* MediaCapture::GetBackendName(MediaCaptureBackend backend)
{
  return "";
}
void MediaCapture::AdjustVideoSize(u32* width, u32* height)
{
}
std::unique_ptr<MediaCapture> MediaCapture::Create(MediaCaptureBackend backend, Error* error)
{
  Error::SetStringView(error, "the frontend records, not the core");
  return {};
}

// ---- pictures as files: no codec is linked ------------------------------------

RGBA8Image::RGBA8Image() = default;
RGBA8Image::RGBA8Image(u32 width, u32 height) : Image(width, height)
{
}
RGBA8Image::RGBA8Image(u32 width, u32 height, const u32* pixels) : Image(width, height, pixels)
{
}
RGBA8Image::RGBA8Image(u32 width, u32 height, std::vector<u32> pixels) : Image(width, height, std::move(pixels))
{
}
RGBA8Image::RGBA8Image(const RGBA8Image& copy) : Image(copy)
{
}
RGBA8Image::RGBA8Image(RGBA8Image&& move) : Image(std::move(move))
{
}
RGBA8Image& RGBA8Image::operator=(const RGBA8Image& copy)
{
  Image<u32>::operator=(copy);
  return *this;
}
RGBA8Image& RGBA8Image::operator=(RGBA8Image&& move)
{
  Image<u32>::operator=(std::move(move));
  return *this;
}
bool RGBA8Image::LoadFromFile(const char* filename)
{
  return false;
}
bool RGBA8Image::LoadFromFile(std::string_view filename, std::FILE* fp)
{
  return false;
}
bool RGBA8Image::LoadFromBuffer(std::string_view filename, const void* buffer, size_t buffer_size)
{
  return false;
}
bool RGBA8Image::SaveToFile(const char* filename, u8 quality) const
{
  return false;
}
bool RGBA8Image::SaveToFile(std::string_view filename, std::FILE* fp, u8 quality) const
{
  return false;
}
std::optional<std::vector<u8>> RGBA8Image::SaveToBuffer(std::string_view filename, u8 quality) const
{
  return std::nullopt;
}

// ---- the desktop ------------------------------------------------------------------

void PlatformMisc::SuspendScreensaver()
{
}
void PlatformMisc::ResumeScreensaver()
{
}
size_t PlatformMisc::GetRuntimePageSize()
{
  // the sandbox's, and every x86-64 host's
  return 4096;
}

bool PageFaultHandler::Install(Error* error)
{
  // Nothing is installed because nothing may fault on purpose: fastmem by
  // mmap is not built (meson.build never defines ENABLE_MMAP_FASTMEM).
  return true;
}

void CrashHandler::WriteDumpForCaller()
{
}

bool CDImage::IsDeviceName(const char* filename)
{
  return false;
}
std::unique_ptr<CDImage> CDImage::OpenDeviceImage(const char* filename, Error* error)
{
  Error::SetStringView(error, "a sandbox has no disc drive");
  return {};
}

std::unique_ptr<InputSource> InputSource::CreateSDLSource()
{
  return {};
}
