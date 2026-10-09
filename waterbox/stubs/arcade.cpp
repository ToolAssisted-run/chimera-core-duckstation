// What ArcadeDuck asks a desktop for, answered for a sandbox. Compiled only
// into the arcade boards' half of the core (meson.build, machine_sources).
//
//   a cabinet's outputs    lamps, coin counters and recoil are told to
//                          whoever listens on a socket or a window message.
//                          Nobody does.
//   a link cable           Tecmo's Gallop Racer cabinets were wired to each
//                          other, which ArcadeDuck does over TCP. A sandbox
//                          has no sockets, so the cable is unplugged: every
//                          call that would open one says no, and the board
//                          runs alone, as a lone cabinet did.
//   a crosshair, a bezel   pictures drawn over the frame for a person aiming
//                          a mouse. The frontend draws its own.
//   a library of games     ArcadeDuck asks its game list for a title's
//                          settings. There is no list.

#include "core/arcade/arcade_output_network.h"
#include "core/arcade/arcade_output_win32.h"
#include "core/game_list.h"
#include "scmversion/scmversion.h"
#include "util/imgui_manager.h"
#include "util/sockets.h"

#include "common/error.h"

#include <mutex>

// ---- the build's name: a package is its commit, and says so itself ------------

const char* g_scm_version_id_str = "chimera";
const char* g_scm_version_str = "chimera";

// ---- a cabinet's outputs --------------------------------------------------------

void Arcade::Output::NetworkTransport::Start(std::string_view game_id)
{
}
void Arcade::Output::NetworkTransport::Stop()
{
}
void Arcade::Output::NetworkTransport::Notify(std::string_view name, s32 value)
{
}
void Arcade::Output::Win32Transport::Start(std::string_view game_id)
{
}
void Arcade::Output::Win32Transport::Stop()
{
}
void Arcade::Output::Win32Transport::Notify(std::string_view name, s32 value)
{
}

// ---- pictures over the frame ------------------------------------------------------

void ImGuiManager::RenderBezelOverlay(bool enabled, const std::string& image_path)
{
}
void ImGuiManager::SetArcadeCrosshairImage(u32 index, std::string image_path)
{
}
bool ImGuiManager::DrawArcadeCrosshairImage(u32 index, float left, float top, float right, float bottom)
{
  return false;
}
std::pair<u32, u32> ImGuiManager::GetArcadeCrosshairImageSize(u32 index)
{
  return {0u, 0u};
}

// ---- a library of games -----------------------------------------------------------

static std::recursive_mutex s_no_list;

std::unique_lock<std::recursive_mutex> GameList::GetLock()
{
  return std::unique_lock<std::recursive_mutex>(s_no_list);
}
const GameList::Entry* GameList::GetEntryForPath(std::string_view path)
{
  return nullptr;
}

// ---- sockets: there are none ------------------------------------------------------

std::optional<SocketAddress> SocketAddress::Parse(Type type, const char* address, u32 port, Error* error)
{
  Error::SetStringView(error, "a sandbox has no sockets");
  return std::nullopt;
}
SmallString SocketAddress::ToString() const
{
  return SmallString();
}

BaseSocket::BaseSocket(SocketMultiplexer& multiplexer, SocketDescriptor descriptor)
  : m_multiplexer(multiplexer), m_descriptor(descriptor)
{
}
BaseSocket::~BaseSocket() = default;

std::shared_ptr<ListenSocket> SocketMultiplexer::InternalCreateListenSocket(const SocketAddress& address,
                                                                            CreateStreamSocketCallback callback,
                                                                            Error* error)
{
  Error::SetStringView(error, "a sandbox has no sockets");
  return {};
}
std::shared_ptr<StreamSocket> SocketMultiplexer::InternalConnectStreamSocket(const SocketAddress& address,
                                                                              CreateStreamSocketCallback callback,
                                                                              Error* error)
{
  Error::SetStringView(error, "a sandbox has no sockets");
  return {};
}

void ListenSocket::Close()
{
}

StreamSocket::StreamSocket(SocketMultiplexer& multiplexer, SocketDescriptor descriptor)
  : BaseSocket(multiplexer, descriptor)
{
  m_connected = false;
}
StreamSocket::~StreamSocket() = default;
void StreamSocket::Close()
{
}
bool StreamSocket::SetNagleBuffering(bool enabled, Error* error)
{
  return false;
}
void StreamSocket::OnReadEvent()
{
}
void StreamSocket::OnWriteEvent()
{
}
void StreamSocket::OnHangupEvent()
{
}

BufferedStreamSocket::BufferedStreamSocket(SocketMultiplexer& multiplexer, SocketDescriptor descriptor,
                                           size_t receive_buffer_size, size_t send_buffer_size)
  : StreamSocket(multiplexer, descriptor)
{
}
BufferedStreamSocket::~BufferedStreamSocket() = default;
std::span<const u8> BufferedStreamSocket::AcquireReadBuffer() const
{
  return {};
}
void BufferedStreamSocket::ReleaseReadBuffer(size_t bytes_consumed)
{
}
std::span<u8> BufferedStreamSocket::AcquireWriteBuffer(size_t wanted_bytes, bool allow_smaller)
{
  return {};
}
void BufferedStreamSocket::ReleaseWriteBuffer(size_t bytes_written, bool commit)
{
}
void BufferedStreamSocket::Close()
{
}
void BufferedStreamSocket::OnReadEvent()
{
}
void BufferedStreamSocket::OnWriteEvent()
{
}
void BufferedStreamSocket::OnWrite()
{
}
