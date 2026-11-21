#define SDL_MAIN_HANDLED
#include "RageDisplay_DDRMini.h"

#include <SDL.h>
#include <GLES/gl.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

#include "DisplaySpec.h"
#include "RageLog.h"
#include "RageMath.h"
#include "RageSurface.h"
#include "global.h"

namespace {
RageDisplay::RagePixelFormatDesc formats[NUM_RagePixelFormat] = {
  {32,{0xFF000000,0x00FF0000,0x0000FF00,0x000000FF}},
  {32,{0x0000FF00,0x00FF0000,0xFF000000,0x000000FF}},
  {16,{0xF000,0x0F00,0x00F0,0x000F}}, {16,{0xF800,0x07C0,0x003E,0x0001}},
  {16,{0xF800,0x07E0,0x001F,0}}, {24,{0xFF0000,0x00FF00,0x0000FF,0}},
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
  if(context_) SDL_GL_DeleteContext(context_); if(window_) SDL_DestroyWindow(window_); SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
std::string RageDisplay_DDRMini::Init(const VideoModeParams& p, bool) {
  SDL_SetMainReady(); if(SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0) return SDL_GetError();
  bool reload=false; return SetVideoMode(p,reload);
}
std::string RageDisplay_DDRMini::TryVideoMode(const VideoModeParams& p, bool& reload) {
  if(context_) { SDL_GL_DeleteContext(context_); context_=nullptr; }
  if(window_) { SDL_DestroyWindow(window_); window_=nullptr; }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,1); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,16); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
  window_=SDL_CreateWindow(p.window_title_.c_str(),SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
                           p.width_,p.height_,SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN_DESKTOP);
  if(!window_) return SDL_GetError(); context_=SDL_GL_CreateContext(window_); if(!context_) return SDL_GetError();
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
void RageDisplay_DDRMini::SendMatrices(){ RageMatrix p,m; RageMatrixMultiply(&p,GetCentering(),GetProjectionTop()); RageMatrixMultiply(&m,GetViewTop(),GetWorldTop()); glMatrixMode(GL_PROJECTION);glLoadMatrixf((float*)&p);glMatrixMode(GL_MODELVIEW);glLoadMatrixf((float*)&m);glMatrixMode(GL_TEXTURE);glLoadMatrixf((float*)GetTextureTop()); }
void RageDisplay_DDRMini::Draw(const RageSpriteVertex v[],int n,unsigned mode){ SendMatrices();setup_vertices(v,n);glDrawArrays(mode,0,n); }
void RageDisplay_DDRMini::DrawQuadsInternal(const RageSpriteVertex v[],int n){ static std::vector<unsigned short> ix;ix.resize(n/4*6);for(int q=0;q<n/4;++q){unsigned short a=q*4;unsigned short z[]={a,(unsigned short)(a+1),(unsigned short)(a+2),(unsigned short)(a+2),(unsigned short)(a+1),(unsigned short)(a+3)};std::copy(z,z+6,ix.begin()+q*6);}SendMatrices();setup_vertices(v,n);glDrawElements(GL_TRIANGLES,ix.size(),GL_UNSIGNED_SHORT,ix.data()); }
void RageDisplay_DDRMini::DrawQuadStripInternal(const RageSpriteVertex v[],int n){Draw(v,n,GL_TRIANGLE_STRIP);} void RageDisplay_DDRMini::DrawFanInternal(const RageSpriteVertex v[],int n){Draw(v,n,GL_TRIANGLE_FAN);} void RageDisplay_DDRMini::DrawStripInternal(const RageSpriteVertex v[],int n){Draw(v,n,GL_TRIANGLE_STRIP);} void RageDisplay_DDRMini::DrawTrianglesInternal(const RageSpriteVertex v[],int n){Draw(v,n,GL_TRIANGLES);}
void RageDisplay_DDRMini::DrawLineStripInternal(const RageSpriteVertex v[],int n,float w){glLineWidth(w);Draw(v,n,GL_LINE_STRIP);}
void RageDisplay_DDRMini::DrawSymmetricQuadStripInternal(const RageSpriteVertex v[],int n){ DrawStripInternal(v,n); }
void RageDisplay_DDRMini::DrawCompiledGeometryInternal(const RageCompiledGeometry* g,int m){SendMatrices();g->Draw(m);}
RageCompiledGeometry* RageDisplay_DDRMini::CreateCompiledGeometry(){return new GLESGeometry;} void RageDisplay_DDRMini::DeleteCompiledGeometry(RageCompiledGeometry* p){delete p;}
bool RageDisplay_DDRMini::SupportsTextureFormat(RagePixelFormat p,bool){GLenum f,t;int b;return gl_format(p,f,t,b);}
uintptr_t RageDisplay_DDRMini::CreateTexture(RagePixelFormat p,RageSurface* s,bool){GLenum f,t;int b;if(!gl_format(p,f,t,b))return 0;GLuint id;glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);std::vector<unsigned char> tight;const void* data=s->pixels_;if(s->pitch_!=s->w_*b){tight.resize(s->w_*s->h_*b);for(int y=0;y<s->h_;++y)memcpy(tight.data()+y*s->w_*b,s->pixels_+y*s->pitch_,s->w_*b);data=tight.data();}glTexImage2D(GL_TEXTURE_2D,0,f,s->w_,s->h_,0,f,t,data);return id;}
void RageDisplay_DDRMini::UpdateTexture(uintptr_t id,RageSurface* s,int x,int y,int w,int h){GLenum f,t;int b;RagePixelFormat p=FindPixelFormat(s->fmt_.bits_per_pixel_,s->fmt_.rmask_,s->fmt_.gmask_,s->fmt_.bmask_,s->fmt_.amask_,true);if(!gl_format(p,f,t,b))return;glBindTexture(GL_TEXTURE_2D,id);glPixelStorei(GL_UNPACK_ALIGNMENT,1);std::vector<unsigned char> buf(w*h*b);for(int r=0;r<h;++r)memcpy(buf.data()+r*w*b,s->pixels_+r*s->pitch_,w*b);glTexSubImage2D(GL_TEXTURE_2D,0,x,y,w,h,f,t,buf.data());}
void RageDisplay_DDRMini::DeleteTexture(uintptr_t t){GLuint x=t;if(x)glDeleteTextures(1,&x);} void RageDisplay_DDRMini::ClearAllTextures(){glBindTexture(GL_TEXTURE_2D,0);} void RageDisplay_DDRMini::SetTexture(TextureUnit u,uintptr_t t){if(u!=TextureUnit_1)return;if(t){glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,t);}else glDisable(GL_TEXTURE_2D);}
void RageDisplay_DDRMini::SetTextureMode(TextureUnit,TextureMode m){glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,m==TextureMode_Add?GL_ADD:GL_MODULATE);} void RageDisplay_DDRMini::SetTextureWrapping(TextureUnit,bool b){glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,b?GL_REPEAT:GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,b?GL_REPEAT:GL_CLAMP_TO_EDGE);} int RageDisplay_DDRMini::GetMaxTextureSize()const{GLint n;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&n);return n;} void RageDisplay_DDRMini::SetTextureFiltering(TextureUnit,bool b){glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,b?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,b?GL_LINEAR:GL_NEAREST);}
void RageDisplay_DDRMini::SetBlendMode(BlendMode m){glEnable(GL_BLEND);GLenum s=GL_SRC_ALPHA,d=GL_ONE_MINUS_SRC_ALPHA;if(m==BLEND_ADD)d=GL_ONE;else if(m==BLEND_MODULATE){s=GL_ZERO;d=GL_SRC_COLOR;}else if(m==BLEND_COPY_SRC){s=GL_ONE;d=GL_ZERO;}else if(m==BLEND_NO_EFFECT){s=GL_ZERO;d=GL_ONE;}glBlendFunc(s,d);}
bool RageDisplay_DDRMini::IsZWriteEnabled()const{GLboolean b;glGetBooleanv(GL_DEPTH_WRITEMASK,&b);return b;}void RageDisplay_DDRMini::SetZWrite(bool b){glDepthMask(b);}void RageDisplay_DDRMini::SetZBias(float f){if(f){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(0,f);}else glDisable(GL_POLYGON_OFFSET_FILL);}void RageDisplay_DDRMini::SetZTestMode(ZTestMode m){if(m==ZTEST_OFF)glDisable(GL_DEPTH_TEST);else{glEnable(GL_DEPTH_TEST);glDepthFunc(m==ZTEST_WRITE_ON_FAIL?GL_GREATER:GL_LEQUAL);}}void RageDisplay_DDRMini::ClearZBuffer(){glClear(GL_DEPTH_BUFFER_BIT);}void RageDisplay_DDRMini::SetCullMode(CullMode m){if(m==CULL_NONE)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glCullFace(m==CULL_BACK?GL_BACK:GL_FRONT);}}void RageDisplay_DDRMini::SetAlphaTest(bool b){if(b){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.01f);}else glDisable(GL_ALPHA_TEST);}
void RageDisplay_DDRMini::SetMaterial(const RageColor&e,const RageColor&a,const RageColor&d,const RageColor&s,float sh){glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,&e.r_);glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT,&a.r_);glMaterialfv(GL_FRONT_AND_BACK,GL_DIFFUSE,&d.r_);glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,&s.r_);glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,sh);}void RageDisplay_DDRMini::SetLighting(bool b){if(b)glEnable(GL_LIGHTING);else glDisable(GL_LIGHTING);}void RageDisplay_DDRMini::SetLightOff(int i){glDisable(GL_LIGHT0+i);}void RageDisplay_DDRMini::SetLightDirectional(int i,const RageColor&a,const RageColor&d,const RageColor&s,const RageVector3&v){GLfloat p[]={-v.x_,-v.y_,-v.z_,0};glEnable(GL_LIGHT0+i);glLightfv(GL_LIGHT0+i,GL_AMBIENT,&a.r_);glLightfv(GL_LIGHT0+i,GL_DIFFUSE,&d.r_);glLightfv(GL_LIGHT0+i,GL_SPECULAR,&s.r_);glLightfv(GL_LIGHT0+i,GL_POSITION,p);}void RageDisplay_DDRMini::SetSphereEnvironmentMapping(TextureUnit,bool){/* GLES 1.1 has no core sphere-map texgen. */}
RageSurface* RageDisplay_DDRMini::CreateScreenshot(){RageSurface* s=CreateSurface(params_.width_,params_.height_,24,0xFF0000,0x00FF00,0x0000FF,0);glReadPixels(0,0,s->w_,s->h_,GL_RGB,GL_UNSIGNED_BYTE,s->pixels_);return s;}
