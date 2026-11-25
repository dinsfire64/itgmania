#include "LoadingWindow_DDRMini.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageUtil.h"

namespace {
struct Glyph {
  char character;
  uint8_t columns[5];
};

// Five columns of seven bits, from left to right. This deliberately small
// startup font avoids depending on FontManager before it has been created.
constexpr Glyph kGlyphs[] = {
    {'A', {0x7e, 0x09, 0x09, 0x09, 0x7e}},
    {'B', {0x7f, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3e, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7f, 0x41, 0x41, 0x22, 0x1c}},
    {'E', {0x7f, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7f, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3e, 0x41, 0x49, 0x49, 0x7a}},
    {'H', {0x7f, 0x08, 0x08, 0x08, 0x7f}},
    {'I', {0x41, 0x41, 0x7f, 0x41, 0x41}},
    {'J', {0x20, 0x40, 0x41, 0x3f, 0x01}},
    {'K', {0x7f, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7f, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7f, 0x02, 0x0c, 0x02, 0x7f}},
    {'N', {0x7f, 0x04, 0x08, 0x10, 0x7f}},
    {'O', {0x3e, 0x41, 0x41, 0x41, 0x3e}},
    {'P', {0x7f, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3e, 0x41, 0x51, 0x21, 0x5e}},
    {'R', {0x7f, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7f, 0x01, 0x01}},
    {'U', {0x3f, 0x40, 0x40, 0x40, 0x3f}},
    {'V', {0x1f, 0x20, 0x40, 0x20, 0x1f}},
    {'W', {0x7f, 0x20, 0x18, 0x20, 0x7f}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'0', {0x3e, 0x51, 0x49, 0x45, 0x3e}},
    {'1', {0x00, 0x42, 0x7f, 0x40, 0x00}},
    {'2', {0x62, 0x51, 0x49, 0x49, 0x46}},
    {'3', {0x22, 0x41, 0x49, 0x49, 0x36}},
    {'4', {0x18, 0x14, 0x12, 0x7f, 0x10}},
    {'5', {0x2f, 0x49, 0x49, 0x49, 0x31}},
    {'6', {0x3e, 0x49, 0x49, 0x49, 0x32}},
    {'7', {0x01, 0x01, 0x71, 0x09, 0x07}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x26, 0x49, 0x49, 0x49, 0x3e}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {',', {0x00, 0x40, 0x20, 0x00, 0x00}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'(', {0x00, 0x1c, 0x22, 0x41, 0x00}},
    {')', {0x00, 0x41, 0x22, 0x1c, 0x00}},
    {'%', {0x23, 0x13, 0x08, 0x64, 0x62}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
};

const uint8_t* FindGlyph(char character) {
  for (const Glyph& glyph : kGlyphs) {
    if (glyph.character == character) {
      return glyph.columns;
    }
  }
  for (const Glyph& glyph : kGlyphs) {
    if (glyph.character == '?') {
      return glyph.columns;
    }
  }
  return nullptr;
}
}  // namespace

LoadingWindow_DDRMini::LoadingWindow_DDRMini()
    : window_(nullptr),
      context_(nullptr),
      splash_texture_(0),
      splash_width_(0),
      splash_height_(0),
      texture_width_(0),
      texture_height_(0),
      drawable_width_(0),
      drawable_height_(0),
      animation_step_(0),
      initialized_video_(false),
      indeterminate_(true) {}

LoadingWindow_DDRMini::~LoadingWindow_DDRMini() { Destroy(); }

std::string LoadingWindow_DDRMini::Init() {
  SDL_SetMainReady();
  if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
      return SDL_GetError();
    }
    initialized_video_ = true;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  window_ = SDL_CreateWindow(
      "ITGmania", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720,
      SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
  if (window_ == nullptr) {
    const std::string error = SDL_GetError();
    Destroy();
    return error;
  }

  context_ = SDL_GL_CreateContext(window_);
  if (context_ == nullptr) {
    const std::string error = SDL_GetError();
    Destroy();
    return error;
  }

  SDL_GL_SetSwapInterval(1);
  SDL_GL_GetDrawableSize(window_, &drawable_width_, &drawable_height_);
  glViewport(0, 0, drawable_width_, drawable_height_);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnableClientState(GL_VERTEX_ARRAY);
  Draw();
  return std::string();
}

void LoadingWindow_DDRMini::Destroy() {
  if (context_ != nullptr) {
    SDL_GL_MakeCurrent(window_, context_);
    if (splash_texture_ != 0) {
      glDeleteTextures(1, &splash_texture_);
      splash_texture_ = 0;
    }
    SDL_GL_DeleteContext(context_);
    context_ = nullptr;
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  if (initialized_video_) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    initialized_video_ = false;
  }
}

void LoadingWindow_DDRMini::PumpEvents() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
  }
}

void LoadingWindow_DDRMini::DrawQuad(
    float left, float top, float right, float bottom, float red, float green,
    float blue, float alpha, bool textured) {
  const GLfloat vertices[] = {
      left, top, right, top, left, bottom, right, bottom};
  const float texture_right =
      textured ? static_cast<float>(splash_width_) / texture_width_ : 0.0f;
  const float texture_bottom =
      textured ? static_cast<float>(splash_height_) / texture_height_ : 0.0f;
  const GLfloat texcoords[] = {
      0.0f, 0.0f, texture_right, 0.0f, 0.0f, texture_bottom, texture_right,
      texture_bottom};

  glColor4f(red, green, blue, alpha);
  glVertexPointer(2, GL_FLOAT, 0, vertices);
  if (textured) {
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
  } else {
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  }
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void LoadingWindow_DDRMini::DrawText(
    float center_x, float center_y, float maximum_width) {
  if (text_.empty()) {
    return;
  }

  std::vector<std::string> lines(1);
  for (char character : text_) {
    if (character == '\n') {
      lines.emplace_back();
    } else if (character != '\r') {
      lines.back() += character == '\t' ? ' ' : character;
    }
  }

  constexpr size_t kMaximumLines = 3;
  if (lines.size() > kMaximumLines) {
    lines.resize(kMaximumLines);
  }
  constexpr size_t kMaximumCharacters = 72;
  size_t longest_line = 0;
  for (std::string& line : lines) {
    if (line.size() > kMaximumCharacters) {
      line.resize(kMaximumCharacters - 3);
      line += "...";
    }
    longest_line = std::max(longest_line, line.size());
  }
  if (longest_line == 0) {
    return;
  }

  const float cell_size = std::max(
      2.0f, std::min(4.0f, maximum_width / (longest_line * 6.0f)));
  const float line_height = 7.0f * cell_size;
  const float line_advance = 9.0f * cell_size;
  const float block_height =
      line_height + static_cast<float>(lines.size() - 1) * line_advance;
  const float block_top = center_y - block_height * 0.5f;

  for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
    const std::string& line = lines[line_index];
    const float text_width =
        line.empty() ? 0.0f : line.size() * 6.0f * cell_size - cell_size;
    float character_left = center_x - text_width * 0.5f;
    const float text_top = block_top + line_index * line_advance;

    for (unsigned char raw_character : line) {
      if (raw_character != ' ') {
        const char character =
            raw_character < 128
                ? static_cast<char>(std::toupper(raw_character))
                : '?';
        const uint8_t* columns = FindGlyph(character);
        if (columns != nullptr) {
          for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
              if ((columns[column] & (1 << row)) != 0) {
                const float left = character_left + column * cell_size;
                const float top = text_top + row * cell_size;
                DrawQuad(left, top, left + cell_size, top + cell_size, 1.0f,
                         1.0f, 1.0f, 1.0f);
              }
            }
          }
        }
      }
      character_left += 6.0f * cell_size;
    }
  }
}

void LoadingWindow_DDRMini::Draw() {
  if (window_ == nullptr || context_ == nullptr || drawable_width_ <= 0 ||
      drawable_height_ <= 0) {
    return;
  }

  PumpEvents();
  SDL_GL_MakeCurrent(window_, context_);
  glViewport(0, 0, drawable_width_, drawable_height_);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrthof(0.0f, static_cast<float>(drawable_width_),
           static_cast<float>(drawable_height_), 0.0f, -1.0f, 1.0f);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glClearColor(0.02f, 0.12f, 0.16f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const float progress_left = drawable_width_ * 0.1f;
  const float progress_right = drawable_width_ * 0.9f;
  const float progress_top = drawable_height_ * 0.88f;
  const float progress_bottom = drawable_height_ * 0.92f;
  float splash_bottom = drawable_height_ * 0.08f;

  if (splash_texture_ != 0) {
    glBindTexture(GL_TEXTURE_2D, splash_texture_);
    const float maximum_width = progress_right - progress_left;
    const float maximum_height = drawable_height_ * 0.52f;
    const float scale = std::min(
        1.0f,
        std::min(maximum_width / splash_width_, maximum_height / splash_height_));
    const float splash_draw_width = splash_width_ * scale;
    const float splash_draw_height = splash_height_ * scale;
    const float splash_left = (drawable_width_ - splash_draw_width) * 0.5f;
    const float splash_top = drawable_height_ * 0.06f;
    splash_bottom = splash_top + splash_draw_height;
    DrawQuad(splash_left, splash_top, splash_left + splash_draw_width,
             splash_bottom, 1.0f, 1.0f, 1.0f, 1.0f, true);
  }

  const float text_center_y =
      splash_bottom + (progress_top - splash_bottom) * 0.5f;
  DrawText(drawable_width_ * 0.5f, text_center_y,
           progress_right - progress_left);

  DrawQuad(progress_left - 4.0f, progress_top - 4.0f,
           progress_right + 4.0f, progress_bottom + 4.0f, 0.0f, 0.0f, 0.0f,
           0.75f);

  float fill_left = progress_left;
  float fill_right = progress_left;
  if (indeterminate_) {
    constexpr int kAnimationSteps = 40;
    const float fraction = static_cast<float>(animation_step_++ % kAnimationSteps) /
                           static_cast<float>(kAnimationSteps - 1);
    const float segment = (progress_right - progress_left) * 0.2f;
    fill_left = progress_left +
                fraction * ((progress_right - progress_left) - segment);
    fill_right = fill_left + segment;
  } else if (total_work_ > 0) {
    const float fraction = std::max(
        0.0f, std::min(1.0f, static_cast<float>(progress_) / total_work_));
    fill_right =
        progress_left + fraction * (progress_right - progress_left);
  }
  DrawQuad(fill_left, progress_top, fill_right, progress_bottom, 0.1f, 0.75f,
           1.0f, 1.0f);

  SDL_GL_SwapWindow(window_);
}

void LoadingWindow_DDRMini::SetText(std::string text) {
  text_ = std::move(text);
  Draw();
}

void LoadingWindow_DDRMini::SetSplash(const RageSurface* splash) {
  if (splash == nullptr || context_ == nullptr) {
    return;
  }

  RageSurface* converted = nullptr;
  RageSurfaceUtils::ConvertSurface(
      splash, converted, splash->w_, splash->h_, 32, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0xff000000);
  const RageSurface* upload = converted != nullptr ? converted : splash;

  splash_width_ = upload->w_;
  splash_height_ = upload->h_;
  texture_width_ = power_of_two(splash_width_);
  texture_height_ = power_of_two(splash_height_);
  std::vector<uint8_t> pixels(
      static_cast<size_t>(splash_width_) * splash_height_ * 4);
  for (int row = 0; row < splash_height_; ++row) {
    std::memcpy(pixels.data() + static_cast<size_t>(row) * splash_width_ * 4,
                upload->pixels_ + static_cast<size_t>(row) * upload->pitch_,
                static_cast<size_t>(splash_width_) * 4);
  }

  SDL_GL_MakeCurrent(window_, context_);
  if (splash_texture_ != 0) {
    glDeleteTextures(1, &splash_texture_);
  }
  glGenTextures(1, &splash_texture_);
  glBindTexture(GL_TEXTURE_2D, splash_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_width_, texture_height_, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, splash_width_, splash_height_, GL_RGBA,
                  GL_UNSIGNED_BYTE, pixels.data());
  delete converted;
  Draw();
}

void LoadingWindow_DDRMini::SetProgress(int progress) {
  LoadingWindow::SetProgress(progress);
  Draw();
}

void LoadingWindow_DDRMini::SetTotalWork(int total_work) {
  LoadingWindow::SetTotalWork(total_work);
  Draw();
}

void LoadingWindow_DDRMini::SetIndeterminate(bool indeterminate) {
  indeterminate_ = indeterminate;
  animation_step_ = 0;
  Draw();
}
