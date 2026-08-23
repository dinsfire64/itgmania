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
  DebugThread->SetName("SnekConfig debug thread");
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
  PadSensor currSensor = PadSensor_Top;

  switch (sensor_index) {
    case 0:
      currSensor = PadSensor_Right;
      break;
    case 1:
      currSensor = PadSensor_Left;
      break;
    case 2:
      currSensor = PadSensor_Bottom;
      break;
    case 3:
      currSensor = PadSensor_Top;
      break;
    default:
      LOG->Warn("Invalid sensor position %d", sensor_index);
      break;
  }

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* pInput = &GAMESTATE->GetCurrentGame()->m_InputScheme;
  std::string sInputName = pInput->m_szName;

  // all buttons here are active low, so set to zero when "true"
  if (EqualsNoCase(sInputName, "dance")) {
    INPUTFILTER->setFullSensorState(
        pn, PadPanel_Up, currSensor, (state.btn_UL_U) ? 0.0f : 1.0f);
    INPUTFILTER->setFullSensorState(
        pn, PadPanel_Down, currSensor, (state.btn_UR_D) ? 0.0f : 1.0f);
    INPUTFILTER->setFullSensorState(
        pn, PadPanel_Left, currSensor, (state.btn_CN_L) ? 0.0f : 1.0f);
    INPUTFILTER->setFullSensorState(
        pn, PadPanel_Right, currSensor, (state.btn_LL_R) ? 0.0f : 1.0f);

  } else if (EqualsNoCase(sInputName, "pump")) {
    INPUTFILTER->setFullSensorState(
        pn, PadPanel_UpLeft, currSensor, (state.btn_UL_U) ? 0.0f : 1.0f);
    INPUTFILTER->setFullSensorState(
        pn, PadPanel_UpRight, currSensor, (state.btn_UR_D) ? 0.0f : 1.0f);
    INPUTFILTER->setFullSensorState(
        pn, PadPanel_Center, currSensor, (state.btn_CN_L) ? 0.0f : 1.0f);
    INPUTFILTER->setFullSensorState(
        pn, PadPanel_DownLeft, currSensor, (state.btn_LL_R) ? 0.0f : 1.0f);
    INPUTFILTER->setFullSensorState(
        pn, PadPanel_DownRight, currSensor, (state.btn_LR_START) ? 0.0f : 1.0f);
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
