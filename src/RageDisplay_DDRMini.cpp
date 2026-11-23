#define SDL_MAIN_HANDLED
#include "RageDisplay_DDRMini.h"

#include <SDL.h>
#include <GLES/gl.h>
#include <algorithm>
#include <cmath>
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
#include "RageUtil.h"
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
bool sphere_environment_mapping = false;

void generate_sphere_map_texcoords(const std::vector<RageModelVertex>& vertices,
                                   std::vector<RageVector2>& texcoords) {
  GLfloat model_view[16];
  glGetFloatv(GL_MODELVIEW_MATRIX, model_view);
  texcoords.resize(vertices.size());

  for (size_t i = 0; i < vertices.size(); ++i) {
    const RageVector3& position = vertices[i].p_;
    const RageVector3& normal = vertices[i].n_;

    // GL_SPHERE_MAP operates in eye space. Models using this path are
    // uniformly scaled, so normalizing after applying the model-view 3x3 is
    // equivalent to using its inverse-transpose for their normals.
    RageVector3 p(
        model_view[0] * position.x_ + model_view[4] * position.y_ +
            model_view[8] * position.z_ + model_view[12],
        model_view[1] * position.x_ + model_view[5] * position.y_ +
            model_view[9] * position.z_ + model_view[13],
        model_view[2] * position.x_ + model_view[6] * position.y_ +
            model_view[10] * position.z_ + model_view[14]);
    RageVector3 n(
        model_view[0] * normal.x_ + model_view[4] * normal.y_ +
            model_view[8] * normal.z_,
        model_view[1] * normal.x_ + model_view[5] * normal.y_ +
            model_view[9] * normal.z_,
        model_view[2] * normal.x_ + model_view[6] * normal.y_ +
            model_view[10] * normal.z_);

    const float p_length =
        std::sqrt(p.x_ * p.x_ + p.y_ * p.y_ + p.z_ * p.z_);
    const float n_length =
        std::sqrt(n.x_ * n.x_ + n.y_ * n.y_ + n.z_ * n.z_);
    if (p_length <= 0.000001f || n_length <= 0.000001f) {
      texcoords[i] = RageVector2(0.5f, 0.5f);
      continue;
    }

    p.x_ /= p_length;
    p.y_ /= p_length;
    p.z_ /= p_length;
    n.x_ /= n_length;
    n.y_ /= n_length;
    n.z_ /= n_length;

    const float dot = p.x_ * n.x_ + p.y_ * n.y_ + p.z_ * n.z_;
    const RageVector3 reflection(p.x_ - 2.0f * dot * n.x_,
                                 p.y_ - 2.0f * dot * n.y_,
                                 p.z_ - 2.0f * dot * n.z_);
    const float m = 2.0f * std::sqrt(
        reflection.x_ * reflection.x_ + reflection.y_ * reflection.y_ +
        (reflection.z_ + 1.0f) * (reflection.z_ + 1.0f));
    if (m <= 0.000001f) {
      texcoords[i] = RageVector2(0.5f, 0.5f);
    } else {
      texcoords[i] = RageVector2(reflection.x_ / m + 0.5f,
                                 reflection.y_ / m + 0.5f);
    }
  }
}

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
  mutable std::vector<RageVector2> sphere_texcoords;
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
    if (vertices.empty()) return;
    const MeshInfo& mi=mesh_info_[m];
    glVertexPointer(3,GL_FLOAT,sizeof(RageModelVertex),&vertices[0].p_);
    glNormalPointer(GL_FLOAT,sizeof(RageModelVertex),&vertices[0].n_);
    if (sphere_environment_mapping) {
      generate_sphere_map_texcoords(vertices, sphere_texcoords);
      glTexCoordPointer(2, GL_FLOAT, 0, sphere_texcoords.data());
    } else {
      glTexCoordPointer(2,GL_FLOAT,sizeof(RageModelVertex),&vertices[0].t_);
    }
    glDisableClientState(GL_COLOR_ARRAY);

    if (mi.needs_texture_matrix_scale_) {
      // MilkShape vertex flags can request that animated texture translation
      // not affect a mesh. The mine model uses this to keep its outer ring
      // fixed while the model animation rotates underneath it. Match the
      // software desktop OpenGL renderer, which removes translation from the
      // texture matrix for such meshes.
      RageMatrix texture_matrix;
      glGetFloatv(GL_TEXTURE_MATRIX,
                  reinterpret_cast<GLfloat*>(&texture_matrix));
      texture_matrix.m_[3][0] = 0.0f;
      texture_matrix.m_[3][1] = 0.0f;
      texture_matrix.m_[3][2] = 0.0f;
      glMatrixMode(GL_TEXTURE);
      glLoadMatrixf(reinterpret_cast<const GLfloat*>(&texture_matrix));
      glMatrixMode(GL_MODELVIEW);
    }

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
bool RageDisplay_DDRMini::BeginFrame() {
  // Match the legacy renderer: depth writes must be enabled while clearing,
  // regardless of the state left by the final actor in the previous frame.
  glViewport(0, 0, params_.window_width_, params_.window_height_);
  glClearColor(0, 0, 0, 0);
  glDepthMask(GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  return RageDisplay::BeginFrame();
}
void RageDisplay_DDRMini::EndFrame() {
  FrameLimitBeforeVsync();
  SDL_GL_SwapWindow(window_);
  FrameLimitAfterVsync();
  // Keep the GPU close to engine time; queued frames make rhythm-game input
  // and audio synchronization visibly late.
  glFinish();
  RageDisplay::EndFrame();
}
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
  // ITGmania computes texture coordinates against power-of-two dimensions.
  // GLES 1 implementations also need not support arbitrary NPOT textures.
  // Allocate the padded resource first, then upload only the image rectangle.
  const int texture_width = power_of_two(upload->w_);
  const int texture_height = power_of_two(upload->h_);
  while (glGetError() != GL_NO_ERROR) {}
  glTexImage2D(GL_TEXTURE_2D, 0, format, texture_width, texture_height, 0,
               format, type, nullptr);
  GLenum texture_error = glGetError();
  if (texture_error == GL_NO_ERROR && upload->pixels_) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, upload->w_, upload->h_, format,
                    type, tight.data());
    texture_error = glGetError();
  }
  delete converted;
  if (texture_error != GL_NO_ERROR) {
    LOG->Warn(
        "DDR Mini GLES texture upload failed: error=0x%x image=%dx%d texture=%dx%d format=%d",
        texture_error, s->w_, s->h_, texture_width, texture_height,
        static_cast<int>(p));
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
void RageDisplay_DDRMini::SetBlendMode(BlendMode mode) {
  glEnable(GL_BLEND);
  GLenum source = GL_SRC_ALPHA;
  GLenum dest = GL_ONE_MINUS_SRC_ALPHA;
  switch (mode) {
    case BLEND_NORMAL: break;
    case BLEND_ADD: dest = GL_ONE; break;
    case BLEND_SUBTRACT:
      // GLES 1.1 has no core blend equation. Additive blending is a safer
      // degradation than drawing the source as an opaque black mask.
      dest = GL_ONE;
      break;
    case BLEND_MODULATE: source = GL_ZERO; dest = GL_SRC_COLOR; break;
    case BLEND_COPY_SRC: source = GL_ONE; dest = GL_ZERO; break;
    case BLEND_ALPHA_MASK:
    case BLEND_ALPHA_KNOCK_OUT:
      // Separate RGB/alpha blending is extension-only. Preserve destination
      // color; losing the mask is preferable to blacking out the background.
      source = GL_ZERO; dest = GL_ONE; break;
    case BLEND_ALPHA_MULTIPLY: dest = GL_ZERO; break;
    case BLEND_WEIGHTED_MULTIPLY:
      source = GL_DST_COLOR; dest = GL_SRC_COLOR; break;
    case BLEND_INVERT_DEST: source = GL_ONE; dest = GL_ONE; break;
    case BLEND_NO_EFFECT: source = GL_ZERO; dest = GL_ONE; break;
    default: break;
  }
  glBlendFunc(source, dest);
}
bool RageDisplay_DDRMini::IsZWriteEnabled() const {
  GLboolean enabled = GL_FALSE;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &enabled);
  return enabled == GL_TRUE;
}
void RageDisplay_DDRMini::SetZWrite(bool enabled) { glDepthMask(enabled); }
void RageDisplay_DDRMini::SetZBias(float bias) {
  const float near_value = 0.05f * (1.0f - bias);
  const float far_value = 1.0f - 0.05f * bias;
  glDepthRangef(near_value, far_value);
}
void RageDisplay_DDRMini::SetZTestMode(ZTestMode mode) {
  // GL_ALWAYS is not equivalent to disabling depth testing: actors may enable
  // depth writes independently and expect those writes to be retained.
  glEnable(GL_DEPTH_TEST);
  switch (mode) {
    case ZTEST_OFF: glDepthFunc(GL_ALWAYS); break;
    case ZTEST_WRITE_ON_PASS: glDepthFunc(GL_LEQUAL); break;
    case ZTEST_WRITE_ON_FAIL: glDepthFunc(GL_GREATER); break;
    default: glDepthFunc(GL_ALWAYS); break;
  }
}
void RageDisplay_DDRMini::ClearZBuffer() {
  const bool was_writable = IsZWriteEnabled();
  glDepthMask(GL_TRUE);
  glClear(GL_DEPTH_BUFFER_BIT);
  glDepthMask(was_writable);
}
void RageDisplay_DDRMini::SetCullMode(CullMode m){if(m==CULL_NONE)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(m==CULL_BACK?GL_BACK:GL_FRONT);}}void RageDisplay_DDRMini::SetAlphaTest(bool b){if(b){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.01f);}else glDisable(GL_ALPHA_TEST);}
void RageDisplay_DDRMini::SetMaterial(const RageColor& emissive,
                                      const RageColor& ambient,
                                      const RageColor& diffuse,
                                      const RageColor& specular,
                                      float shininess) {
  GLboolean lighting = GL_FALSE;
  glGetBooleanv(GL_LIGHTING, &lighting);
  if (lighting) {
    glMaterialfv(GL_FRONT, GL_EMISSION, &emissive.r_);
    glMaterialfv(GL_FRONT, GL_AMBIENT, &ambient.r_);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, &diffuse.r_);
    glMaterialfv(GL_FRONT, GL_SPECULAR, &specular.r_);
    glMaterialf(GL_FRONT, GL_SHININESS, shininess);
  } else {
    // Material state has no effect without GL_LIGHTING. Preserve the actor's
    // diffuse alpha while approximating its emissive and ambient RGB terms.
    const RageColor color(diffuse.r_ + emissive.r_ + ambient.r_,
                          diffuse.g_ + emissive.g_ + ambient.g_,
                          diffuse.b_ + emissive.b_ + ambient.b_, diffuse.a_);
    glColor4f(color.r_, color.g_, color.b_, color.a_);
  }
}

void RageDisplay_DDRMini::SetLighting(bool enabled) {
  if (enabled) glEnable(GL_LIGHTING);
  else glDisable(GL_LIGHTING);
}

void RageDisplay_DDRMini::SetLightOff(int index) {
  glDisable(GL_LIGHT0 + index);
}

void RageDisplay_DDRMini::SetLightDirectional(
    int index, const RageColor& ambient, const RageColor& diffuse,
    const RageColor& specular, const RageVector3& direction) {
  // OpenGL transforms a light position when it is set. The engine supplies
  // this direction in world space, so actor transforms must not rotate it.
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
  glEnable(GL_LIGHT0 + index);
  glLightfv(GL_LIGHT0 + index, GL_AMBIENT, &ambient.r_);
  glLightfv(GL_LIGHT0 + index, GL_DIFFUSE, &diffuse.r_);
  glLightfv(GL_LIGHT0 + index, GL_SPECULAR, &specular.r_);
  const GLfloat position[] = {
      direction.x_, direction.y_, direction.z_, 0.0f};
  glLightfv(GL_LIGHT0 + index, GL_POSITION, position);
  glPopMatrix();
}

void RageDisplay_DDRMini::SetSphereEnvironmentMapping(TextureUnit unit,
                                                       bool enabled) {
  // This GLES renderer exposes one texture unit. GLES 1.1 omits desktop
  // OpenGL's GL_SPHERE_MAP texgen, so compiled geometry supplies equivalent
  // texture coordinates from the CPU while this state is enabled.
  if (unit == TextureUnit_1) sphere_environment_mapping = enabled;
}
RageSurface* RageDisplay_DDRMini::CreateScreenshot(){RageSurface* s=CreateSurface(params_.width_,params_.height_,24,0xFF0000,0x00FF00,0x0000FF,0);glReadPixels(0,0,s->w_,s->h_,GL_RGB,GL_UNSIGNED_BYTE,s->pixels_);return s;}
