#include "LightsDriver_DDRMini.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "GameInput.h"
#include "LightsManager.h"
#include "PlayerNumber.h"

REGISTER_LIGHTS_DRIVER_CLASS(DDRMini);

namespace {
constexpr const char* LED_PATHS[] = {
    "/sys/class/leds/button1/brightness",
    "/sys/class/leds/button2/brightness",
    "/sys/class/leds/button3/brightness",
    "/sys/class/leds/button4/brightness",
    "/sys/class/leds/button5/brightness",
    "/sys/class/leds/button6/brightness",
    "/sys/class/leds/yellow1/brightness",
    "/sys/class/leds/yellow2/brightness",
    "/sys/class/leds/red1/brightness",
    "/sys/class/leds/red2/brightness",
    "/sys/class/leds/speaker_l/brightness",
    "/sys/class/leds/speaker_r/brightness",
};

constexpr const char* PAD_PATHS[] = {
    "/dev/input/hidraw_p1",
    "/dev/input/hidraw_p2",
};

uint8_t Brightness(bool enabled) { return enabled ? 255 : 0; }

bool WriteAll(int fd, const void* buffer, size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(buffer);
  while (length > 0) {
    const ssize_t written = write(fd, bytes, length);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      return false;
    }
    bytes += written;
    length -= static_cast<size_t>(written);
  }
  return true;
}
}  // namespace

LightsDriver_DDRMini::LightsDriver_DDRMini() {
  led_fds_.fill(-1);
  pad_fds_.fill(-1);

  for (unsigned i = 0; i < NUM_LEDS; ++i) {
    led_fds_[i] = open(LED_PATHS[i], O_WRONLY | O_CLOEXEC);
  }

  for (unsigned i = 0; i < NUM_PADS; ++i) {
    pad_fds_[i] = open(PAD_PATHS[i], O_WRONLY | O_CLOEXEC);
  }
}

LightsDriver_DDRMini::~LightsDriver_DDRMini() {
  for (unsigned i = 0; i < NUM_LEDS; ++i) {
    if (led_fds_[i] >= 0) {
      static constexpr char OFF[] = "0";
      (void)lseek(led_fds_[i], 0, SEEK_SET);
      (void)WriteAll(led_fds_[i], OFF, sizeof(OFF) - 1);
      close(led_fds_[i]);
    }
  }

  constexpr std::array<uint8_t, PAD_REPORT_SIZE> off = {0x05, 0, 0, 0, 0};
  for (int fd : pad_fds_) {
    if (fd >= 0) {
      (void)WriteAll(fd, off.data(), off.size());
      close(fd);
    }
  }
}

void LightsDriver_DDRMini::WriteLED(unsigned index, uint8_t value, bool force) {
  if (!force && previous_leds_[index] == value) {
    return;
  }

  int fd = led_fds_[index];
  if (fd < 0) {
    return;
  }

  const char* text = value ? "255" : "0";
  const size_t length = value ? 3 : 1;
  if (lseek(fd, 0, SEEK_SET) < 0) {
    return;
  }
  if (WriteAll(fd, text, length)) {
    previous_leds_[index] = value;
  }
}

void LightsDriver_DDRMini::WritePad(
    unsigned index, const std::array<uint8_t, PAD_REPORT_SIZE>& report,
    bool force) {
  if (!force && previous_pads_[index] == report) {
    return;
  }

  int fd = pad_fds_[index];
  if (fd >= 0 && WriteAll(fd, report.data(), report.size())) {
    previous_pads_[index] = report;
  }
}

void LightsDriver_DDRMini::Set(const LightsState* ls) {
  if (ls == nullptr) {
    return;
  }

  const uint8_t p1_start =
      Brightness(ls->game_button_lights_[PLAYER_1][GAME_BUTTON_START]);
  const uint8_t p2_start =
      Brightness(ls->game_button_lights_[PLAYER_2][GAME_BUTTON_START]);
  const uint8_t speaker = Brightness(
      ls->cabinet_lights_[LIGHT_BASS_LEFT] ||
      ls->cabinet_lights_[LIGHT_BASS_RIGHT]);

  const std::array<uint8_t, NUM_LEDS> leds = {
      p1_start,
      p1_start,
      p1_start,
      p2_start,
      p2_start,
      p2_start,
      Brightness(ls->cabinet_lights_[LIGHT_MARQUEE_LR_LEFT]),
      Brightness(ls->cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT]),
      Brightness(ls->cabinet_lights_[LIGHT_MARQUEE_UP_LEFT]),
      Brightness(ls->cabinet_lights_[LIGHT_MARQUEE_UP_RIGHT]),
      speaker,
      speaker,
  };

  const std::array<std::array<uint8_t, PAD_REPORT_SIZE>, NUM_PADS> pads = {{
      {{0x05, Brightness(ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_RIGHT]),
        Brightness(ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_DOWN]),
        Brightness(ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_LEFT]),
        Brightness(ls->game_button_lights_[PLAYER_1][DANCE_BUTTON_UP])}},
      {{0x05, Brightness(ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_RIGHT]),
        Brightness(ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_DOWN]),
        Brightness(ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_LEFT]),
        Brightness(ls->game_button_lights_[PLAYER_2][DANCE_BUTTON_UP])}},
  }};

  for (unsigned i = 0; i < NUM_LEDS; ++i) {
    WriteLED(i, leds[i], first_update_);
  }
  for (unsigned i = 0; i < NUM_PADS; ++i) {
    WritePad(i, pads[i], first_update_);
  }

  first_update_ = false;
}
