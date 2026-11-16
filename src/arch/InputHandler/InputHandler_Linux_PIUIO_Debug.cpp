#include "InputHandler_Linux_PIUIO_Debug.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

#include "Game.h"
#include "GameInput.h"
#include "GameState.h"
#include "InputFilter.h"
#include "RageInputDevice.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "arch/InputHandler/InputHandler.h"

REGISTER_INPUT_HANDLER_CLASS2(PIUIO_Debug, Linux_PIUIO_Debug);
REGISTER_AUTODETECT_INPUT_HANDLER_CLASS2(PIUIO_Debug, Linux_PIUIO_Debug);

bool InputHandler_Linux_PIUIO_Debug::ShouldAutoLoad() {
  struct stat st;
  return stat(kDevicePath, &st) == 0 && S_ISCHR(st.st_mode);
}

InputHandler_Linux_PIUIO_Debug::InputHandler_Linux_PIUIO_Debug() {
  fd_ = open(kDevicePath, O_RDONLY);
  if (fd_ < 0) {
    LOG->Warn(
        "Couldn't open PIUIO Debug device at %s: %s", kDevicePath,
        strerror(errno));
    return;
  }

  struct stat st;
  if (fstat(fd_, &st) == -1) {
    LOG->Warn("Couldn't stat PIUIO Debug device: %s", strerror(errno));
    close(fd_);
    fd_ = -1;
    return;
  }

  if (!S_ISCHR(st.st_mode)) {
    LOG->Warn("Ignoring %s: not a character device", kDevicePath);
    close(fd_);
    fd_ = -1;
    return;
  }

  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    LOG->Warn("Could not determine system page size");
    close(fd_);
    fd_ = -1;
    return;
  }

  mapping_size_ = static_cast<size_t>(page_size);
  mapped_ = mmap(nullptr, mapping_size_, PROT_READ, MAP_SHARED, fd_, 0);

  if (mapped_ == MAP_FAILED) {
    LOG->Warn("Could not memory map %s: %s", kDevicePath, strerror(errno));
    mapped_ = nullptr;
    mapping_size_ = 0;
    close(fd_);
    fd_ = -1;
    return;
  }

  LOG->Info("PIUIO Debug device %s opened", kDevicePath);
}

void InputHandler_Linux_PIUIO_Debug::StartSensorDebugging() {
  if (!IsConnected() || debug_thread_ != nullptr) {
    return;
  }

  shutdown_ = false;

  debug_thread_ = new RageThread();
  debug_thread_->SetName("PIUIO debug thread");
  debug_thread_->Create(InputThread_Start, this);
}

void InputHandler_Linux_PIUIO_Debug::StopSensorDebugging() {
  if (debug_thread_ != nullptr) {
    shutdown_ = true;
    if (debug_thread_->IsCreated()) {
      debug_thread_->Wait();
    }
    delete debug_thread_;
    debug_thread_ = nullptr;
  }
}

InputHandler_Linux_PIUIO_Debug::~InputHandler_Linux_PIUIO_Debug() {
  // Shut down the thread if it's running
  StopSensorDebugging();

  if (mapped_ != nullptr) {
    munmap(mapped_, mapping_size_);
    mapped_ = nullptr;
    mapping_size_ = 0;
  }

  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

int InputHandler_Linux_PIUIO_Debug::InputThread_Start(void* p) {
  static_cast<InputHandler_Linux_PIUIO_Debug*>(p)->InputThreadMain();
  return 0;
}

void InputHandler_Linux_PIUIO_Debug::QueueFullSensorState(
    PlayerNumber pn, uint8_t sensor_index, uint8_t state) {
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
      return;
  }

  // check to see which game we are running as it can change during gameplay.
  const InputScheme* input = &GAMESTATE->GetCurrentGame()->input_scheme_;
  const std::string& input_name = input->name_;

  // all buttons here are active low, so set to zero when "true"
  if (EqualsNoCase(input_name, "dance")) {
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Up, curr_sensor, (state & kButtonUlU) == 0);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Down, curr_sensor, (state & kButtonUrD) == 0);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Left, curr_sensor, (state & kButtonCnL) == 0);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Right, curr_sensor, (state & kButtonLlR) == 0);

  } else if (EqualsNoCase(input_name, "pump")) {
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_UpLeft, curr_sensor, (state & kButtonUlU) == 0);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_UpRight, curr_sensor, (state & kButtonUrD) == 0);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_Center, curr_sensor, (state & kButtonCnL) == 0);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_DownLeft, curr_sensor, (state & kButtonLlR) == 0);
    INPUTFILTER->setFullSensorStateBinary(
        pn, PadPanel_DownRight, curr_sensor, (state & kButtonLrStart) == 0);
  }
}

void InputHandler_Linux_PIUIO_Debug::InputThreadMain() {
  if (!IsConnected()) {
    LOG->Warn("InputHandler_Linux_PIUIO_Debug started with no file handler");
    return;
  }

  auto* bytes =
      static_cast<const volatile uint8_t (*)[kFullStateSize]>(mapped_);

  std::array<std::array<uint8_t, kNumSensors>, kNumPads> previous_states;
  for (auto& player_states : previous_states) {
    player_states.fill(0xff);
  }

  while (!shutdown_) {
    for (size_t sensor = 0; sensor < kNumSensors; ++sensor) {
      for (size_t player = 0; player < kNumPads; ++player) {
        auto& previous = previous_states[player][sensor];
        const uint8_t current = bytes[sensor][kPlayerIndex[player]];

        if (current != previous) {
          previous = current;

          QueueFullSensorState(
              player == 0 ? PLAYER_1 : PLAYER_2, sensor, current);
        }
      }
    }

    // limit the polling of this thread to prevent additional overhead.
    usleep(10000);
  }
}

void InputHandler_Linux_PIUIO_Debug::GetDevicesAndDescriptions(
    std::vector<InputDeviceInfo>&) {
  // This is an auxiliary sensor-debug provider, not a distinct input device.
}

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
