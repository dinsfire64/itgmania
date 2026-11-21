#ifndef RAGE_DISPLAY_DDRMINI_H
#define RAGE_DISPLAY_DDRMINI_H

#include "RageDisplay.h"

struct SDL_Window;
typedef void* SDL_GLContext;

class RageDisplay_DDRMini : public RageDisplay {
 public:
  RageDisplay_DDRMini();
  ~RageDisplay_DDRMini() override;
  std::string Init(const VideoModeParams&, bool) override;
  std::string GetApiDescription() const override { return "OpenGL ES 1 / SDL2"; }
  void GetDisplaySpecs(DisplaySpecs&) const override;
  const RagePixelFormatDesc* GetPixelFormatDesc(RagePixelFormat) const override;
  bool BeginFrame() override;
  void EndFrame() override;
  ActualVideoModeParams GetActualVideoModeParams() const override { return params_; }
  void SetBlendMode(BlendMode) override;
  bool SupportsTextureFormat(RagePixelFormat, bool = false) override;
  bool SupportsPerVertexMatrixScale() override { return false; }
  uintptr_t CreateTexture(RagePixelFormat, RageSurface*, bool) override;
  void UpdateTexture(uintptr_t, RageSurface*, int, int, int, int) override;
  void DeleteTexture(uintptr_t) override;
  void ClearAllTextures() override;
  int GetNumTextureUnits() override { return 1; }
  void SetTexture(TextureUnit, uintptr_t) override;
  void SetTextureMode(TextureUnit, TextureMode) override;
  void SetTextureWrapping(TextureUnit, bool) override;
  int GetMaxTextureSize() const override;
  void SetTextureFiltering(TextureUnit, bool) override;
  bool IsZWriteEnabled() const override;
  void SetZWrite(bool) override;
  void SetZBias(float) override;
  void SetZTestMode(ZTestMode) override;
  void ClearZBuffer() override;
  void SetCullMode(CullMode) override;
  void SetAlphaTest(bool) override;
  void SetMaterial(const RageColor&, const RageColor&, const RageColor&,
                   const RageColor&, float) override;
  void SetLighting(bool) override;
  void SetLightOff(int) override;
  void SetLightDirectional(int, const RageColor&, const RageColor&,
                           const RageColor&, const RageVector3&) override;
  void SetSphereEnvironmentMapping(TextureUnit, bool) override;
  void SetCelShaded(int) override {}
  RageCompiledGeometry* CreateCompiledGeometry() override;
  void DeleteCompiledGeometry(RageCompiledGeometry*) override;

 protected:
  void DrawQuadsInternal(const RageSpriteVertex[], int) override;
  void DrawQuadStripInternal(const RageSpriteVertex[], int) override;
  void DrawFanInternal(const RageSpriteVertex[], int) override;
  void DrawStripInternal(const RageSpriteVertex[], int) override;
  void DrawTrianglesInternal(const RageSpriteVertex[], int) override;
  void DrawCompiledGeometryInternal(const RageCompiledGeometry*, int) override;
  void DrawLineStripInternal(const RageSpriteVertex[], int, float) override;
  void DrawSymmetricQuadStripInternal(const RageSpriteVertex[], int) override;
  std::string TryVideoMode(const VideoModeParams&, bool&) override;
  RageSurface* CreateScreenshot() override;

 private:
  void SendMatrices();
  void Draw(const RageSpriteVertex[], int, unsigned);
  SDL_Window* window_ = nullptr;
  SDL_GLContext context_ = nullptr;
  ActualVideoModeParams params_;
};

#endif
