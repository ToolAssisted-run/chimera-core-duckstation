// An OpenGL that lives inside the sandbox.
//
// DuckStation's hardware renderer is the reason this core exists: internal
// resolution, PGXP, true colour, texture filtering are all its. It was
// unreachable in a core because a core has no GPU and no driver - so this
// build gives it one that is neither. Mesa's softpipe, compiled into the
// guest, IS the OpenGL implementation: plain C, no JIT, no dispatch on the
// host's CPU, so every pixel is decided by code this repository compiled and
// is the same on every machine. waterbox/setup-mesa.sh builds it.
//
// OSMesa is the front end that renders into a memory buffer instead of a
// window, which is the shape a core wants: there is nothing to swap into, and
// the frontend takes the finished frame through the ABI.

#include "util/opengl_context.h"
#include "util/opengl_loader.h"

#include "common/error.h"

#include <algorithm>
#include <cstdio>
#include <vector>

// OSMesa's entry points, declared rather than included: <GL/osmesa.h> pulls in
// Mesa's own <GL/gl.h>, and this file already has glad's, which declares the
// same types and the same several thousand enums.
extern "C" {
typedef struct osmesa_context* OSMesaContext;
typedef void (*OSMESAproc)();

OSMesaContext OSMesaCreateContextExt(GLenum format, GLint depthBits, GLint stencilBits, GLint accumBits,
                                     OSMesaContext sharelist);
void OSMesaDestroyContext(OSMesaContext ctx);
GLboolean OSMesaMakeCurrent(OSMesaContext ctx, void* buffer, GLenum type, GLsizei width, GLsizei height);
OSMESAproc OSMesaGetProcAddress(const char* funcName);
}

#define OSMESA_RGBA 0x1908

namespace {

class ChimeraGLContext final : public OpenGLContext
{
public:
  explicit ChimeraGLContext(const WindowInfo& wi) : OpenGLContext(wi) {}

  ~ChimeraGLContext() override
  {
    if (m_context != nullptr)
      OSMesaDestroyContext(m_context);
  }

  bool Create()
  {
    // The default framebuffer OSMesa hands out. The renderer draws into
    // render targets of its own and the frame is read from one of those, so
    // this only has to exist.
    m_width = std::max<u32>(m_wi.surface_width, 640);
    m_height = std::max<u32>(m_wi.surface_height, 480);
    m_surface.resize(static_cast<size_t>(m_width) * m_height * 4);

    // 24 bit depth and 8 bit stencil: the renderer asks for both.
    m_context = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, nullptr);
    if (m_context == nullptr)
      return false;

    if (!OSMesaMakeCurrent(m_context, m_surface.data(), GL_UNSIGNED_BYTE, static_cast<GLsizei>(m_width),
                           static_cast<GLsizei>(m_height)))
    {
      return false;
    }

    // what softpipe is: desktop GL, 3.3 core once a context asks for it
    m_version.profile = Profile::Core;
    m_version.major_version = 3;
    m_version.minor_version = 3;
    return true;
  }

  void* GetProcAddress(const char* name) override { return reinterpret_cast<void*>(OSMesaGetProcAddress(name)); }

  bool ChangeSurface(const WindowInfo& new_wi) override
  {
    m_wi = new_wi;
    return MakeCurrent();
  }

  void ResizeSurface(u32 new_surface_width, u32 new_surface_height) override
  {
    if (new_surface_width != 0)
      m_wi.surface_width = new_surface_width;
    if (new_surface_height != 0)
      m_wi.surface_height = new_surface_height;
  }

  // There is nothing to swap into.
  bool SwapBuffers() override { return true; }

  bool IsCurrent() const override { return m_context != nullptr; }

  bool MakeCurrent() override
  {
    return m_context != nullptr && OSMesaMakeCurrent(m_context, m_surface.data(), GL_UNSIGNED_BYTE,
                                                     static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
  }

  bool DoneCurrent() override { return true; }

  bool SupportsNegativeSwapInterval() const override { return false; }

  // Nothing is being waited for: there is no display and no vertical blank.
  bool SetSwapInterval(s32 interval) override { return true; }

  // One thread, one context.
  std::unique_ptr<OpenGLContext> CreateSharedContext(const WindowInfo& wi, Error* error) override
  {
    Error::SetStringView(error, "a sandboxed core has one thread and one context");
    return nullptr;
  }

private:
  OSMesaContext m_context = nullptr;
  std::vector<u8> m_surface;
  u32 m_width = 0;
  u32 m_height = 0;
};

} // namespace

std::unique_ptr<OpenGLContext> ChimeraCreateGLContext(const WindowInfo& wi, Error* error)
{
  std::unique_ptr<ChimeraGLContext> context = std::make_unique<ChimeraGLContext>(wi);
  if (!context->Create())
  {
    Error::SetStringView(error, "Mesa would not make an off-screen context");
    return nullptr;
  }
  return context;
}

// No GPU bridge yet: the softpipe always draws (waterbox/gl-bridged.cpp will
// replace these two when it exists).
extern "C" bool chimera_gl_bridged()
{
  return false;
}

std::unique_ptr<OpenGLContext> ChimeraCreateBridgedGLContext(const WindowInfo& wi, Error* error)
{
  Error::SetStringView(error, "no GPU bridge was offered");
  return nullptr;
}
