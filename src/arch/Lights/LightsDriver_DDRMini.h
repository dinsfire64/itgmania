#ifndef LIGHTS_DRIVER_DDRMINI_H
#define LIGHTS_DRIVER_DDRMINI_H

#include <array>
#include <cstdint>

#include "arch/Lights/LightsDriver.h"

class LightsDriver_DDRMini : public LightsDriver {
 public:
  LightsDriver_DDRMini();
  ~LightsDriver_DDRMini() override;

  void Set(const LightsState* ls) override;

 private:
  static constexpr unsigned NUM_LEDS = 12;
  static constexpr unsigned NUM_PADS = 2;
  static constexpr unsigned PAD_REPORT_SIZE = 5;

  std::array<int, NUM_LEDS> led_fds_{};
  std::array<int, NUM_PADS> pad_fds_{};
  std::array<uint8_t, NUM_LEDS> previous_leds_{};
  std::array<std::array<uint8_t, PAD_REPORT_SIZE>, NUM_PADS> previous_pads_{};
  bool first_update_ = true;

  void WriteLED(unsigned index, uint8_t value, bool force);
  void WritePad(
      unsigned index, const std::array<uint8_t, PAD_REPORT_SIZE>& report,
      bool force);
};

#endif
