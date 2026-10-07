// slipstream - viewer/driver for original Slipstream 5000 data (SDL3 front end).
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "platform/viewer_app.hpp"

using namespace slip;

static void usage() {
  std::puts(
      "slipstream [options]\n"
      "  --data DIR          folder containing SLIPSTRM.RES / SLIPCD.RES (default: auto-detect)\n"
      "  --track N           1..10 (default 2)\n"
      "  --models [NAME]     start in the shape viewer (optionally at NAME.SHP)\n"
      "  --sprites [NAME]    start in the sprite viewer\n"
      "  --drive             start driving ship --ship\n"
      "  --ship N            0..9\n"
      "  --ship-scale F      display scale for ship models (default 2)\n"
      "  --res WxH           internal render size (default 960x540)\n"
      "  --screenshot FILE   render one frame to a PPM file and exit (no window)\n"
      "  --cam X,Y,Z,YAW,PITCH   camera (world units, radians)\n"
      "  --sim SEC           with --drive and --screenshot: hold full throttle for SEC seconds first\n"
      "  --bench N           headless: render N frames and report speed\n"
      "  --soundfont FILE    SoundFont for the MIDI music (default: resources/GeneralUser-GS.sf2, bundled in the app)\n"
      "  --music NAME        play this song (INGAME2/3/4/6, INTRO, WIN, LOSE .HMP) instead of a random race song\n"
      "  --laps N            race length for the finish / result music (default 3)\n"
      "  --no-countdown      skip the 5 s start sequence\n"
      "  --weapons SPEC      player loadout, e.g. seeker:9,scrambler:9,booster:2 (default: the original's cheat loadout; 'none' = blaster only)\n"
      "  --no-pickups        no bonus objects      --no-ai-weapons   the AI ships do not shoot      --no-voices   no pilot/announcer lines\n"
      "  --no-music          no music      --no-sfx   no sound effects     --no-audio   no sound at all\n"
      "  --volume V          master volume 0..1 (default 1)   --music-volume V (0.8)   --sfx-volume V (1)\n");
}

static bool writePPM(const std::string& path, const SoftwareRenderer& r) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", r.width(), r.height());
  const uint32_t* p = r.pixels();
  for (int i = 0; i < r.width() * r.height(); ++i) {
    unsigned char rgb[3] = {uint8_t(p[i] >> 16), uint8_t(p[i] >> 8), uint8_t(p[i])};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
  return true;
}

static void copyDebug(const slip::ViewerApp& app) {
  std::string text;
  for (const auto& l : app.hudLines()) text += l + "\n";
  SDL_SetClipboardText(text.c_str());
}

int main(int argc, char** argv) {
  AppOptions opt;
  std::string screenshot;
  int bench = 0;
  double simSeconds = 0;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* name) -> const char* {
      if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", name); std::exit(2); }
      return argv[++i];
    };
    auto optional = [&]() -> std::string { return (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : ""; };
    if (a == "--data") opt.dataDir = next("--data");
    else if (a == "--track") opt.track = std::atoi(next("--track"));
    else if (a == "--models") { opt.mode = AppMode::Model; opt.shape = optional(); }
    else if (a == "--sprites") { opt.mode = AppMode::Sprite; opt.sprite = optional(); }
    else if (a == "--drive") opt.drive = true;
    else if (a == "--ship") opt.ship = std::atoi(next("--ship"));
    else if (a == "--ship-scale") opt.shipScale = float(std::atof(next("--ship-scale")));
    else if (a == "--res") { if (std::sscanf(next("--res"), "%dx%d", &opt.width, &opt.height) != 2) { usage(); return 2; } }
    else if (a == "--screenshot") screenshot = next("--screenshot");
    else if (a == "--sim") simSeconds = std::atof(next("--sim"));
    else if (a == "--bench") bench = std::atoi(next("--bench"));
    else if (a == "--soundfont") opt.audio.soundfont = next("--soundfont");
    else if (a == "--music") { opt.music = next("--music"); if (opt.music.find('.') == std::string::npos) opt.music += ".HMP"; }
    else if (a == "--laps") opt.laps = std::max(1, std::atoi(next("--laps")));
    else if (a == "--no-countdown") opt.countdown = false;
    else if (a == "--weapons") opt.weapons = next("--weapons");
    else if (a == "--no-pickups") opt.pickups = false;
    else if (a == "--no-ai-weapons") opt.aiWeapons = false;
    else if (a == "--no-voices") opt.voices = false;
    else if (a == "--no-music") opt.noMusic = true;
    else if (a == "--no-sfx") opt.audio.sfx = 0.0f;
    else if (a == "--no-audio") opt.audio.enabled = false;
    else if (a == "--volume") opt.audio.master = float(std::atof(next("--volume")));
    else if (a == "--music-volume") opt.audio.music = float(std::atof(next("--music-volume")));
    else if (a == "--sfx-volume") opt.audio.sfx = float(std::atof(next("--sfx-volume")));
    else if (a == "--cam") {
      double v[5];
      if (std::sscanf(next("--cam"), "%lf,%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3], &v[4]) != 5) { usage(); return 2; }
      opt.haveCam = true; opt.cam[0] = v[0]; opt.cam[1] = v[1]; opt.cam[2] = v[2]; opt.camYaw = float(v[3]); opt.camPitch = float(v[4]);
    } else if (a == "-h" || a == "--help") { usage(); return 0; }
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
  }

  if (!screenshot.empty() && simSeconds > 0) opt.countdown = false;
  if (!screenshot.empty() || bench > 0) opt.audio.openDevice = false;  // headless runs stay silent (no device, nothing rendered)
  ViewerApp app;
  std::string err;
  if (!app.init(opt, &err)) {
    std::fprintf(stderr, "slipstream: %s\n", err.c_str());
    if (screenshot.empty() && bench == 0)  // launched from Finder: show the reason in a dialog
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Slipstream", (err + "\n\nStart from Terminal with:  slipstream --data /path/to/your/Slipstream5000/folder").c_str(), nullptr);
    return 1;
  }

  if (bench > 0) {  // headless render timing
    uint64_t t0 = 0;
    (void)t0;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < bench; ++i) { app.update(1.0 / 60.0, InputState{}); app.render(); }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%d frames at %dx%d: %.1f ms/frame (%.0f fps)\n", bench, opt.width, opt.height, 1000.0 * sec / bench, bench / sec);
    return 0;
  }
  if (!screenshot.empty()) {  // headless
    InputState drive;
    drive.throttle = 1;
    drive.fire = std::getenv("SLIP_FIRE") != nullptr;  // test hook: hold the trigger during --sim
    for (double t = 0; t < simSeconds; t += 1.0 / 60.0) app.update(1.0 / 60.0, drive);
    app.update(1.0 / 60.0, simSeconds > 0 ? drive : InputState{});
    app.render();
    if (!writePPM(screenshot, app.renderer())) { std::fprintf(stderr, "cannot write %s\n", screenshot.c_str()); return 1; }
    for (auto& l : app.hudLines()) std::printf("%s\n", l.c_str());
    std::printf("wrote %s\n", screenshot.c_str());
    return 0;
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow("Slipstream 5000 (reimplementation, data viewer)", 1280, 720,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
  SDL_Renderer* ren = SDL_CreateRenderer(window, nullptr);
  if (!ren) { std::fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
  SDL_SetRenderVSync(ren, 1);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, opt.width, opt.height);
  if (!tex) { std::fprintf(stderr, "texture: %s\n", SDL_GetError()); return 1; }
  SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
  SDL_SetWindowRelativeMouseMode(window, true);

  SDL_Gamepad* pad = nullptr;
  int npads = 0;
  if (SDL_JoystickID* ids = SDL_GetGamepads(&npads)) {
    if (npads > 0) pad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
  }

  bool running = true;
  uint64_t last = SDL_GetPerformanceCounter();
  const double freq = double(SDL_GetPerformanceFrequency());
  float mouseDX = 0, mouseDY = 0;
  const double quitAfter = std::getenv("SLIP_QUIT_AFTER") ? std::atof(std::getenv("SLIP_QUIT_AFTER")) : 0.0;  // test hook: quit cleanly after N seconds
  const auto startTime = std::chrono::steady_clock::now();
  while (running) {
    if (quitAfter > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count() > quitAfter) running = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_EVENT_QUIT: running = false; break;
        case SDL_EVENT_MOUSE_MOTION: mouseDX += e.motion.xrel; mouseDY += e.motion.yrel; break;
        case SDL_EVENT_GAMEPAD_ADDED:
          if (!pad) pad = SDL_OpenGamepad(e.gdevice.which);
          break;
        case SDL_EVENT_GAMEPAD_REMOVED:
          if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) { SDL_CloseGamepad(pad); pad = nullptr; }
          break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) app.toggleDrive();
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) app.cycleWeapon();
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) app.nextItem(1);
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) app.nextItem(-1);
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) running = false;
          break;
        case SDL_EVENT_KEY_DOWN:
          if (e.key.repeat) break;
          switch (e.key.key) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_RIGHTBRACKET: app.nextItem(1); break;
            case SDLK_LEFTBRACKET: app.nextItem(-1); break;
            case SDLK_SPACE: app.toggleDrive(); break;
            case SDLK_F1: app.setMode(AppMode::Track); break;
            case SDLK_F2: app.setMode(AppMode::Model); break;
            case SDLK_F3: app.setMode(AppMode::Sprite); break;
            case SDLK_TAB: app.toggleCulling(); break;
            case SDLK_F4: copyDebug(app); break;
            case SDLK_F5: app.toggleVisibility(); break;
            case SDLK_F6: app.togglePainter(); break;
            case SDLK_F7: app.toggleAllScenery(); break;
            case SDLK_F8: app.toggleAssist(); break;
            case SDLK_F9: app.toggleAI(); break;
            case SDLK_M: app.toggleMusic(); break;
            case SDLK_N: app.toggleSfx(); break;
            case SDLK_X: app.cycleWeapon(); break;
            case SDLK_C:
              if (e.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) copyDebug(app);
              break;
            default:
              if (e.key.key >= SDLK_1 && e.key.key <= SDLK_9) app.selectShip(int(e.key.key - SDLK_1));
              if (e.key.key == SDLK_0) app.selectShip(9);
          }
          break;
        default: break;
      }
    }
    uint64_t now = SDL_GetPerformanceCounter();
    double dt = std::min(0.1, double(now - last) / freq);
    last = now;

    const bool* k = SDL_GetKeyboardState(nullptr);
    InputState in;
    in.moveForward = float(k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - float(k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]);
    in.moveRight = float(k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - float(k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]);
    in.moveUp = float(k[SDL_SCANCODE_E]) - float(k[SDL_SCANCODE_Q]);
    in.fast = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    in.lookDX = mouseDX * 0.0025f;
    in.lookDY = mouseDY * 0.0025f;
    mouseDX = mouseDY = 0;
    in.throttle = float(k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]);
    in.brake = float(k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]);
    in.pitch = float(k[SDL_SCANCODE_E]) - float(k[SDL_SCANCODE_Q]);  // nose up / down (drive mode)
    in.steer = float(k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - float(k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]);
    if (pad) {
      auto ax = [&](SDL_GamepadAxis a) { float v = float(SDL_GetGamepadAxis(pad, a)) / 32767.0f; return std::fabs(v) < 0.12f ? 0.0f : v; };
      in.moveRight += ax(SDL_GAMEPAD_AXIS_LEFTX);
      in.moveForward -= ax(SDL_GAMEPAD_AXIS_LEFTY);
      in.lookDX += ax(SDL_GAMEPAD_AXIS_RIGHTX) * 2.2f * float(dt);
      in.lookDY += ax(SDL_GAMEPAD_AXIS_RIGHTY) * 1.6f * float(dt);
      in.steer += ax(SDL_GAMEPAD_AXIS_LEFTX);
      in.throttle = std::max(in.throttle, ax(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
      in.brake = std::max(in.brake, ax(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    }
    in.fire = k[SDL_SCANCODE_F] || (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST));
    in.steer = std::clamp(in.steer, -1.0f, 1.0f);

    app.update(dt, in);
    app.render();

    SDL_UpdateTexture(tex, nullptr, app.renderer().pixels(), opt.width * int(sizeof(uint32_t)));
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, nullptr, nullptr);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    float y = 6;
    for (const auto& line : app.hudLines()) {
      SDL_RenderDebugText(ren, 6, y, line.c_str());
      y += 11;
    }
    SDL_RenderPresent(ren);
  }

  if (pad) SDL_CloseGamepad(pad);
  app.audio().shutdown();  // stop the audio callback and close the device while SDL is still alive (the app object outlives SDL_Quit)
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
