#define SDL_MAIN_HANDLED
#include "RageSoundDriver_DDRMini_SDL.h"

#include <SDL.h>
#include <algorithm>
#include <cstring>

#include "RageLog.h"
#include "global.h"

REGISTER_SOUND_DRIVER_CLASS2("DDRMiniSDL", DDRMini_SDL);

namespace {
constexpr int kChannels = 2;
constexpr int kBytesPerFrame = kChannels * sizeof(int16_t);
}

RageSoundDriver_DDRMini_SDL::RageSoundDriver_DDRMini_SDL() = default;

RageSoundDriver_DDRMini_SDL::~RageSoundDriver_DDRMini_SDL() {
  if (device_ != 0) {
    SDL_PauseAudioDevice(device_, 1);
    SDL_CloseAudioDevice(device_);
    device_ = 0;
  }
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

std::string RageSoundDriver_DDRMini_SDL::Init() {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    return std::string("SDL audio initialization failed: ") + SDL_GetError();
  }

  SDL_AudioSpec requested;
  SDL_zero(requested);
  // This is the native format verified by the DDR Mini smoke test.  The
  // engine resamples source audio to the driver's reported rate.
  requested.freq = 48000;
  requested.format = AUDIO_S16SYS;
  requested.channels = kChannels;
  requested.samples = 1024;
  requested.callback = AudioCallback;
  requested.userdata = this;

  SDL_AudioSpec obtained;
  SDL_zero(obtained);
  device_ = SDL_OpenAudioDevice(
      nullptr, 0, &requested, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (device_ == 0) {
    return std::string("SDL_OpenAudioDevice failed: ") + SDL_GetError();
  }
  if (obtained.format != AUDIO_S16SYS || obtained.channels != kChannels) {
    SDL_CloseAudioDevice(device_);
    device_ = 0;
    return ssprintf(
        "DDR Mini SDL returned unsupported audio format 0x%x with %d channels",
        obtained.format, obtained.channels);
  }

  sample_rate_ = obtained.freq;
  buffer_frames_ = obtained.samples;
  SetDecodeBufferSize(buffer_frames_ * 4);
  StartDecodeThread();

  LOG->Info(
      "DDR Mini SDL audio initialized: driver=%s, %d Hz, stereo S16, %d-frame buffer",
      SDL_GetCurrentAudioDriver(), sample_rate_, buffer_frames_);
  SDL_PauseAudioDevice(device_, 0);
  return std::string();
}

void RageSoundDriver_DDRMini_SDL::AudioCallback(
    void* userdata, uint8_t* stream, int length) {
  static_cast<RageSoundDriver_DDRMini_SDL*>(userdata)->FillAudio(
      stream, length);
}

void RageSoundDriver_DDRMini_SDL::FillAudio(uint8_t* stream, int length) {
  const int frames = length / kBytesPerFrame;
  if (frames <= 0) {
    std::memset(stream, 0, length);
    return;
  }

  const int64_t first_frame = frames_generated_.load(std::memory_order_relaxed);
  Mix(
      reinterpret_cast<int16_t*>(stream), frames, first_frame,
      GetPosition());
  frames_generated_.store(first_frame + frames, std::memory_order_relaxed);
}

int64_t RageSoundDriver_DDRMini_SDL::GetPosition() const {
  return std::max<int64_t>(
      0, frames_generated_.load(std::memory_order_relaxed) - buffer_frames_);
}

float RageSoundDriver_DDRMini_SDL::GetPlayLatency() const {
  return static_cast<float>(buffer_frames_) / sample_rate_;
}
