// The sound device of a machine that has none.
//
// Upstream's AudioStream (src/util/audio_stream.cpp) is a ring buffer between
// the emulation thread and a device callback, with a time stretcher and a
// surround expander in front of it: all of it exists to keep a sound card fed
// at whatever speed the emulator happens to be running. A core has no device
// and no speed. What the SPU wrote this frame is what the frame sounded like,
// and the frontend takes it at the end of the frame.
//
// So this file IS the class - the same header, none of upstream's source - and
// the stream is a plain buffer the adapter empties once a frame
// (ChimeraAudioTake). Nothing here reads a clock, and nothing drops or
// invents a sample: how many frames of sound a frame of picture produced is
// the machine's business.
//
// SPDX-License-Identifier: GPL-3.0

#include "util/audio_stream.h"

#include "common/error.h"
#include "common/settings_interface.h"

#include <cstring>

// 44100 Hz at 50 frames a second is 882 stereo frames; a frame the machine
// runs long (a mode change) can be a little more. A second of room is room
// for anything short of a machine that has stopped drawing frames - and even
// then nothing is lost, see EndWrite.
static constexpr u32 CAPACITY_FRAMES = 44100;

extern "C" int ChimeraAudioTake(int16_t* out, int max_frames);

// The surround expander the header keeps a pointer to. Nothing makes one.
class FreeSurroundDecoder
{
};

// The stream the machine's sound goes to, and where it is in its buffer. They
// are kept here rather than in the object because the adapter reads them and
// the object's own members are private to upstream's header.
static AudioStream* s_live_stream = nullptr;
static s16* s_live_buffer = nullptr;
static u32 s_live_wpos = 0;
static u32 s_overflowed_frames = 0;

AudioStream::DeviceInfo::DeviceInfo(std::string name_, std::string display_name_, u32 minimum_latency_)
  : name(std::move(name_)), display_name(std::move(display_name_)), minimum_latency_frames(minimum_latency_)
{
}

AudioStream::DeviceInfo::~DeviceInfo() = default;

AudioStream::AudioStream(u32 sample_rate, const AudioStreamParameters& parameters)
  : m_sample_rate(sample_rate), m_parameters(parameters), m_internal_channels(NUM_INPUT_CHANNELS),
    m_output_channels(NUM_INPUT_CHANNELS)
{
  m_buffer_size = CAPACITY_FRAMES;
  m_target_buffer_size = CAPACITY_FRAMES;
  m_buffer = std::make_unique<s16[]>(static_cast<size_t>(CAPACITY_FRAMES) * NUM_INPUT_CHANNELS);
  std::memset(m_buffer.get(), 0, sizeof(s16) * CAPACITY_FRAMES * NUM_INPUT_CHANNELS);
}

AudioStream::~AudioStream()
{
  if (s_live_stream == this)
  {
    s_live_stream = nullptr;
    s_live_buffer = nullptr;
    s_live_wpos = 0;
  }
}

namespace {
// The class is concrete upstream only through its device subclasses.
class ChimeraAudioStream final : public AudioStream
{
public:
  ChimeraAudioStream(u32 sample_rate, const AudioStreamParameters& parameters) : AudioStream(sample_rate, parameters) {}
};
} // namespace

std::unique_ptr<AudioStream> AudioStream::CreateNullStream(u32 sample_rate, u32 buffer_ms)
{
  // The SPU keeps one of these beside the real stream and writes to it while
  // sound is muted or fast-forwarding. It must not become the stream the
  // frontend hears, so it is never the live one.
  AudioStreamParameters params;
  params.stretch_mode = AudioStretchMode::Off;
  params.expansion_mode = AudioExpansionMode::Disabled;
  return std::make_unique<ChimeraAudioStream>(sample_rate, params);
}

std::unique_ptr<AudioStream> AudioStream::CreateStream(AudioBackend backend, u32 sample_rate,
                                                       const AudioStreamParameters& parameters,
                                                       const char* driver_name, const char* device_name,
                                                       Error* error)
{
  std::unique_ptr<AudioStream> stream = std::make_unique<ChimeraAudioStream>(sample_rate, parameters);
  s_live_stream = stream.get();
  s_live_buffer = stream->m_buffer.get();
  s_live_wpos = 0;
  return stream;
}

u32 AudioStream::GetAlignedBufferSize(u32 size)
{
  return (size + (CHUNK_SIZE - 1)) / CHUNK_SIZE * CHUNK_SIZE;
}

u32 AudioStream::GetBufferSizeForMS(u32 sample_rate, u32 ms)
{
  return GetAlignedBufferSize((ms * sample_rate) / 1000u);
}

u32 AudioStream::GetMSForBufferSize(u32 sample_rate, u32 buffer_size)
{
  return (sample_rate == 0) ? 0 : ((buffer_size * 1000u) / sample_rate);
}

static constexpr const std::array s_backend_names = {"Null", "Cubeb", "SDL"};

std::optional<AudioBackend> AudioStream::ParseBackendName(const char* str)
{
  for (size_t i = 0; i < s_backend_names.size(); i++)
  {
    if (std::strcmp(str, s_backend_names[i]) == 0)
      return static_cast<AudioBackend>(i);
  }
  return std::nullopt;
}

const char* AudioStream::GetBackendName(AudioBackend backend)
{
  return s_backend_names[static_cast<size_t>(backend)];
}

const char* AudioStream::GetBackendDisplayName(AudioBackend backend)
{
  return s_backend_names[static_cast<size_t>(backend)];
}

static constexpr const std::array s_expansion_mode_names = {
  "Disabled", "StereoLFE", "Quadraphonic", "QuadraphonicLFE", "Surround51", "Surround71",
};

const char* AudioStream::GetExpansionModeName(AudioExpansionMode mode)
{
  return (static_cast<size_t>(mode) < s_expansion_mode_names.size()) ? s_expansion_mode_names[static_cast<size_t>(mode)] :
                                                                       "";
}

const char* AudioStream::GetExpansionModeDisplayName(AudioExpansionMode mode)
{
  return GetExpansionModeName(mode);
}

std::optional<AudioExpansionMode> AudioStream::ParseExpansionMode(const char* name)
{
  for (size_t i = 0; i < s_expansion_mode_names.size(); i++)
  {
    if (std::strcmp(name, s_expansion_mode_names[i]) == 0)
      return static_cast<AudioExpansionMode>(i);
  }
  return std::nullopt;
}

static constexpr const std::array s_stretch_mode_names = {"None", "Resample", "TimeStretch"};

const char* AudioStream::GetStretchModeName(AudioStretchMode mode)
{
  return (static_cast<size_t>(mode) < s_stretch_mode_names.size()) ? s_stretch_mode_names[static_cast<size_t>(mode)] : "";
}

const char* AudioStream::GetStretchModeDisplayName(AudioStretchMode mode)
{
  return GetStretchModeName(mode);
}

std::optional<AudioStretchMode> AudioStream::ParseStretchMode(const char* name)
{
  for (size_t i = 0; i < s_stretch_mode_names.size(); i++)
  {
    if (std::strcmp(name, s_stretch_mode_names[i]) == 0)
      return static_cast<AudioStretchMode>(i);
  }
  return std::nullopt;
}

u32 AudioStream::GetBufferedFramesRelaxed() const
{
  return (this == s_live_stream) ? s_live_wpos : 0;
}

void AudioStream::SetPaused(bool paused)
{
  // The frontend pauses by not asking for a frame. A stream that forgot what
  // it held because the machine stopped between two frames would lose sound.
  m_paused = paused;
}

#ifdef CHIMERA_ARCADE
// An arcade board is louder than a console. ArcadeDuck raises what the board's
// sound chip mixed by 12 dB on its way to a sound card - a cabinet's own
// amplifier, in effect - and holds the peaks down with a limiter. The 12 dB is
// kept, because a board that played 12 dB quieter than the emulator it comes
// from would be a different-sounding machine; the limiter is a sound card's
// business and is not, so a peak the gain pushes past full scale stops there.
// In whole numbers: 10^(12/20) is 3.98107, which is 130451/32768, and no two C
// libraries can disagree about a multiplication.
static s32 s_gain_q15 = 32768;

void AudioStream::SetOutputVolume(u32 volume, s32 gain_db)
{
  m_volume = volume;
  m_output_gain_db = gain_db;
  if (this == s_live_stream || !s_live_stream)
    s_gain_q15 = (gain_db == 12) ? 130451 : 32768;
}

void AudioStream::SetBackendPeriodFrames(u32 frames)
{
}
#else
void AudioStream::SetOutputVolume(u32 volume)
{
  m_volume = volume;
}
#endif

void AudioStream::BeginWrite(SampleType** buffer_ptr, u32* num_frames)
{
  // CHUNK_SIZE at a time, as upstream hands it out: the SPU's own loop is
  // written against that, and a different chunking would be a different
  // schedule of SPU ticks - which are the machine's.
  if (this != s_live_stream)
  {
    // the SPU's null stream: sound nobody is meant to hear
    *buffer_ptr = m_buffer.get();
    *num_frames = CHUNK_SIZE;
    return;
  }

  if (s_live_wpos + CHUNK_SIZE > m_buffer_size)
  {
    // Nobody took a second of sound. Drop it and go on: the machine does.
    s_overflowed_frames += s_live_wpos;
    s_live_wpos = 0;
  }
  *buffer_ptr = &m_buffer[static_cast<size_t>(s_live_wpos) * NUM_INPUT_CHANNELS];
  *num_frames = CHUNK_SIZE;
}

void AudioStream::EndWrite(u32 num_frames)
{
  if (this == s_live_stream)
    s_live_wpos += num_frames;
}

void AudioStream::EmptyBuffer()
{
  // Upstream empties the ring when the speed changes or a state is loaded so
  // a sound card does not play stale audio. Here that would make a frame's
  // sound depend on whether a state was loaded before it; the buffer is in
  // the state, and is emptied by the adapter and by nothing else.
}

void AudioStream::SetNominalRate(float tempo)
{
  m_nominal_rate = tempo;
}

void AudioStream::SetStretchMode(AudioStretchMode mode)
{
  m_parameters.stretch_mode = mode;
}

std::vector<std::pair<std::string, std::string>> AudioStream::GetDriverNames(AudioBackend backend)
{
  return {};
}

std::vector<AudioStream::DeviceInfo> AudioStream::GetOutputDevices(AudioBackend backend, const char* driver,
                                                                   u32 sample_rate)
{
  return {};
}

void AudioStreamParameters::Load(SettingsInterface& si, const char* section)
{
  // Stretching and expansion are things done to sound on its way to a device.
  stretch_mode = AudioStretchMode::Off;
  expansion_mode = AudioExpansionMode::Disabled;
}

void AudioStreamParameters::Save(SettingsInterface& si, const char* section) const
{
}

void AudioStreamParameters::Clear(SettingsInterface& si, const char* section)
{
}

bool AudioStreamParameters::operator==(const AudioStreamParameters& rhs) const
{
  return (std::memcmp(this, &rhs, sizeof(*this)) == 0);
}

bool AudioStreamParameters::operator!=(const AudioStreamParameters& rhs) const
{
  return !(*this == rhs);
}

// What the frame sounded like: interleaved stereo frames, taken once. What
// does not fit in the caller's buffer is kept for the next frame - a frame
// the machine ran long is late sound, not lost sound.
int ChimeraAudioTake(int16_t* out, int max_frames)
{
  if (!s_live_buffer)
    return 0;

  const u32 count = (s_live_wpos < static_cast<u32>(max_frames)) ? s_live_wpos : static_cast<u32>(max_frames);
#ifdef CHIMERA_ARCADE
  if (s_gain_q15 != 32768)
  {
    for (u32 i = 0; i < count * 2; i++)
    {
      const s64 v = (static_cast<s64>(s_live_buffer[i]) * s_gain_q15) >> 15;
      out[i] = static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
  }
  else
#endif
  std::memcpy(out, s_live_buffer, sizeof(s16) * 2 * count);
  if (count < s_live_wpos)
    std::memmove(s_live_buffer, s_live_buffer + static_cast<size_t>(count) * 2, sizeof(s16) * 2 * (s_live_wpos - count));
  s_live_wpos -= count;
  return static_cast<int>(count);
}
