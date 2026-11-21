#ifndef RAGE_SOUND_DRIVER_DDRMINI_SDL_H
#define RAGE_SOUND_DRIVER_DDRMINI_SDL_H

#include <atomic>
#include <cstdint>
#include <string>

#include "RageSoundDriver.h"

class RageSoundDriver_DDRMini_SDL : public RageSoundDriver {
 public:
  RageSoundDriver_DDRMini_SDL();
  ~RageSoundDriver_DDRMini_SDL() override;

  std::string Init() override;
  int64_t GetPosition() const override;
  float GetPlayLatency() const override;
  int GetSampleRate() const override { return sample_rate_; }

 private:
  static void AudioCallback(void* userdata, uint8_t* stream, int length);
  void FillAudio(uint8_t* stream, int length);

  uint32_t device_ = 0;
  int sample_rate_ = 48000;
  int buffer_frames_ = 1024;
  std::atomic<int64_t> frames_generated_{0};
};

#endif
