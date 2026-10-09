// The guest's end of the GPU bridge: the context DuckStation's GL device
// thinks it is drawing into, when the drawing is actually happening on a real
// GPU outside the sandbox.
//
// There is no driver on this side. Every entry point the device calls is a
// generated wrapper that packs its arguments and hands them to the host
// (miniBox, source/gl/gl-bridge.h says why that is the only shape available).
// What this file adds is the object upstream asks for a context: something
// that answers GetProcAddress, reports a version, and has nothing to swap
// into.
//
// The device itself is unmodified but for one thing (patch 0009): it may not
// map a driver buffer, because a pointer into the driver is a pointer a
// sandboxed guest cannot follow. It loads its entry points through glad over
// this object's GetProcAddress, exactly as on a desktop - so glad asks the
// DRIVER what version and extensions it has, through the bridge, and what
// DuckStation believes about the GPU is what the GPU said.
//
// WHAT THIS COSTS: the GPU is outside the sandbox. It is outside the
// savestate, outside this core's determinism, and different on every machine.
// A run recorded this way is not guaranteed to replay anywhere.

#include "util/opengl_context.h"
#include "util/opengl_loader.h"

#include "common/error.h"

#include "gl-bridge.h" // miniBox source/gl: the shared contract

#include <memory>

#include <cstring>

namespace {

chimera_gl_bridge_fn g_bridge;

// glGetString, with the lifetime OpenGL promises. A driver's strings are
// static: a caller may ask for the vendor, then the renderer, then the
// version, and read all three afterwards - upstream does exactly that, and
// then decides which driver workarounds to apply from what it read. The
// generated wrapper copies each answer into ONE buffer, so the three pointers
// were one pointer and every string was the last one asked for ("GL_VENDOR:
// 4.50", seen on the first bridged run). Each name gets its own here.
using GetStringFn = const GLubyte*(GLAD_API_PTR*)(GLenum);
GetStringFn g_bridge_get_string;

const GLubyte* GLAD_API_PTR StableGetString(GLenum name)
{
  static char strings[5][4096];
  int slot;
  switch (name)
  {
    case GL_VENDOR:
      slot = 0;
      break;
    case GL_RENDERER:
      slot = 1;
      break;
    case GL_VERSION:
      slot = 2;
      break;
    case GL_SHADING_LANGUAGE_VERSION:
      slot = 3;
      break;
    case GL_EXTENSIONS:
      slot = 4;
      break;
    default:
      return g_bridge_get_string(name);
  }

  const GLubyte* const answer = g_bridge_get_string(name);
  if (!answer)
    return nullptr;
  std::strncpy(strings[slot], reinterpret_cast<const char*>(answer), sizeof(strings[slot]) - 1);
  strings[slot][sizeof(strings[slot]) - 1] = '\0';
  return reinterpret_cast<const GLubyte*>(strings[slot]);
}

class ChimeraBridgedGLContext final : public OpenGLContext
{
public:
  explicit ChimeraBridgedGLContext(const WindowInfo& wi) : OpenGLContext(wi) {}

  bool Create()
  {
    // What upstream is told before glad runs and asks the driver itself.
    m_version.profile = Profile::Core;
    m_version.major_version = 3;
    m_version.minor_version = 3;
    return true;
  }

  // Every name resolves to a wrapper, or to null for a call this core was
  // not built with - which is what a driver answers for a call it lacks.
  void* GetProcAddress(const char* name) override
  {
    void* const wrapper = chimera_gl_lookup(name);
    if (wrapper && std::strcmp(name, "glGetString") == 0)
    {
      g_bridge_get_string = reinterpret_cast<GetStringFn>(wrapper);
      return reinterpret_cast<void*>(&StableGetString);
    }
    return wrapper;
  }

  bool ChangeSurface(const WindowInfo& new_wi) override
  {
    m_wi = new_wi;
    return true;
  }

  void ResizeSurface(u32 new_surface_width, u32 new_surface_height) override
  {
    if (new_surface_width != 0)
      m_wi.surface_width = new_surface_width;
    if (new_surface_height != 0)
      m_wi.surface_height = new_surface_height;
  }

  // Nothing to swap into: the frontend takes the finished frame through the
  // ABI, as it does from every other renderer this core has.
  bool SwapBuffers() override { return true; }

  // The host made the context current on its own thread before handing the
  // guest the callback, and there is only one of each here.
  bool IsCurrent() const override { return true; }
  bool MakeCurrent() override { return true; }
  bool DoneCurrent() override { return true; }

  bool SupportsNegativeSwapInterval() const override { return false; }
  bool SetSwapInterval(s32 interval) override { return true; }

  std::unique_ptr<OpenGLContext> CreateSharedContext(const WindowInfo& wi, Error* error) override
  {
    Error::SetStringView(error, "a sandboxed core has one thread and one context");
    return nullptr;
  }
};

} // namespace

// Whether a host offered a GPU. Read by the device, which must not take the
// persistent-mapping paths when it did (patch 0009), and by the context
// factory (patch 0008).
extern "C" bool chimera_gl_bridged()
{
  return g_bridge != nullptr;
}

// Called before Init when the host has a context to offer. Installing the
// wrappers here means the device finds them the moment it asks.
extern "C" bool chimera_gl_bridge_start(chimera_gl_bridge_fn bridge)
{
  if (!bridge)
    return false;

  if (!chimera_gl_install(bridge))
    return false; // an older host; the softpipe draws instead

  g_bridge = bridge;
  return true;
}

std::unique_ptr<OpenGLContext> ChimeraCreateBridgedGLContext(const WindowInfo& wi, Error* error)
{
  if (!g_bridge)
  {
    Error::SetStringView(error, "no GPU bridge was offered");
    return nullptr;
  }

  std::unique_ptr<ChimeraBridgedGLContext> context = std::make_unique<ChimeraBridgedGLContext>(wi);
  if (!context->Create())
  {
    Error::SetStringView(error, "the GPU bridge would not start");
    return nullptr;
  }

  return context;
}
