#ifndef INPUT_HANDLER_PIUIO_DEBUG
#define INPUT_HANDLER_PIUIO_DEBUG

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "InputHandler.h"
#include "PlayerNumber.h"
#include "RageInputDevice.h"
#include "RageThreads.h"

class InputHandler_Linux_PIUIO_Debug : public InputHandler {
 public:
  InputHandler_Linux_PIUIO_Debug();
  ~InputHandler_Linux_PIUIO_Debug() override;

  static bool ShouldAutoLoad();
  void GetDevicesAndDescriptions(
      std::vector<InputDeviceInfo>& devices_out) override;

  void StartSensorDebugging() override;
  void StopSensorDebugging() override;

  bool IsConnected() const { return fd_ >= 0 && mapped_ != nullptr; }

 private:
  // this matches the static path in the kernel module that exposes this
  // information.
  // see: https://github.com/dinsfire64/piuio (thank you djpohly)
  static constexpr char kDevicePath[] = "/dev/piuio_full0";

  // the piuio has eight byte states, and there are four sensors
  static constexpr size_t kFullStateSize = 8;
  static constexpr size_t kNumSensors = 4;
  static constexpr size_t kNumPads = 2;

  // Definitions are Pump, then ITG in naming. For example, UL/U is the Pump
  // up-left or dance up input. The device reports active-low button states.
  static constexpr uint8_t kButtonUlU = 1U << 0;
  static constexpr uint8_t kButtonUrD = 1U << 1;
  static constexpr uint8_t kButtonCnL = 1U << 2;
  static constexpr uint8_t kButtonLlR = 1U << 3;
  static constexpr uint8_t kButtonLrStart = 1U << 4;

  // location of the pad state in each piuio message.
  // no other bytes change based on sensor, so we are only interested in these
  // two bytes.
  static constexpr size_t kPlayerIndex[kNumPads] = {
      0,
      2,
  };

  void QueueFullSensorState(
      PlayerNumber pn, uint8_t sensor_index, uint8_t state);

  static int InputThread_Start(void* p);
  void InputThreadMain();

  // memory mapped info from the kernel
  void* mapped_ = nullptr;
  size_t mapping_size_ = 0;

  int fd_ = -1;

  std::atomic<bool> shutdown_{true};
  RageThread* debug_thread_ = nullptr;
};

#endif

/*
 * (c) 2026 din
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
