#ifndef LOADING_WINDOW_DDRMINI_H
#define LOADING_WINDOW_DDRMINI_H

#include "LoadingWindow.h"

#include <SDL.h>
#include <GLES/gl.h>

#include <string>

class LoadingWindow_DDRMini : public LoadingWindow {
 public:
  LoadingWindow_DDRMini();
  ~LoadingWindow_DDRMini() override;

  std::string Init() override;
  void SetText(std::string text) override;
  void SetSplash(const RageSurface* splash) override;
  void SetProgress(int progress) override;
  void SetTotalWork(int total_work) override;
  void SetIndeterminate(bool indeterminate) override;

 private:
  void Draw();
  void PumpEvents();
  void Destroy();
  void DrawQuad(float left, float top, float right, float bottom,
                float red, float green, float blue, float alpha,
                bool textured = false);
  void DrawText(float center_x, float center_y, float maximum_width);

  SDL_Window* window_;
  SDL_GLContext context_;
  GLuint splash_texture_;
  int splash_width_;
  int splash_height_;
  int texture_width_;
  int texture_height_;
  int drawable_width_;
  int drawable_height_;
  int animation_step_;
  bool initialized_video_;
  bool indeterminate_;
  std::string text_;
};

#endif
