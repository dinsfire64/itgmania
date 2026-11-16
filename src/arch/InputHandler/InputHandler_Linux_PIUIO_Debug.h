#ifndef INPUT_HANDLER_PIUIO_DEBUG
#define INPUT_HANDLER_PIUIO_DEBUG

#include "InputHandler.h"
#include "PlayerNumber.h"
#include "RageInputDevice.h"
#include "RageThreads.h"

#pragma pack(push, 1)

// definitions are Pump, then ITG in naming.
// ex: UL for Up Left arrow, U for Up.
typedef union {
  struct {
    bool btn_UL_U : 1;
    bool btn_UR_D : 1;
    bool btn_CN_L : 1;
    bool btn_LL_R : 1;
    bool btn_LR_START : 1;
    bool btn_SELECT : 1;
    bool btn_MENU_LEFT : 1;
    bool btn_MENU_RIGHT : 1;
  };
  uint8_t raw;
} piuio_player_byte_t;

#pragma pack(pop)

class InputHandler_Linux_PIUIO_Debug : public InputHandler {
 public:
  InputHandler_Linux_PIUIO_Debug();
  ~InputHandler_Linux_PIUIO_Debug();
  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut);

  void StartSensorDebugging();
  void StopSensorDebugging();

  bool IsConnected() { return fd >= 0; }

 private:
  // this matches the static path in the kernel module that exposes this
  // information.
  // see: https://github.com/dinsfire64/piuio (thank you djpohly)
  static constexpr char DEVICE_PATH[] = "/dev/piuio_full0";

  // the piuio has eight byte states, and there are four sensors
  static constexpr unsigned int FULL_STATE_SIZE = 8;
  static constexpr unsigned int NUM_OF_SENSORS = 4;
  static constexpr unsigned int NUM_OF_PADS = 2;

  // location of the pad state in each piuio message.
  // no other bytes change based on sensor, so we are only interested in these
  // two bytes.
  static constexpr size_t playerIndex[NUM_OF_PADS] = {
      0,
      2,
  };

  void BroadcastFullSensorStateHelper(
      PlayerNumber pn, uint8_t sensor_index, piuio_player_byte_t state);

  static int InputThread_Start(void* p);
  void InputThreadMain();

  // memory mapped info from the kernel
  void* mapped;

  int fd = -1;

  bool m_bShutdown = true;
  RageThread* DebugThread = nullptr;
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
