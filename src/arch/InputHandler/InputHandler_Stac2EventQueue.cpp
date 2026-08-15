#include "InputHandler_Stac2EventQueue.h"

#include <fcntl.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "EnumHelper.h"
#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "LightsManager.h"
#include "PrefsManager.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "StdString.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "arch/InputHandler/InputHandler.h"
#include "arch/Lights/LightsDriver_Export.h"
#include "archutils/Common/HidDevice.h"

REGISTER_INPUT_HANDLER_CLASS(Stac2EventQueue);

constexpr uint64_t ONE_SECOND_IN_MICROSECONDS_ULL = 1000000ULL;

// used for converting the bit positions into physical sensor locations.
struct SensorBitMapping {
  PadPanel panel;
  PadSensor sensor;
};

static constexpr SensorBitMapping sensorBitMappingDance[] = {
    {PadPanel::Up, PadSensor::Top},     {PadPanel::Up, PadSensor::Bottom},
    {PadPanel::Up, PadSensor::Left},    {PadPanel::Up, PadSensor::Right},

    {PadPanel::Down, PadSensor::Top},   {PadPanel::Down, PadSensor::Bottom},
    {PadPanel::Down, PadSensor::Left},  {PadPanel::Down, PadSensor::Right},

    {PadPanel::Left, PadSensor::Top},   {PadPanel::Left, PadSensor::Bottom},
    {PadPanel::Left, PadSensor::Left},  {PadPanel::Left, PadSensor::Right},

    {PadPanel::Right, PadSensor::Top},  {PadPanel::Right, PadSensor::Bottom},
    {PadPanel::Right, PadSensor::Left}, {PadPanel::Right, PadSensor::Right},
};

static constexpr SensorBitMapping sensorBitMappingPump[] = {
    {PadPanel::UpLeft, PadSensor::Top},
    {PadPanel::UpLeft, PadSensor::Bottom},
    {PadPanel::UpLeft, PadSensor::Left},
    {PadPanel::UpLeft, PadSensor::Right},

    {PadPanel::UpRight, PadSensor::Top},
    {PadPanel::UpRight, PadSensor::Bottom},
    {PadPanel::UpRight, PadSensor::Left},
    {PadPanel::UpRight, PadSensor::Right},

    {PadPanel::Center, PadSensor::Top},
    {PadPanel::Center, PadSensor::Bottom},
    {PadPanel::Center, PadSensor::Left},
    {PadPanel::Center, PadSensor::Right},

    {PadPanel::DownLeft, PadSensor::Top},
    {PadPanel::DownLeft, PadSensor::Bottom},
    {PadPanel::DownLeft, PadSensor::Left},
    {PadPanel::DownLeft, PadSensor::Right},

    {PadPanel::DownRight, PadSensor::Top},
    {PadPanel::DownRight, PadSensor::Bottom},
    {PadPanel::DownRight, PadSensor::Left},
    {PadPanel::DownRight, PadSensor::Right},
};

InputHandler_Stac2EventQueue::InputHandler_Stac2EventQueue() {
  m_bShutdown = false;

  // ensure auto reconnect and blocking reads (we need to ask it a question then
  // get an answer)
  dev = new HidDevice(
      STAC2EVENTQUEUE_VID, STAC2EVENTQUEUE_PID_P1,
      STAC2EVENTQUEUE_INTERFACE_NUM, true, false);

  if (IsConnected()) {
    std::array<uint8_t, STAC2EVENTQUEUE_PACKETSIZE> res;

    // get firmware information and check connection
    if (SendCommand(STAC2_CONFIG_OPCODE_GETINFO, res)) {
      LOG->Info(
          "Found stac2 version: %s", reinterpret_cast<const char*>(&res[2]));
    } else {
      LOG->Warn("Failed to open stac2 config end point");
      return;
    }

    // check to see if it has the newer opcodes.
    // older firmware versions will send an error response.
    if (!SendCommand(STAC2_CONFIG_OPCODE_GETTIME, res)) {
      LOG->Warn(
          "STAC2 firmware version does not support event queue, exiting...");
      return;
    }

    if (PREFSMAN->m_bThreadedInput) {
      InputThread.SetName("Stac2EventQueue thread");
      InputThread.Create(InputThread_Start, this);
    }
  } else {
    LOG->Warn("Stac2EventQueue enabled, but could not connect to stac2.");
  }
}

InputHandler_Stac2EventQueue::~InputHandler_Stac2EventQueue() {
  // disconnect cleanup etc
  if (InputThread.IsCreated()) {
    m_bShutdown = true;
    LOG->Trace("Shutting down Stac2EventQueue thread ...");
    InputThread.Wait();
    LOG->Trace("Stac2EventQueue thread shut down.");
  }

  if (IsConnected()) {
    std::array<uint8_t, STAC2EVENTQUEUE_PACKETSIZE> res;
    SendCommand(STAC2_CONFIG_DISCONNECT, res);
  }
}

std::string InputHandler_Stac2EventQueue::GetDeviceSpecificInputString(
    const DeviceInput& di) {
  return InputHandler::GetDeviceSpecificInputString(di);
}

void InputHandler_Stac2EventQueue::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& vDevicesOut) {
  vDevicesOut.push_back(
      InputDeviceInfo(InputDevice(DEVICE_STAC2_P1), "Stac2EventQueueP1"));
  /*
  vDevicesOut.push_back(
    InputDeviceInfo(InputDevice(DEVICE_STAC2_P2), "Stac2EventQueueP2"));
  */
}

int InputHandler_Stac2EventQueue::InputThread_Start(void* p) {
  ((InputHandler_Stac2EventQueue*)p)->InputThreadMain();
  return 0;
}

uint64_t InputHandler_Stac2EventQueue::CheckRTTTime() {
  uint64_t totalRttUs = 0;
  constexpr int samples = 10;

  std::array<uint8_t, STAC2EVENTQUEUE_PACKETSIZE> res;

  for (int i = 0; i < samples; i++) {
    uint64_t hostBefore = RageTimer::GetTimeSinceStartMicroseconds();

    SendCommand(STAC2_CONFIG_OPCODE_GETTIME, res);

    uint64_t hostAfter = RageTimer::GetTimeSinceStartMicroseconds();

    uint64_t rttUs = hostAfter - hostBefore;
    totalRttUs += rttUs;

    LOG->Trace("Sample %d: RTT = %" PRIu64 " us", i, rttUs);
  }

  int64_t usbRoundTripTimeUs = totalRttUs / samples;

  LOG->Info("Stac2 RTT: %" PRIu64 " us", usbRoundTripTimeUs);

  return usbRoundTripTimeUs;
}

void InputHandler_Stac2EventQueue::BroadcastFullSensorState(uint32_t state) {
  // check to see which game we are running as it can change during gameplay.
  const InputScheme* pInput = &GAMESTATE->GetCurrentGame()->m_InputScheme;
  std::string sInputName = pInput->m_szName;
  bool isDance = EqualsNoCase(sInputName, "dance");

  // swap the mapping depending on the game type.
  const auto& sensorBitMapping =
      isDance ? sensorBitMappingDance : sensorBitMappingPump;

  const SensorBitMapping* mapping;
  size_t mappingSize;

  if (isDance) {
    mapping = sensorBitMappingDance;
    mappingSize = std::size(sensorBitMappingDance);
  } else {
    mapping = sensorBitMappingPump;
    mappingSize = std::size(sensorBitMappingPump);
  }

  for (size_t bit = 0; bit < mappingSize; ++bit) {
    const auto& mapping = sensorBitMapping[bit];

    INPUTFILTER->setFullSensorState(
        PLAYER_1, mapping.panel, mapping.sensor,
        (state & (1u << bit)) ? 1.0f : 0.0f);
  }
}

void InputHandler_Stac2EventQueue::InputThreadMain() {
  std::array<uint8_t, STAC2EVENTQUEUE_PACKETSIZE> res;

  // before we start, get the rtt of the USB device (time in flight)
  uint64_t usbRoundTripTimeUs = CheckRTTTime();

  if (!SendCommand(STAC2_CONFIG_OPCODE_ENABLE_EVENT_QUEUE, res)) {
    LOG->Warn("STAC2 does not support event queue, exiting...");
    return;
  }

  // the device responds to the enable command
  // with it's current time, so use that as an offset
  uint64_t startLoopDeviceTime;
  std::memcpy(&startLoopDeviceTime, &res[2], sizeof(startLoopDeviceTime));

  // keep track of when we started this thread.
  uint64_t startLocalTime = ArchHooks::GetSystemTimeInMicroseconds();

  LOG->Info(
      "Stac2EventQueue: startLoopDeviceTime %" PRIu64 " LocalTime: %" PRIu64 "",
      startLoopDeviceTime, startLocalTime);

  while (!m_bShutdown) {
    // ask for events from the device.
    SendCommand(STAC2_CONFIG_OPCODE_READ_EVENT_QUEUE, res);

    // the first byte of the payload is the number of events in this message.
    uint8_t numOfEvents = res[2];

    // outgoing_event_t starts at this index, also used as an interator.
    uint16_t offset = 3;

    if (numOfEvents > 0) {
      for (uint8_t i = 0; i < numOfEvents; i++) {
        outgoing_event_t newEvent = {};
        std::memcpy(&newEvent, &res[offset], sizeof(newEvent));

        // allow the engine to know when there are debug sensors and their
        // current state
        BroadcastFullSensorState(newEvent.btn_state);

        // only let the top 5bits pass through, since they are the "muxed"
        // values.
        uint8_t muxedButtons = (newEvent.btn_state >> 27) & 0xFF;

        // use the start time to make a game engine time
        // the time we started + half the time it takes to
        uint64_t eventTimeUs = (startLocalTime + usbRoundTripTimeUs / 2) +
                               (newEvent.timestamp_us - startLoopDeviceTime);

        // convert the time into a ragetimer object by taking the us value and
        // making s and us.
        RageTimer eventTime(
            eventTimeUs / ONE_SECOND_IN_MICROSECONDS_ULL,
            (eventTimeUs % ONE_SECOND_IN_MICROSECONDS_ULL));

        // push the muxed button state to the engine
        PushInputStateToEngine(muxedButtons, eventTime);

        // iterate by sizeof(outgoing_event_t)
        offset += sizeof(outgoing_event_t);
      }
    }
  }
}

bool InputHandler_Stac2EventQueue::SendCommand(
    uint8_t opcode, std::array<uint8_t, STAC2EVENTQUEUE_PACKETSIZE>& response) {
  std::array<uint8_t, STAC2EVENTQUEUE_PACKETSIZE> cmd{};

  cmd[0] = STAC2EVENTQUEUE_HID_OUTPUT;
  cmd[1] = opcode;

  // Compute CRC over bytes 0-62
  uint8_t crc = 0;
  for (size_t i = 0; i < STAC2EVENTQUEUE_PACKETSIZE - 1; ++i) {
    crc = static_cast<uint8_t>(crc + cmd[i]);
  }

  // last byte is the crc.
  cmd[(STAC2EVENTQUEUE_PACKETSIZE - 1)] = crc;

  // Send command
  if (dev->Write(cmd.data(), cmd.size()) != HidResults::Success) {
    LOG->Warn("stac2 could not send");
    return false;
  }

  int rtnSize = dev->Read(response.data(), response.size());

  // Read response, always a full payload size.
  if (rtnSize != STAC2EVENTQUEUE_PACKETSIZE) {
    LOG->Warn("could not read stac2 command back");
    return false;
  }

  if (response[0] != STAC2EVENTQUEUE_HID_INPUT) {
    LOG->Warn("STAC2 invalid hid report: %02x", response[0]);
    return false;
  }

  uint8_t opencodeRtn = response[1];

  if (opencodeRtn == STAC2_CONFIG_ERROR) {
    LOG->Warn("STAC2 invalid opcode: %02x", opcode);
    return false;
  } else if (opencodeRtn != opcode) {
    LOG->Warn(
        "STAC2 recv mismatching opcode: %02x != %02x", opencodeRtn, opcode);
    return false;
  }

  return true;
}

void InputHandler_Stac2EventQueue::PushInputStateToEngine(
    std::uint32_t newInput, RageTimer eventTime) {
  for (int i = 0; i < 32; i++) {
    bool pressed = (newInput & (1 << i));

    DeviceInput di(DEVICE_STAC2_P1, enum_add2(JOY_BUTTON_1, i), pressed);
    di.ts = eventTime;

    ButtonPressed(di);
  }
}
