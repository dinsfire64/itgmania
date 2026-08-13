#ifndef INPUT_HANDLER_STAC2EVENTQUEUE
#define INPUT_HANDLER_STAC2EVENTQUEUE

/*
 * -------------------------- NOTE --------------------------
 *
 * This driver needs user read/write access the device.
 * This can be achieved by using a udev rule like this:
 *
 * SUBSYSTEMS=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="10d9",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * or
 *
 * KERNEL=="hidraw*", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="10d9",
 * OWNER="dance", GROUP="dance", MODE="0660"
 *
 * Refer to your distribution's documentation on how to properly apply a udev
 * rule.
 *
 * -------------------------- NOTE --------------------------
 */

#include <cstdint>
#include <string>
#include <vector>

#include "InputHandler.h"
#include "LightsManager.h"
#include "RageInputDevice.h"
#include "RageThreads.h"
#include "archutils/Common/HidDevice.h"

#define STAC2EVENTQUEUE_PACKETSIZE 64

#define STAC2EVENTQUEUE_VID 0x2E8A
#define STAC2EVENTQUEUE_PID_P1 0x10D9
#define STAC2EVENTQUEUE_PID_P2 0x10E9

// the config interface is number 1
#define STAC2EVENTQUEUE_INTERFACE_NUM 1

// hid report number is this
#define STAC2EVENTQUEUE_HID_OUTPUT 0x11

// hid report response starts with this.
#define STAC2EVENTQUEUE_HID_INPUT 0x12

// these are all opcodes.
#define STAC2_CONFIG_OPCODE_GETINFO 0x01
#define STAC2_CONFIG_OPCODE_GETTIME 0x10
#define STAC2_CONFIG_OPCODE_ENABLE_EVENT_QUEUE 0x11
#define STAC2_CONFIG_OPCODE_READ_EVENT_QUEUE 0x0F
#define STAC2_CONFIG_DISCONNECT 0x0A
#define STAC2_CONFIG_ERROR 0xFF

// this struct is on packed by the microcontroller, so we need to match it.
#pragma pack(push, 1)
typedef struct {
  uint64_t timestamp_us;
  uint32_t btn_state;
} outgoing_event_t;
#pragma pack(pop)

// ensure the sizes are correct
static_assert(sizeof(outgoing_event_t) == 12, "outgoing_event_t != 12");

class InputHandler_Stac2EventQueue : public InputHandler {
 public:
  InputHandler_Stac2EventQueue();
  ~InputHandler_Stac2EventQueue();

  std::string GetDeviceSpecificInputString(const DeviceInput& di);
  void GetDevicesAndDescriptions(std::vector<InputDeviceInfo>& vDevicesOut);

  bool IsConnected() { return dev != nullptr && dev->IsConnected(); }

 private:
  HidDevice* dev;

  bool m_bShutdown;
  RageThread InputThread;

  void PushInputStateToEngine(std::uint32_t newInput, RageTimer eventTime);
  uint64_t CheckRTTTime();

  static int InputThread_Start(void* p);
  void InputThreadMain();

  bool SendCommand(
      uint8_t opcode,
      std::array<uint8_t, STAC2EVENTQUEUE_PACKETSIZE>& response);
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
