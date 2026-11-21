#include <SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct audio_state { double phase; };
static FILE *log_file;
static char mixer_status[96] = "OUTPUT: STARTING";

static void log_message(const char *format, ...)
{
  char message[512];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  SDL_Log("%s", message);
  if (log_file) {
    fprintf(log_file, "[%10u ms] %s\n", SDL_GetTicks(), message);
    fflush(log_file);
  }
}

static void audio_callback(void *userdata, Uint8 *stream, int length)
{
  struct audio_state *state = userdata;
  int16_t *samples = (int16_t *)stream;
  int count = length / (int)sizeof(*samples), i;
  for (i = 0; i < count; ++i) {
    samples[i] = (int16_t)(sin(state->phase) * 2500.0);
    state->phase += (2.0 * 3.14159265358979323846 * 440.0) / 48000.0;
    if (state->phase >= 2.0 * 3.14159265358979323846)
      state->phase -= 2.0 * 3.14159265358979323846;
  }
}

static void update_audio_status(void)
{
  FILE *file = fopen("/tmp/ddrmini-audio-status", "r");
  char route[32];
  int volume;
  if (!file) {
    snprintf(mixer_status, sizeof(mixer_status), "OUTPUT: MONITOR NOT RUNNING");
    return;
  }
  if (fscanf(file, "%31s %d", route, &volume) == 2)
    snprintf(mixer_status, sizeof(mixer_status), "OUTPUT: %s  DAC %d / 63",
             route, volume);
  else
    snprintf(mixer_status, sizeof(mixer_status), "OUTPUT: STATUS UNAVAILABLE");
  fclose(file);
}

/* Seven rows of five pixels; avoids adding a font dependency. */
static const uint8_t *glyph(char c)
{
  static const uint8_t blank[7]={0,0,0,0,0,0,0}, colon[7]={0,4,4,0,4,4,0};
  static const uint8_t dash[7]={0,0,0,31,0,0,0}, dot[7]={0,0,0,0,0,6,6};
  static const uint8_t slash[7]={1,2,2,4,8,8,16};
  static const uint8_t digits[10][7]={
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}};
  static const uint8_t letters[26][7]={
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
  if (c >= 'a' && c <= 'z') c = (char)(c-'a'+'A');
  if (c >= 'A' && c <= 'Z') return letters[c-'A'];
  if (c >= '0' && c <= '9') return digits[c-'0'];
  if (c==':') return colon;
  if (c=='-') return dash;
  if (c=='.') return dot;
  if (c=='/') return slash;
  return blank;
}

static void draw_text(SDL_Renderer *r, int x, int y, int scale,
                      const char *text, SDL_Color color)
{
  SDL_Rect pixel={0,0,scale,scale};
  SDL_SetRenderDrawColor(r,color.r,color.g,color.b,color.a);
  while (*text) {
    const uint8_t *rows=glyph(*text++); int row,col;
    for (row=0;row<7;++row) for (col=0;col<5;++col)
      if (rows[row] & (1u << (4-col))) {
        pixel.x=x+col*scale; pixel.y=y+row*scale; SDL_RenderFillRect(r,&pixel);
      }
    x += 6*scale;
  }
}

int main(void)
{
  const char *log_path="/var/run/media/game/ddrmini-smoke.log";
  SDL_Window *window=NULL; SDL_Renderer *renderer=NULL; SDL_RendererInfo info;
  SDL_AudioDeviceID audio=0; SDL_AudioSpec requested={0};
  struct audio_state audio_state={0.0};
  char video_status[96]="VIDEO: STARTING", audio_status[96]="AUDIO: STARTING";
  char joystick_status[96]="JOYSTICKS: 0", last_input[128]="LAST INPUT: NONE";
  int running=1, joystick_count, i;

  log_file=fopen(log_path,"a");
  if (!log_file) { log_path="/tmp/ddrmini-smoke.log"; log_file=fopen(log_path,"a"); }
  SDL_SetMainReady(); SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);
  if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK|
               SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)!=0) {
    log_message("SDL_Init failed: %s",SDL_GetError()); return 1;
  }
  log_message("DDR Mini smoke test starting; log: %s",log_path);
  log_message("video driver: %s; audio driver: %s",SDL_GetCurrentVideoDriver(),
              SDL_GetCurrentAudioDriver());
  update_audio_status();
  window=SDL_CreateWindow("DDR Mini smoke test",SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED,1280,720,SDL_WINDOW_FULLSCREEN_DESKTOP);
  if (!window) { log_message("SDL_CreateWindow failed: %s",SDL_GetError()); goto done; }
  renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) { log_message("accelerated renderer failed: %s",SDL_GetError());
    renderer=SDL_CreateRenderer(window,-1,0); }
  if (!renderer) { log_message("SDL_CreateRenderer failed: %s",SDL_GetError()); goto done; }
  memset(&info,0,sizeof(info)); SDL_GetRendererInfo(renderer,&info);
  snprintf(video_status,sizeof(video_status),"VIDEO: OK  %s / %s",
      SDL_GetCurrentVideoDriver(),info.name?info.name:"UNKNOWN");
  log_message("renderer: %s",info.name?info.name:"unknown");

  joystick_count=SDL_NumJoysticks();
  snprintf(joystick_status,sizeof(joystick_status),"JOYSTICKS: %d",joystick_count);
  log_message("joysticks detected: %d",joystick_count);
  for (i=0;i<joystick_count;++i) {
    SDL_Joystick *joystick=SDL_JoystickOpen(i);
    log_message("joystick %d: %s%s",i,SDL_JoystickNameForIndex(i),
                joystick?" (opened)":" (open failed)");
  }
  requested.freq=48000; requested.format=AUDIO_S16SYS; requested.channels=2;
  requested.samples=1024; requested.callback=audio_callback; requested.userdata=&audio_state;
  audio=SDL_OpenAudioDevice(NULL,0,&requested,NULL,0);
  if (audio) { snprintf(audio_status,sizeof(audio_status),"AUDIO: OK  48000 HZ STEREO");
    log_message("audio opened; playing a quiet 440 Hz test tone"); SDL_PauseAudioDevice(audio,0);
  } else { snprintf(audio_status,sizeof(audio_status),"AUDIO: FAILED  %.60s",SDL_GetError());
    log_message("audio open failed: %s",SDL_GetError()); }

  while (running) {
    SDL_Event event; Uint32 ticks=SDL_GetTicks(); SDL_Color white={240,240,240,255};
    SDL_Color green={80,255,120,255}, cyan={80,220,255,255};
    SDL_Rect bar={40,650,(int)((ticks/8u)%1200u),24};
    static Uint32 last_audio_update;
    if (ticks - last_audio_update >= 100u) {
      update_audio_status();
      last_audio_update = ticks;
    }
    while (SDL_PollEvent(&event)) switch(event.type) {
      case SDL_QUIT: running=0; break;
      case SDL_KEYDOWN:
        snprintf(last_input,sizeof(last_input),"LAST INPUT: KEY %d",event.key.keysym.scancode);
        log_message("key down: scancode=%d",event.key.keysym.scancode);
        if (event.key.keysym.scancode==SDL_SCANCODE_ESCAPE) running=0;
        break;
      case SDL_JOYBUTTONDOWN:
        snprintf(last_input,sizeof(last_input),"LAST INPUT: JOY %d BUTTON %u",
                 event.jbutton.which,event.jbutton.button);
        log_message("joystick %d button %u down",event.jbutton.which,event.jbutton.button); break;
      case SDL_JOYAXISMOTION:
        if (event.jaxis.value < -8000 || event.jaxis.value > 8000) {
          snprintf(last_input,sizeof(last_input),"LAST INPUT: JOY %d AXIS %u VALUE %d",
                   event.jaxis.which,event.jaxis.axis,event.jaxis.value);
          log_message("joystick %d axis %u value %d",event.jaxis.which,
                      event.jaxis.axis,event.jaxis.value); }
        break;
      case SDL_CONTROLLERBUTTONDOWN:
        snprintf(last_input,sizeof(last_input),"LAST INPUT: CONTROLLER %d BUTTON %d",
                 event.cbutton.which,event.cbutton.button);
        log_message("controller %d button %d down",event.cbutton.which,event.cbutton.button); break;
      default: break;
    }
    SDL_SetRenderDrawColor(renderer,10,18,35,255); SDL_RenderClear(renderer);
    draw_text(renderer,40,35,5,"DDR MINI SMOKE TEST",cyan);
    draw_text(renderer,40,130,4,video_status,green);
    draw_text(renderer,40,200,4,audio_status,audio?green:(SDL_Color){255,100,80,255});
    draw_text(renderer,40,270,4,joystick_status,white);
    draw_text(renderer,40,340,3,mixer_status,audio?green:(SDL_Color){255,100,80,255});
    draw_text(renderer,40,400,3,last_input,cyan);
    draw_text(renderer,40,490,3,"LOG: USB/DDRMINI-SMOKE.LOG",white);
    draw_text(renderer,40,550,3,"PRESS BUTTONS TO TEST INPUT",white);
    SDL_SetRenderDrawColor(renderer,(Uint8)(80+(ticks/10u)%175u),80,220,255);
    SDL_RenderFillRect(renderer,&bar); SDL_RenderPresent(renderer);
  }
done:
  if(audio) SDL_CloseAudioDevice(audio);
  if(renderer) SDL_DestroyRenderer(renderer);
  if(window) SDL_DestroyWindow(window);
  SDL_Quit();
  if(log_file) fclose(log_file);
  return 1; /* Firmware treats 0 and 42 as reboot/shutdown requests. */
}
