#define SDL_MAIN_HANDLED
#include "RageDisplay_DDRMini.h"

#include <SDL.h>
#include <GLES/gl.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <cstdint>
#include <set>
#include <vector>

#include "DisplaySpec.h"
#include "RageLog.h"
#include "RageMath.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "global.h"

namespace {
RageDisplay::RagePixelFormatDesc formats[NUM_RagePixelFormat] = {
  {32,{0x000000FF,0x0000FF00,0x00FF0000,0xFF000000}},
  {32,{0x0000FF00,0x00FF0000,0xFF000000,0x000000FF}},
  {16,{0xF000,0x0F00,0x00F0,0x000F}}, {16,{0xF800,0x07C0,0x003E,0x0001}},
  {16,{0xF800,0x07E0,0x001F,0}}, {24,{0x0000FF,0x00FF00,0xFF0000,0}},
  {8,{0,0,0,0}}, {24,{0x0000FF,0x00FF00,0xFF0000,0}},
  {16,{0x001F,0x03E0,0x7C00,0x8000}}, {16,{0x7C00,0x03E0,0x001F,0}}
};

bool gl_format(RagePixelFormat pf, GLenum& format, GLenum& type, int& bytes) {
  switch (pf) {
    case RagePixelFormat_RGBA8: format=GL_RGBA; type=GL_UNSIGNED_BYTE; bytes=4; return true;
    case RagePixelFormat_RGBA4: format=GL_RGBA; type=GL_UNSIGNED_SHORT_4_4_4_4; bytes=2; return true;
    case RagePixelFormat_RGB5A1: format=GL_RGBA; type=GL_UNSIGNED_SHORT_5_5_5_1; bytes=2; return true;
    case RagePixelFormat_RGB5: format=GL_RGB; type=GL_UNSIGNED_SHORT_5_6_5; bytes=2; return true;
    case RagePixelFormat_RGB8: format=GL_RGB; type=GL_UNSIGNED_BYTE; bytes=3; return true;
    default: return false;
  }
}

// Keep the upload format for each GL texture. The source RageSurface can have
// a completely different layout, particularly for dynamic texture updates.
std::map<GLuint, RagePixelFormat> texture_formats;

// RageSurfaceUtils performs channel conversion and palette expansion using
// the engine's own format semantics. Never reinterpret arbitrary surface bytes
// as GL_RGBA / GL_RGB without converting first.
RageSurface* converted_upload_surface(RageSurface* src, RagePixelFormat pf) {
  const auto& desc = formats[pf];
  RageSurface* converted = nullptr;
  if (!RageSurfaceUtils::ConvertSurface(
          src, converted, src->w_, src->h_, desc.bpp_,
          desc.masks_[0], desc.masks_[1], desc.masks_[2], desc.masks_[3])) {
    return nullptr; // already in the exact format
  }
  return converted;
}

void setup_vertices(const RageSpriteVertex* v, int count) {
  static std::vector<float> pos, normal, tex;
  static std::vector<unsigned char> color;
  pos.resize(count*3); normal.resize(count*3); tex.resize(count*2); color.resize(count*4);
  for (int i=0; i<count; ++i) {
    for (int j=0;j<3;++j) { pos[i*3+j]=v[i].p_[j]; normal[i*3+j]=v[i].n_[j]; }
    tex[i*2]=v[i].t_.x_; tex[i*2+1]=v[i].t_.y_;
    color[i*4]=v[i].c_.r_; color[i*4+1]=v[i].c_.g_;
    color[i*4+2]=v[i].c_.b_; color[i*4+3]=v[i].c_.a_;
  }
  glVertexPointer(3,GL_FLOAT,0,pos.data()); glColorPointer(4,GL_UNSIGNED_BYTE,0,color.data());
  glTexCoordPointer(2,GL_FLOAT,0,tex.data()); glNormalPointer(GL_FLOAT,0,normal.data());
}

class GLESGeometry : public RageCompiledGeometry {
 public:
  std::vector<RageModelVertex> vertices; std::vector<msTriangle> triangles;
  void Allocate(const std::vector<msMesh>&) override {}
  void Change(const std::vector<msMesh>& meshes) override {
    vertices.resize(GetTotalVertices()); triangles.resize(GetTotalTriangles());
    for (size_t i=0;i<meshes.size();++i) {
      std::copy(meshes[i].vertices_.begin(),meshes[i].vertices_.end(),vertices.begin()+mesh_info_[i].vertex_start_);
      for (size_t j=0;j<meshes[i].triangles_.size();++j) {
        triangles[mesh_info_[i].triangle_start_+j]=meshes[i].triangles_[j];
        for(int k=0;k<3;++k) triangles[mesh_info_[i].triangle_start_+j].vertex_indices_[k]+=mesh_info_[i].vertex_start_;
      }
    }
  }
  void Draw(int m) const override {
    const MeshInfo& mi=mesh_info_[m];
    glVertexPointer(3,GL_FLOAT,sizeof(RageModelVertex),&vertices[0].p_);
    glNormalPointer(GL_FLOAT,sizeof(RageModelVertex),&vertices[0].n_);
    glTexCoordPointer(2,GL_FLOAT,sizeof(RageModelVertex),&vertices[0].t_);
    glDisableClientState(GL_COLOR_ARRAY); glColor4f(1,1,1,1);
    glDrawElements(GL_TRIANGLES,mi.triangle_count_*3,GL_UNSIGNED_SHORT,&triangles[mi.triangle_start_]);
    glEnableClientState(GL_COLOR_ARRAY);
  }
};
}

RageDisplay_DDRMini::RageDisplay_DDRMini() { LOG->MapLog("renderer","Current renderer: DDR Mini GLES"); }
RageDisplay_DDRMini::~RageDisplay_DDRMini() {
  texture_formats.clear(); if(context_) SDL_GL_DeleteContext(context_); if(window_) SDL_DestroyWindow(window_); SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
std::string RageDisplay_DDRMini::Init(const VideoModeParams& p, bool) {
  SDL_SetMainReady(); if(SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0) return SDL_GetError();
  bool reload=false; return SetVideoMode(p,reload);
}
std::string RageDisplay_DDRMini::TryVideoMode(const VideoModeParams& p, bool& reload) {
  if(context_) { texture_formats.clear(); SDL_GL_DeleteContext(context_); context_=nullptr; }
  if(window_) { SDL_DestroyWindow(window_); window_=nullptr; }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,1); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,16); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
  window_=SDL_CreateWindow(p.window_title_.c_str(),SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
                           p.width_,p.height_,SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN_DESKTOP);
  if (!window_) {
    return SDL_GetError();
  }
  context_ = SDL_GL_CreateContext(window_);
  if (!context_) {
    return SDL_GetError();
  }
  SDL_GL_SetSwapInterval(p.vsync_?1:0); int w,h; SDL_GL_GetDrawableSize(window_,&w,&h);
  params_=ActualVideoModeParams(p,w,h,false); params_.width_=w; params_.height_=h; reload=true;
  glViewport(0,0,w,h); glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnableClientState(GL_NORMAL_ARRAY); SetDefaultRenderStates();
  LOG->Info("DDR Mini SDL video driver: %s; GLES vendor: %s; renderer: %s; version: %s",
    SDL_GetCurrentVideoDriver(),glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
  return "";
}
void RageDisplay_DDRMini::GetDisplaySpecs(DisplaySpecs& out) const { out.clear(); out.insert(DisplaySpec("DDRMini","DDR Mini",{1280,720,60})); }
const RageDisplay::RagePixelFormatDesc* RageDisplay_DDRMini::GetPixelFormatDesc(RagePixelFormat p) const { return p>=0&&p<NUM_RagePixelFormat?&formats[p]:nullptr; }
bool RageDisplay_DDRMini::BeginFrame(){ glClear(GL_COLOR_BUFFER_BIT); return RageDisplay::BeginFrame(); }
void RageDisplay_DDRMini::EndFrame(){ FrameLimitBeforeVsync(); SDL_GL_SwapWindow(window_); FrameLimitAfterVsync(); ProcessStatsOnFlip(); }
void RageDisplay_DDRMini::SendMatrices(){ RageMatrix p,m; RageMatrixMultiply(&p,GetCentering(),GetProjectionTop()); RageMatrixMultiply(&m,GetViewTop(),GetWorldTop()); glMatrixMode(GL_PROJECTION);glLoadMatrixf((float*)&p);glMatrixMode(GL_MODELVIEW);glLoadMatrixf((float*)&m);glMatrixMode(GL_TEXTURE);glLoadMatrixf((float*)GetTextureTop());glMatrixMode(GL_MODELVIEW); }
void RageDisplay_DDRMini::Draw(const RageSpriteVertex v[],int n,unsigned mode){ SendMatrices();setup_vertices(v,n);glDrawArrays(mode,0,n); }
void RageDisplay_DDRMini::DrawQuadsInternal(const RageSpriteVertex v[], int n) {
  // GL_QUADS uses perimeter order 0,1,2,3, NOT triangle-strip order.
  // Split each quad along diagonal 0-2 to preserve texture interpolation.
  if (n < 4) return;
  SendMatrices();
  const int quads_per_batch = 16384; // 4*16384 vertices fit GL_UNSIGNED_SHORT
  static std::vector<unsigned short> indices;
  for (int first = 0; first + 3 < n; first += quads_per_batch * 4) {
    const int quads = std::min(quads_per_batch, (n - first) / 4);
    indices.resize(static_cast<size_t>(quads) * 6);
    for (int q = 0; q < quads; ++q) {
      const unsigned short a = static_cast<unsigned short>(q * 4);
      const size_t i = static_cast<size_t>(q) * 6;
      indices[i + 0] = a;
      indices[i + 1] = a + 1;
      indices[i + 2] = a + 2;
      indices[i + 3] = a;
      indices[i + 4] = a + 2;
      indices[i + 5] = a + 3;
    }
    setup_vertices(v + first, quads * 4);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()),
                   GL_UNSIGNED_SHORT, indices.data());
  }
}
void RageDisplay_DDRMini::DrawQuadStripInternal(const RageSpriteVertex v[],int n){Draw(v,n,GL_TRIANGLE_STRIP);} void RageDisplay_DDRMini::DrawFanInternal(const RageSpriteVertex v[],int n){Draw(v,n,GL_TRIANGLE_FAN);} void RageDisplay_DDRMini::DrawStripInternal(const RageSpriteVertex v[],int n){Draw(v,n,GL_TRIANGLE_STRIP);} void RageDisplay_DDRMini::DrawTrianglesInternal(const RageSpriteVertex v[],int n){Draw(v,n,GL_TRIANGLES);}
void RageDisplay_DDRMini::DrawLineStripInternal(const RageSpriteVertex v[],int n,float w){glLineWidth(w);Draw(v,n,GL_LINE_STRIP);}
void RageDisplay_DDRMini::DrawSymmetricQuadStripInternal(const RageSpriteVertex v[], int n) {
  // Match RageDisplay_Legacy::DrawSymmetricQuadStripInternal: four
  // triangles for each three-vertex segment, sharing its center vertex.
  if (n < 6) return;
  const int pieces = (n - 3) / 3;
  SendMatrices();
  static std::vector<unsigned short> indices;
  // A piece references up to (piece*3+5), so keep batches below 65536.
  const int max_pieces = 21844;
  for (int first = 0; first < pieces; first += max_pieces) {
    const int count = std::min(max_pieces, pieces - first);
    indices.resize(static_cast<size_t>(count) * 12);
    for (int i = 0; i < count; ++i) {
      const unsigned short a = static_cast<unsigned short>(i * 3);
      const size_t j = static_cast<size_t>(i) * 12;
      const unsigned short t[12] = {
        static_cast<unsigned short>(a+1), static_cast<unsigned short>(a+3), a,
        static_cast<unsigned short>(a+1), static_cast<unsigned short>(a+4), static_cast<unsigned short>(a+3),
        static_cast<unsigned short>(a+1), static_cast<unsigned short>(a+5), static_cast<unsigned short>(a+4),
        static_cast<unsigned short>(a+1), static_cast<unsigned short>(a+2), static_cast<unsigned short>(a+5)
      };
      std::copy(t, t + 12, indices.begin() + j);
    }
    setup_vertices(v + first * 3, count * 3 + 3);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()),
                   GL_UNSIGNED_SHORT, indices.data());
  }
}
void RageDisplay_DDRMini::DrawCompiledGeometryInternal(const RageCompiledGeometry* g,int m){SendMatrices();g->Draw(m);}
RageCompiledGeometry* RageDisplay_DDRMini::CreateCompiledGeometry(){return new GLESGeometry;} void RageDisplay_DDRMini::DeleteCompiledGeometry(RageCompiledGeometry* p){delete p;}
bool RageDisplay_DDRMini::SupportsTextureFormat(RagePixelFormat p,bool){GLenum f,t;int b;return gl_format(p,f,t,b);}
uintptr_t RageDisplay_DDRMini::CreateTexture(RagePixelFormat p, RageSurface* s, bool) {
  GLenum format, type;
  int bytes;
  if (!gl_format(p, format, type, bytes) || !s) return 0;

  RageSurface* converted = converted_upload_surface(s, p);
  const RageSurface* upload = converted ? converted : s;
  std::vector<uint8_t> tight(static_cast<size_t>(upload->w_) * upload->h_ * bytes);
  for (int row = 0; row < upload->h_; ++row)
    memcpy(tight.data() + static_cast<size_t>(row) * upload->w_ * bytes,
           upload->pixels_ + static_cast<size_t>(row) * upload->pitch_,
           static_cast<size_t>(upload->w_) * bytes);

  GLuint id = 0;
  glGenTextures(1, &id);
  if (!id) { delete converted; return 0; }
  glBindTexture(GL_TEXTURE_2D, id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, format, upload->w_, upload->h_, 0,
               format, type, tight.data());
  delete converted;
  if (glGetError() != GL_NO_ERROR) {
    glDeleteTextures(1, &id);
    return 0;
  }
  texture_formats[id] = p;
  return id;
}

void RageDisplay_DDRMini::UpdateTexture(uintptr_t handle, RageSurface* s,
                                         int x, int y, int w, int h) {
  const GLuint id = static_cast<GLuint>(handle);
  const auto it = texture_formats.find(id);
  if (it == texture_formats.end() || !s || w <= 0 || h <= 0 ||
      w > s->w_ || h > s->h_) return;

  GLenum format, type;
  int bytes;
  if (!gl_format(it->second, format, type, bytes)) return;
  RageSurface* converted = converted_upload_surface(s, it->second);
  const RageSurface* upload = converted ? converted : s;
  std::vector<uint8_t> tight(static_cast<size_t>(w) * h * bytes);
  // ITGmania supplies the update rectangle at the start of the source surface;
  // x/y are the destination coordinates in the existing GL texture.
  for (int row = 0; row < h; ++row)
    memcpy(tight.data() + static_cast<size_t>(row) * w * bytes,
           upload->pixels_ + static_cast<size_t>(row) * upload->pitch_,
           static_cast<size_t>(w) * bytes);
  glBindTexture(GL_TEXTURE_2D, id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, format, type, tight.data());
  delete converted;
}

void RageDisplay_DDRMini::DeleteTexture(uintptr_t handle) {
  GLuint id = static_cast<GLuint>(handle);
  texture_formats.erase(id);
  if (id) glDeleteTextures(1, &id);
}
void RageDisplay_DDRMini::ClearAllTextures(){glBindTexture(GL_TEXTURE_2D,0);} void RageDisplay_DDRMini::SetTexture(TextureUnit u,uintptr_t t){if(u!=TextureUnit_1)return;if(t){glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,t);}else glDisable(GL_TEXTURE_2D);}
void RageDisplay_DDRMini::SetTextureMode(TextureUnit,TextureMode m){glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,m==TextureMode_Add?GL_ADD:GL_MODULATE);} void RageDisplay_DDRMini::SetTextureWrapping(TextureUnit,bool b){glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,b?GL_REPEAT:GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,b?GL_REPEAT:GL_CLAMP_TO_EDGE);} int RageDisplay_DDRMini::GetMaxTextureSize()const{GLint n;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&n);return n;} void RageDisplay_DDRMini::SetTextureFiltering(TextureUnit,bool b){glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,b?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,b?GL_LINEAR:GL_NEAREST);}
void RageDisplay_DDRMini::SetBlendMode(BlendMode m){glEnable(GL_BLEND);GLenum s=GL_SRC_ALPHA,d=GL_ONE_MINUS_SRC_ALPHA;if(m==BLEND_ADD)d=GL_ONE;else if(m==BLEND_MODULATE){s=GL_ZERO;d=GL_SRC_COLOR;}else if(m==BLEND_COPY_SRC){s=GL_ONE;d=GL_ZERO;}else if(m==BLEND_NO_EFFECT){s=GL_ZERO;d=GL_ONE;}glBlendFunc(s,d);}
bool RageDisplay_DDRMini::IsZWriteEnabled()const{GLboolean b;glGetBooleanv(GL_DEPTH_WRITEMASK,&b);return b;}void RageDisplay_DDRMini::SetZWrite(bool b){glDepthMask(b);}void RageDisplay_DDRMini::SetZBias(float f){if(f){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(0,f);}else glDisable(GL_POLYGON_OFFSET_FILL);}void RageDisplay_DDRMini::SetZTestMode(ZTestMode m){if(m==ZTEST_OFF)glDisable(GL_DEPTH_TEST);else{glEnable(GL_DEPTH_TEST);glDepthFunc(m==ZTEST_WRITE_ON_FAIL?GL_GREATER:GL_LEQUAL);}}void RageDisplay_DDRMini::ClearZBuffer(){glClear(GL_DEPTH_BUFFER_BIT);}void RageDisplay_DDRMini::SetCullMode(CullMode m){if(m==CULL_NONE)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(m==CULL_BACK?GL_BACK:GL_FRONT);}}void RageDisplay_DDRMini::SetAlphaTest(bool b){if(b){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.01f);}else glDisable(GL_ALPHA_TEST);}
void RageDisplay_DDRMini::SetMaterial(const RageColor&e,const RageColor&a,const RageColor&d,const RageColor&s,float sh){glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,&e.r_);glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT,&a.r_);glMaterialfv(GL_FRONT_AND_BACK,GL_DIFFUSE,&d.r_);glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,&s.r_);glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,sh);}void RageDisplay_DDRMini::SetLighting(bool b){if(b)glEnable(GL_LIGHTING);else glDisable(GL_LIGHTING);}void RageDisplay_DDRMini::SetLightOff(int i){glDisable(GL_LIGHT0+i);}void RageDisplay_DDRMini::SetLightDirectional(int i,const RageColor&a,const RageColor&d,const RageColor&s,const RageVector3&v){GLfloat p[]={-v.x_,-v.y_,-v.z_,0};glEnable(GL_LIGHT0+i);glLightfv(GL_LIGHT0+i,GL_AMBIENT,&a.r_);glLightfv(GL_LIGHT0+i,GL_DIFFUSE,&d.r_);glLightfv(GL_LIGHT0+i,GL_SPECULAR,&s.r_);glLightfv(GL_LIGHT0+i,GL_POSITION,p);}void RageDisplay_DDRMini::SetSphereEnvironmentMapping(TextureUnit,bool){/* GLES 1.1 has no core sphere-map texgen. */}
RageSurface* RageDisplay_DDRMini::CreateScreenshot(){RageSurface* s=CreateSurface(params_.width_,params_.height_,24,0xFF0000,0x00FF00,0x0000FF,0);glReadPixels(0,0,s->w_,s->h_,GL_RGB,GL_UNSIGNED_BYTE,s->pixels_);return s;}
