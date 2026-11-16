#include "InputHandler_Linux_PIUIO_Debug.h"

#include <fcntl.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputFilter.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "arch/InputHandler/InputHandler.h"

REGISTER_INPUT_HANDLER_CLASS2(PIUIO_Debug, Linux_PIUIO_Debug);
REGISTER_AUTODETECT_INPUT_HANDLER_CLASS2(PIUIO_Debug, Linux_PIUIO_Debug);

bool InputHandler_Linux_PIUIO_Debug::ShouldAutoLoad() {
  struct stat st;
  return stat(DEVICE_PATH, &st) == 0 && S_ISCHR(st.st_mode);
}

InputHandler_Linux_PIUIO_Debug::InputHandler_Linux_PIUIO_Debug() {
  // do not start the thread when initialized.
  m_bShutdown = true;

  fd = open(DEVICE_PATH, O_RDONLY);
  if (fd < 0) {
    LOG->Warn(
        "Couldn't open PIUIO Debug device at %s %s", DEVICE_PATH,
        strerror(errno));
    return;
  }

  struct stat st;
  if (fstat(fd, &st) == -1) {
    LOG->Warn("Couldn't stat PIUIO Debug device: %s", strerror(errno));
    close(fd);
    return;
  }

  if (!S_ISCHR(st.st_mode)) {
    LOG->Warn("Ignoring %s: not a character device", DEVICE_PATH);
    close(fd);
    return;
  }

  const size_t page_size = sysconf(_SC_PAGESIZE);
  mapped = mmap(nullptr, page_size, PROT_READ, MAP_SHARED, fd, 0);

  if (mapped == MAP_FAILED) {
    LOG->Warn("Could not memory map %s", DEVICE_PATH);
    close(fd);
    return;
  }

  LOG->Info("PIUIO Debug device %s opened", DEVICE_PATH);
}

void InputHandler_Linux_PIUIO_Debug::StartSensorDebugging() {
  if (!IsConnected() || DebugThread != nullptr) {
    return;
  }

  m_bShutdown = false;

  DebugThread = new RageThread();
  DebugThread->SetName("PIUIO debug thread");
  DebugThread->Create(InputThread_Start, this);
}

void InputHandler_Linux_PIUIO_Debug::StopSensorDebugging() {
  if (DebugThread != nullptr && !m_bShutdown) {
    m_bShutdown = true;
    DebugThread->Wait();

    delete DebugThread;
    DebugThread = nullptr;
  }
}

InputHandler_Linux_PIUIO_Debug::~InputHandler_Linux_PIUIO_Debug() {
  // Shut down the thread if it's running
  StopSensorDebugging();

  if (mapped != nullptr && mapped != MAP_FAILED) {
    munmap(mapped, sysconf(_SC_PAGESIZE));
  }

  if (fd >= 0) {
    close(fd);
  }
}

int InputHandler_Linux_PIUIO_Debug::InputThread_Start(void* p) {
  ((InputHandler_Linux_PIUIO_Debug*)p)->InputThreadMain();
  return 0;
}

void InputHandler_Linux_PIUIO_Debug::BroadcastFullSensorStateHelper(
    PlayerNumber pn, uint8_t sensor_index, piuio_player_byte_t state) {
  PadSensor curr_sensor = PadSensor_TopCenter;

  switch (sensor_index) {
    case 0:
      curr_sensor = PadSensor_RightCenter;
      break;
    case 1:
      curr_sensor = PadSensor_LeftCenter;
      break;
    case 2:
      curr_sensor = PadSensor_BottomCenter;
      break;
    case 3:
      curr_sensor = PadSensor_TopCenter;
      break;
    default:
      LOG->Warn("Invalid sensor position %d", sensor_index);
      break;
  }

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* input = &GAMESTATE->GetCurrentGame()->input_scheme_;
  const std::string& input_name = input->name_;

  // all buttons here are active low, so set to zero when "true"
  if (EqualsNoCase(input_name, "dance")) {
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Up, curr_sensor, !state.btn_UL_U);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Down, curr_sensor, !state.btn_UR_D);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Left, curr_sensor, !state.btn_CN_L);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Right, curr_sensor, !state.btn_LL_R);

  } else if (EqualsNoCase(input_name, "pump")) {
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_UpLeft, curr_sensor, !state.btn_UL_U);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_UpRight, curr_sensor, !state.btn_UR_D);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Center, curr_sensor, !state.btn_CN_L);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_DownLeft, curr_sensor, !state.btn_LL_R);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_DownRight, curr_sensor, !state.btn_LR_START);
  }
}

void InputHandler_Linux_PIUIO_Debug::InputThreadMain() {
  if (fd < 0) {
    LOG->Warn("InputHandler_Linux_PIUIO_Debug started with no file handler");
    return;
  }

  auto* bytes =
      static_cast<const volatile uint8_t (*)[FULL_STATE_SIZE]>(mapped);

  piuio_player_byte_t prev[NUM_OF_PADS][NUM_OF_SENSORS] = {};
  bool forceRedraw = false;

  while (!m_bShutdown) {
    for (size_t i = 0; i < NUM_OF_SENSORS; ++i) {
      for (size_t player = 0; player < NUM_OF_PADS; ++player) {
        auto& previous = prev[player][i];
        const auto current = bytes[i][playerIndex[player]];

        if (current != previous.raw) {
          previous.raw = current;

          BroadcastFullSensorStateHelper(
              player == 0 ? PLAYER_1 : PLAYER_2, i, previous);

          forceRedraw = true;
        }
      }
    }

    if (forceRedraw) {
      MESSAGEMAN->Broadcast("TestSensorRedrawEvent");
      forceRedraw = false;
    }

    // limit the polling of this thread to prevent additional overhead.
    usleep(10000);
  }
}

void InputHandler_Linux_PIUIO_Debug::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>& vDevicesOut) {}

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
