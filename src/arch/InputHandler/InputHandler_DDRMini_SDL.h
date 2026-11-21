#ifndef INPUT_HANDLER_DDRMINI_SDL_H
#define INPUT_HANDLER_DDRMINI_SDL_H

#include "InputHandler.h"

class InputHandler_DDRMini_SDL : public InputHandler {
 public:
  InputHandler_DDRMini_SDL();
  void Update() override;
  void GetDevicesAndDescriptions(
      std::vector<InputDeviceInfo>& devices_out) override;
};

#endif
