#include "LightsDriver_DDRMini.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "GameInput.h"
#include "LightsManager.h"
#include "PlayerNumber.h"
#include "RageLog.h"

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

constexpr const char* FALLBACK_PAD_PATHS[] = {
    "/dev/input/hidraw_p1",
    "/dev/input/hidraw_p2",
};

struct PhysicalDevice {
  std::string topology;
  std::string device;
};

std::string ResolvePath(const std::string& path) {
  char resolved[PATH_MAX];
  if (realpath(path.c_str(), resolved) == nullptr) {
    return std::string();
  }
  return resolved;
}

// Reduce a sysfs device path to its physical USB device.  For example, both
// .../1-1.2/1-1.2:1.0/.../input/input5/js0 and
// .../1-1.2/1-1.2:1.1/.../hidraw/hidraw0 become .../1-1.2.  This also handles
// pads exposing joystick and output reports on different USB interfaces.
std::string USBTopology(const std::string& resolved) {
  size_t component_start = 0;
  while (component_start < resolved.size()) {
    const size_t component_end = resolved.find('/', component_start + 1);
    const size_t end = component_end == std::string::npos
                           ? resolved.size()
                           : component_end;
    const std::string component =
        resolved.substr(component_start + 1, end - component_start - 1);
    const size_t colon = component.find(':');
    if (!component.empty() &&
        std::isdigit(static_cast<unsigned char>(component[0])) &&
        component.find('-') != std::string::npos && colon != std::string::npos) {
      return resolved.substr(0, component_start + 1) +
             component.substr(0, colon);
    }
    if (component_end == std::string::npos) {
      break;
    }
    component_start = component_end;
  }
  return resolved;
}

std::vector<PhysicalDevice> EnumerateClass(
    const char* class_directory, const char* prefix, const char* device_prefix,
    bool ddrmini_pads_only = false) {
  std::vector<PhysicalDevice> result;
  DIR* directory = opendir(class_directory);
  if (directory == nullptr) {
    return result;
  }

  const size_t prefix_length = std::strlen(prefix);
  while (dirent* entry = readdir(directory)) {
    if (std::strncmp(entry->d_name, prefix, prefix_length) != 0) {
      continue;
    }
    const std::string class_path =
        std::string(class_directory) + "/" + entry->d_name;
    if (ddrmini_pads_only) {
      std::string vendor;
      std::string product;
      std::ifstream(class_path + "/device/id/vendor") >> vendor;
      std::ifstream(class_path + "/device/id/product") >> product;
      if (vendor != "33dd" || product != "0014") {
        continue;
      }
    }
    const std::string resolved = ResolvePath(class_path);
    if (resolved.empty()) {
      continue;
    }
    result.push_back(
        {USBTopology(resolved), std::string(device_prefix) + entry->d_name});
  }
  closedir(directory);
  std::sort(
      result.begin(), result.end(),
      [](const PhysicalDevice& left, const PhysicalDevice& right) {
        if (left.topology != right.topology) {
          return left.topology < right.topology;
        }
        return left.device < right.device;
      });
  return result;
}

std::array<std::string, 2> DiscoverPadLights() {
  std::vector<PhysicalDevice> pad_inputs = EnumerateClass(
      "/sys/class/input", "js", "/dev/input/", true);
  if (pad_inputs.empty()) {
    // The production DDR Mini controller (USB 33dd:0014) exposes evdev and
    // hidraw interfaces but no legacy js node on this kernel.
    pad_inputs = EnumerateClass(
        "/sys/class/input", "event", "/dev/input/", true);
  }
  const std::vector<PhysicalDevice> hidraw =
      EnumerateClass("/sys/class/hidraw", "hidraw", "/dev/");

  std::multimap<std::string, std::string> hidraw_by_topology;
  for (const PhysicalDevice& device : hidraw) {
    hidraw_by_topology.emplace(device.topology, device.device);
  }

  std::array<std::string, 2> result;
  unsigned player = 0;
  for (const PhysicalDevice& input : pad_inputs) {
    if (player >= result.size()) {
      break;
    }
    const auto match = hidraw_by_topology.find(input.topology);
    if (match == hidraw_by_topology.end()) {
      LOG->Warn(
          "DDR Mini lights: no hidraw output matched %s at %s",
          input.device.c_str(), input.topology.c_str());
      continue;
    }
    result[player] = match->second;
    LOG->Info(
        "DDR Mini lights: P%u %s -> %s (%s)", player + 1,
        input.device.c_str(), result[player].c_str(), input.topology.c_str());
    hidraw_by_topology.erase(match);
    ++player;
  }

  // Preserve compatibility with an unmodified console or unusual kernel
  // layout only when correlation found nothing.  With a partial match, do not
  // risk opening a stock symlink that aliases the already assigned pad.
  if (player == 0) {
    for (unsigned i = 0; i < result.size(); ++i) {
      result[i] = FALLBACK_PAD_PATHS[i];
      LOG->Warn(
          "DDR Mini lights: P%u falling back to %s", i + 1,
          result[i].c_str());
    }
  } else if (player < result.size()) {
    LOG->Warn(
        "DDR Mini lights: only %u correlated pad output(s) found; unmatched players are disabled",
        player);
  }
  return result;
}

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

  const std::array<std::string, NUM_PADS> pad_paths = DiscoverPadLights();
  for (unsigned i = 0; i < NUM_PADS; ++i) {
    if (pad_paths[i].empty()) {
      continue;
    }
    pad_fds_[i] = open(pad_paths[i].c_str(), O_WRONLY | O_CLOEXEC);
    if (pad_fds_[i] < 0) {
      LOG->Warn(
          "DDR Mini lights: failed to open P%u output %s: %s", i + 1,
          pad_paths[i].c_str(), std::strerror(errno));
    }
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
      // Physical marquee wiring:
      // yellow1=upper-left, yellow2=lower-left,
      // red1=lower-right, red2=upper-right.
      Brightness(ls->cabinet_lights_[LIGHT_MARQUEE_UP_LEFT]),
      Brightness(ls->cabinet_lights_[LIGHT_MARQUEE_LR_LEFT]),
      Brightness(ls->cabinet_lights_[LIGHT_MARQUEE_LR_RIGHT]),
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
