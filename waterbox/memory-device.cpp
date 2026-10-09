// A graphics device made of memory.
//
// DuckStation's software renderer draws into the console's VRAM, an array of
// its own - and then, to SHOW it, converts the visible rectangle into a
// texture on a GPU device, because on a desktop the last step is always a
// window. A core has no window. This is the device that step is given: its
// textures are bytes, uploading to one is a copy, and the adapter reads the
// display texture back as the frame (ChimeraDisplayBits).
//
// Everything a GPU would be asked to DRAW is accepted and not done: shaders
// and pipelines are tokens, a draw call returns. That is safe for the software
// renderer because it never draws through the device - with two exceptions it
// does when asked, deinterlacing and 24-bit chroma smoothing, both of which
// upstream implements as shaders. Those are done by the adapter instead
// (cinterface.cpp), on the bytes, so this device never has to pretend.
//
// It is NOT a null device for the hardware renderer: that renderer's machine
// state is on the GPU, and it gets a real OpenGL (see docs/PLAN.md).

#include "util/gpu_device.h"
#include "util/gpu_texture.h"

#include "common/error.h"

#include <cstdlib>
#include <cstring>

namespace {

class MemorySampler final : public GPUSampler
{
public:
  void SetDebugName(std::string_view name) override {}
};

class MemoryShader final : public GPUShader
{
public:
  explicit MemoryShader(GPUShaderStage stage) : GPUShader(stage) {}
  void SetDebugName(std::string_view name) override {}
};

class MemoryPipeline final : public GPUPipeline
{
public:
  void SetDebugName(std::string_view name) override {}
};

class MemoryTextureBuffer final : public GPUTextureBuffer
{
public:
  MemoryTextureBuffer(Format format, u32 size_in_elements) : GPUTextureBuffer(format, size_in_elements)
  {
    m_data = static_cast<u8*>(std::calloc(GetSizeInBytes(), 1));
  }
  ~MemoryTextureBuffer() override { std::free(m_data); }

  void* Map(u32 required_elements) override
  {
    if (m_current_position + required_elements > m_size_in_elements)
      m_current_position = 0;
    return m_data + static_cast<size_t>(m_current_position) * GetElementSize(m_format);
  }
  void Unmap(u32 used_elements) override { m_current_position += used_elements; }
  void SetDebugName(std::string_view name) override {}

private:
  u8* m_data = nullptr;
};

class MemoryTexture final : public GPUTexture
{
public:
  MemoryTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples, Type type, Format format)
    : GPUTexture(static_cast<u16>(width), static_cast<u16>(height), static_cast<u8>(layers), static_cast<u8>(levels),
                 static_cast<u8>(samples), type, format)
  {
    // Only the first level of the first layer is kept. Nothing reads another:
    // the software renderer's display texture has one of each.
    m_pitch = width * GetPixelSize(format);
    m_data = static_cast<u8*>(std::calloc(static_cast<size_t>(m_pitch) * height, 1));
  }
  ~MemoryTexture() override { std::free(m_data); }

  bool Update(u32 x, u32 y, u32 width, u32 height, const void* data, u32 pitch, u32 layer, u32 level) override
  {
    if (layer != 0 || level != 0)
      return true;
    if ((x + width) > m_width || (y + height) > m_height)
      return false;

    const u32 pixel_size = GetPixelSize(m_format);
    const u8* src = static_cast<const u8*>(data);
    u8* dst = m_data + static_cast<size_t>(y) * m_pitch + static_cast<size_t>(x) * pixel_size;
    for (u32 row = 0; row < height; row++)
    {
      std::memcpy(dst, src, static_cast<size_t>(width) * pixel_size);
      src += pitch;
      dst += m_pitch;
    }
    m_state = State::Dirty;
    return true;
  }

  bool Map(void** map, u32* map_stride, u32 x, u32 y, u32 width, u32 height, u32 layer, u32 level) override
  {
    if (layer != 0 || level != 0 || (x + width) > m_width || (y + height) > m_height)
      return false;

    *map = m_data + static_cast<size_t>(y) * m_pitch + static_cast<size_t>(x) * GetPixelSize(m_format);
    *map_stride = m_pitch;
    m_state = State::Dirty;
    return true;
  }

  void Unmap() override {}
  void SetDebugName(std::string_view name) override {}

  const u8* Bits() const { return m_data; }
  u8* Bits() { return m_data; }
  u32 Pitch() const { return m_pitch; }

private:
  u8* m_data = nullptr;
  u32 m_pitch = 0;
};

class MemoryDownloadTexture final : public GPUDownloadTexture
{
public:
  MemoryDownloadTexture(u32 width, u32 height, GPUTexture::Format format, void* memory, u32 memory_stride)
    : GPUDownloadTexture(width, height, format, memory != nullptr)
  {
    if (memory)
    {
      m_data = static_cast<u8*>(memory);
      m_current_pitch = memory_stride;
    }
    else
    {
      m_current_pitch = width * GPUTexture::GetPixelSize(format);
      m_data = static_cast<u8*>(std::calloc(static_cast<size_t>(m_current_pitch) * height, 1));
      m_owned = true;
    }
    m_map_pointer = m_data;
  }
  ~MemoryDownloadTexture() override
  {
    if (m_owned)
      std::free(m_data);
  }

  void CopyFromTexture(u32 dst_x, u32 dst_y, GPUTexture* src, u32 src_x, u32 src_y, u32 width, u32 height,
                       u32 src_layer, u32 src_level, bool use_transfer_pitch) override
  {
    const MemoryTexture* const tex = static_cast<const MemoryTexture*>(src);
    if (!tex || src_layer != 0 || src_level != 0 || tex->GetFormat() != m_format)
      return;
    if ((src_x + width) > tex->GetWidth() || (src_y + height) > tex->GetHeight() || (dst_x + width) > m_width ||
        (dst_y + height) > m_height)
    {
      return;
    }

    const u32 pixel_size = GPUTexture::GetPixelSize(m_format);
    for (u32 row = 0; row < height; row++)
    {
      std::memcpy(m_data + static_cast<size_t>(dst_y + row) * m_current_pitch + static_cast<size_t>(dst_x) * pixel_size,
                  tex->Bits() + static_cast<size_t>(src_y + row) * tex->Pitch() + static_cast<size_t>(src_x) * pixel_size,
                  static_cast<size_t>(width) * pixel_size);
    }
  }

  bool Map(u32 x, u32 y, u32 width, u32 height) override { return true; }
  void Unmap() override {}
  void Flush() override {}
  void SetDebugName(std::string_view name) override {}

private:
  u8* m_data = nullptr;
  bool m_owned = false;
};

class MemoryDevice final : public GPUDevice
{
public:
  MemoryDevice() = default;
  ~MemoryDevice() override = default;

  RenderAPI GetRenderAPI() const override { return RenderAPI::None; }

  bool HasSurface() const override { return false; }
  void DestroySurface() override {}
  bool UpdateWindow() override { return true; }
  void ResizeWindow(s32 new_window_width, s32 new_window_height, float new_window_scale) override {}
  std::string GetDriverInfo() const override { return "Chimera: textures in memory, nothing drawn"; }
  void ExecuteAndWaitForGPUIdle() override {}

  std::unique_ptr<GPUTexture> CreateTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples,
                                            GPUTexture::Type type, GPUTexture::Format format, const void* data,
                                            u32 data_stride) override
  {
    if (!GPUTexture::ValidateConfig(width, height, layers, levels, samples, type, format))
      return {};

    std::unique_ptr<MemoryTexture> tex =
      std::make_unique<MemoryTexture>(width, height, layers, levels, samples, type, format);
    if (!tex->Bits())
      return {};
    if (data)
      tex->Update(0, 0, width, height, data, data_stride, 0, 0);
    return tex;
  }

  std::unique_ptr<GPUSampler> CreateSampler(const GPUSampler::Config& config) override
  {
    return std::make_unique<MemorySampler>();
  }

  std::unique_ptr<GPUTextureBuffer> CreateTextureBuffer(GPUTextureBuffer::Format format, u32 size_in_elements) override
  {
    return std::make_unique<MemoryTextureBuffer>(format, size_in_elements);
  }

  std::unique_ptr<GPUDownloadTexture> CreateDownloadTexture(u32 width, u32 height, GPUTexture::Format format) override
  {
    return std::make_unique<MemoryDownloadTexture>(width, height, format, nullptr, 0);
  }

  std::unique_ptr<GPUDownloadTexture> CreateDownloadTexture(u32 width, u32 height, GPUTexture::Format format,
                                                            void* memory, size_t memory_size,
                                                            u32 memory_stride) override
  {
    return std::make_unique<MemoryDownloadTexture>(width, height, format, memory, memory_stride);
  }

  void CopyTextureRegion(GPUTexture* dst, u32 dst_x, u32 dst_y, u32 dst_layer, u32 dst_level, GPUTexture* src,
                         u32 src_x, u32 src_y, u32 src_layer, u32 src_level, u32 width, u32 height) override
  {
    MemoryTexture* const d = static_cast<MemoryTexture*>(dst);
    const MemoryTexture* const s = static_cast<const MemoryTexture*>(src);
    if (!d || !s || dst_layer != 0 || dst_level != 0 || src_layer != 0 || src_level != 0 ||
        d->GetFormat() != s->GetFormat())
    {
      return;
    }
    if ((src_x + width) > s->GetWidth() || (src_y + height) > s->GetHeight() || (dst_x + width) > d->GetWidth() ||
        (dst_y + height) > d->GetHeight())
    {
      return;
    }

    const u32 pixel_size = d->GetPixelSize();
    for (u32 row = 0; row < height; row++)
    {
      std::memmove(d->Bits() + static_cast<size_t>(dst_y + row) * d->Pitch() + static_cast<size_t>(dst_x) * pixel_size,
                   s->Bits() + static_cast<size_t>(src_y + row) * s->Pitch() + static_cast<size_t>(src_x) * pixel_size,
                   static_cast<size_t>(width) * pixel_size);
    }
  }

  void ResolveTextureRegion(GPUTexture* dst, u32 dst_x, u32 dst_y, u32 dst_layer, u32 dst_level, GPUTexture* src,
                            u32 src_x, u32 src_y, u32 width, u32 height) override
  {
  }

  std::unique_ptr<GPUPipeline> CreatePipeline(const GPUPipeline::GraphicsConfig& config, Error* error) override
  {
    return std::make_unique<MemoryPipeline>();
  }

  void PushDebugGroup(const char* name) override {}
  void PopDebugGroup() override {}
  void InsertDebugMessage(const char* msg) override {}

  // Geometry handed to a device that draws nothing still has to land
  // somewhere: the callers write through the pointer they are given.
  void MapVertexBuffer(u32 vertex_size, u32 vertex_count, void** map_ptr, u32* map_space,
                       u32* map_base_vertex) override
  {
    const size_t bytes = static_cast<size_t>(vertex_size) * vertex_count;
    if (bytes > m_scratch_size)
      GrowScratch(bytes);
    *map_ptr = m_scratch;
    *map_space = vertex_count;
    *map_base_vertex = 0;
  }
  void UnmapVertexBuffer(u32 vertex_size, u32 vertex_count) override {}
  void MapIndexBuffer(u32 index_count, DrawIndex** map_ptr, u32* map_space, u32* map_base_index) override
  {
    const size_t bytes = sizeof(DrawIndex) * static_cast<size_t>(index_count);
    if (bytes > m_scratch_size)
      GrowScratch(bytes);
    *map_ptr = static_cast<DrawIndex*>(m_scratch);
    *map_space = index_count;
    *map_base_index = 0;
  }
  void UnmapIndexBuffer(u32 used_size) override {}
  void PushUniformBuffer(const void* data, u32 data_size) override {}
  void* MapUniformBuffer(u32 size) override
  {
    if (size > m_scratch_size)
      GrowScratch(size);
    return m_scratch;
  }
  void UnmapUniformBuffer(u32 size) override {}

  void SetRenderTargets(GPUTexture* const* rts, u32 num_rts, GPUTexture* ds,
                        GPUPipeline::RenderPassFlag flags) override
  {
  }
  void SetPipeline(GPUPipeline* pipeline) override {}
  void SetTextureSampler(u32 slot, GPUTexture* texture, GPUSampler* sampler) override {}
  void SetTextureBuffer(u32 slot, GPUTextureBuffer* buffer) override {}
  void SetViewport(const GSVector4i rc) override {}
  void SetScissor(const GSVector4i rc) override {}
  void Draw(u32 vertex_count, u32 base_vertex) override {}
  void DrawIndexed(u32 index_count, u32 base_index, u32 base_vertex) override {}
  void DrawIndexedWithBarrier(u32 index_count, u32 base_index, u32 base_vertex, DrawBarrier type) override {}

  // Nothing is presented: a frame is read, not shown.
  bool BeginPresent(bool skip_present, u32 clear_color) override { return false; }
  void EndPresent(bool explicit_submit) override {}
  void SubmitPresent() override {}
  void SetVSyncMode(GPUVSyncMode mode, bool allow_present_throttle) override
  {
    m_vsync_mode = mode;
    m_allow_present_throttle = allow_present_throttle;
  }

  bool SupportsTextureFormat(GPUTexture::Format format) const override
  {
    // The software renderer asks for its display format in order of
    // preference and takes the first the device has. Saying yes to one - the
    // byte order the frontend is handed - means one conversion, the
    // renderer's own, and none here.
    return (format == GPUTexture::Format::BGRA8);
  }

protected:
  bool CreateDevice(std::string_view adapter, bool threaded_presentation,
                    std::optional<bool> exclusive_fullscreen_control, FeatureMask disabled_features,
                    Error* error) override
  {
    m_max_texture_size = 4096;
    m_max_multisamples = 1;
    m_features = {};
    // A surfaceless "window" of the size the console's picture is read at;
    // nothing is scaled to it.
    m_window_info = {};
    m_window_info.type = WindowInfo::Type::Surfaceless;
    m_window_info.surface_width = 640;
    m_window_info.surface_height = 480;
    m_window_info.surface_scale = 1.0f;
    m_window_info.surface_format = GPUTexture::Format::BGRA8;
    return true;
  }

  void DestroyDevice() override
  {
    std::free(m_scratch);
    m_scratch = nullptr;
    m_scratch_size = 0;
  }

  std::unique_ptr<GPUShader> CreateShaderFromBinary(GPUShaderStage stage, std::span<const u8> data,
                                                    Error* error) override
  {
    return std::make_unique<MemoryShader>(stage);
  }

  std::unique_ptr<GPUShader> CreateShaderFromSource(GPUShaderStage stage, GPUShaderLanguage language,
                                                    std::string_view source, const char* entry_point,
                                                    DynamicHeapArray<u8>* out_binary, Error* error) override
  {
    return std::make_unique<MemoryShader>(stage);
  }

private:
  void GrowScratch(size_t bytes)
  {
    std::free(m_scratch);
    m_scratch = std::malloc(bytes);
    m_scratch_size = bytes;
  }

  void* m_scratch = nullptr;
  size_t m_scratch_size = 0;
};

} // namespace

// patches/0001: GPUDevice::CreateDeviceForAPI(RenderAPI::None)
std::unique_ptr<GPUDevice> ChimeraCreateMemoryDevice()
{
  return std::make_unique<MemoryDevice>();
}

// The bytes of a texture this device made, for the adapter. Null for a
// texture of any other device.
const u8* ChimeraMemoryTextureBits(GPUTexture* texture, u32* pitch)
{
  if (!texture || !g_gpu_device || g_gpu_device->GetRenderAPI() != RenderAPI::None)
    return nullptr;

  const MemoryTexture* const tex = static_cast<const MemoryTexture*>(texture);
  *pitch = tex->Pitch();
  return tex->Bits();
}
