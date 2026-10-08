// slipstream - viewer/driver for original Slipstream 5000 data (SDL3 front end).
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

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
      "  --ship-scale F      display scale for ship models (default 1 = original size)\n"
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
      "  --chase             start in the chase camera (V cycles cockpit / close chase / far chase)\n"
      "  --ship-view         start in the close chase view (camera locked to the ship's attitude)\n"
      "  --difficulty N      0..2 (default: the setting of SLIPSTRM.CFG, normally 1): AI speed tables, blaster damage\n"
      "  --no-pickups        no bonus objects      --no-ai-weapons   the AI ships do not shoot      --no-voices   no pilot/announcer lines\n"
      "  --no-music          no music      --no-sfx   no sound effects     --no-audio   no sound at all\n"
      "  --viewer            start in the track viewer instead of the front end (menus); implied by --track / --drive / --models / --sprites / --host / --join\n"
      "  --skip-intro        no logos / intro movie\n"
      "  --host              multiplayer: host a game on the local network (up to 10 players, AI ships fill the empty seats)\n"
      "  --join HOST[:PORT]  multiplayer: join a game (F10 opens the multiplayer menu: host / browse LAN games / join by address)\n"
      "  --name NAME         player name (default: user name)      --port N   TCP port (default 51500)\n"
      "  --players N         with --host: start the race as soon as N humans are in the lobby\n"
      "  --net-run SEC       headless multiplayer test run (autopilot, no window, no sound): runs SEC seconds or until the race is over\n"
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
  double simSeconds = 0, netRun = 0, frontSim = 0;
  bool viewer = false, frontForced = false;
  std::string frontKeys;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* name) -> const char* {
      if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", name); std::exit(2); }
      return argv[++i];
    };
    auto optional = [&]() -> std::string { return (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : ""; };
    if (a == "--data") opt.dataDir = next("--data");
    else if (a == "--track") { viewer = true; opt.track = std::atoi(next("--track")); }
    else if (a == "--models") { viewer = true; opt.mode = AppMode::Model; opt.shape = optional(); }
    else if (a == "--sprites") { viewer = true; opt.mode = AppMode::Sprite; opt.sprite = optional(); }
    else if (a == "--drive") { opt.drive = true; viewer = true; }
    else if (a == "--viewer") viewer = true;
    else if (a == "--front") frontForced = true;
    else if (a == "--skip-intro") opt.skipIntro = true;
    else if (a == "--front-sim") frontSim = std::atof(next("--front-sim"));
    else if (a == "--front-keys") frontKeys = next("--front-keys");
    else if (a == "--host") opt.netRole = "host";
    else if (a == "--join") {
      opt.netRole = "join";
      std::string h = next("--join");
      const size_t c = h.rfind(':');
      if (c != std::string::npos) { opt.netPort = std::atoi(h.c_str() + c + 1); h.resize(c); }
      opt.netHost = h;
    }
    else if (a == "--name") opt.netName = next("--name");
    else if (a == "--port") opt.netPort = std::atoi(next("--port"));
    else if (a == "--players") opt.netPlayers = std::clamp(std::atoi(next("--players")), 1, 10);
    else if (a == "--net-run") { netRun = std::atof(next("--net-run")); opt.netHeadless = true; }
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
    else if (a == "--chase") opt.view = 2;
    else if (a == "--ship-view") opt.view = 1;
    else if (a == "--difficulty") opt.difficulty = std::clamp(std::atoi(next("--difficulty")), 0, 2);
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
  if (opt.netRole.empty() && !viewer && (frontForced || (screenshot.empty() && bench == 0 && netRun <= 0))) opt.front = true;
  if (!screenshot.empty() || bench > 0 || netRun > 0) opt.audio.openDevice = false;  // headless runs stay silent (no device, nothing rendered)
  ViewerApp app;
  std::string err;
  if (!app.init(opt, &err)) {
    std::fprintf(stderr, "slipstream: %s\n", err.c_str());
    if (screenshot.empty() && bench == 0)  // launched from Finder: show the reason in a dialog
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Slipstream", (err + "\n\nStart from Terminal with:  slipstream --data /path/to/your/Slipstream5000/folder").c_str(), nullptr);
    return 1;
  }

  if (netRun > 0) {  // headless multiplayer test: real time, the autopilot flies, results are printed at the end
    setenv("SLIP_AUTOPILOT", "1", 0);
    const auto t0 = std::chrono::steady_clock::now();
    auto prev = t0;
    while (!app.wantsQuit()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      const auto now = std::chrono::steady_clock::now();
      const double dt = std::min(0.1, std::chrono::duration<double>(now - prev).count());
      prev = now;
      InputState hold;
      hold.fire = std::getenv("SLIP_FIRE") != nullptr;  // test hook: hold the trigger
      hold.showList = std::getenv("SLIP_LIST") != nullptr;  // test hook: Tab held
      app.update(dt, hold);
      if (std::chrono::duration<double>(now - t0).count() > netRun) break;
      if (!screenshot.empty() && std::chrono::duration<double>(now - t0).count() > netRun - 0.2) break;
    }
    std::printf("%s\n", app.netSummary().c_str());
    if (!screenshot.empty()) { app.render(); writePPM(screenshot, app.renderer()); }
    return 0;
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
  if (!screenshot.empty() && app.frontActive()) {  // headless front end: --front-sim SEC, --front-keys "dds." (u d l r s=select b=back .=0.5 s)
    for (double t = 0; t < frontSim; t += 1.0 / 60.0) app.update(1.0 / 60.0, InputState{});
    for (char k : frontKeys) {
      if (k == '.') { for (int i = 0; i < 30; ++i) app.update(1.0 / 60.0, InputState{}); continue; }
      app.frontKey(k == 'u' ? PauseMenu::Key::Up : k == 'd' ? PauseMenu::Key::Down : k == 'l' ? PauseMenu::Key::Left : k == 'r' ? PauseMenu::Key::Right : k == 'b' ? PauseMenu::Key::Back : PauseMenu::Key::Select);
      app.update(1.0 / 60.0, InputState{});
    }
    app.render();
    if (!writePPM(screenshot, app.renderer())) { std::fprintf(stderr, "cannot write %s\n", screenshot.c_str()); return 1; }
    std::printf("wrote %s\n", screenshot.c_str());
    return 0;
  }
  if (!screenshot.empty()) {  // headless
    InputState drive;
    drive.throttle = 1;
    drive.fire = std::getenv("SLIP_FIRE") != nullptr;  // test hook: hold the trigger during --sim
    for (double t = 0; t < simSeconds; t += 1.0 / 60.0) app.update(1.0 / 60.0, drive);
    app.update(1.0 / 60.0, simSeconds > 0 ? drive : InputState{});
    if (const char* pk = std::getenv("SLIP_PAUSE")) {  // test hook: open the pause menu, then press the listed keys (u d l r s b)
      app.openPause();
      for (const char* c = pk; *c; ++c) {
        const char k = *c;
        app.menuKey(k == 'u' ? PauseMenu::Key::Up : k == 'd' ? PauseMenu::Key::Down : k == 'l' ? PauseMenu::Key::Left : k == 'r' ? PauseMenu::Key::Right : k == 'b' ? PauseMenu::Key::Back : PauseMenu::Key::Select);
      }
    }
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

  bool running = true, textInput = false;
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
        case SDL_EVENT_MOUSE_MOTION:
          mouseDX += e.motion.xrel; mouseDY += e.motion.yrel;
          if (app.frontActive()) { int ww = 1, wh = 1; SDL_GetWindowSize(window, &ww, &wh); app.frontMouse(e.motion.x / ww, e.motion.y / wh, false); }
          break;
        case SDL_EVENT_GAMEPAD_ADDED:
          if (!pad) pad = SDL_OpenGamepad(e.gdevice.which);
          break;
        case SDL_EVENT_GAMEPAD_REMOVED:
          if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) { SDL_CloseGamepad(pad); pad = nullptr; }
          break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
          if (app.paused()) {  // pause menu
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) app.menuKey(PauseMenu::Key::Up);
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) app.menuKey(PauseMenu::Key::Down);
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) app.menuKey(PauseMenu::Key::Left);
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) app.menuKey(PauseMenu::Key::Right);
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) app.menuKey(PauseMenu::Key::Select);
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST || e.gbutton.button == SDL_GAMEPAD_BUTTON_START) app.menuKey(PauseMenu::Key::Back);
            break;
          }
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START && app.pausable()) { app.openPause(); break; }
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) app.toggleDrive();
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) app.cycleWeapon();
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) app.toggleCamera();
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) app.nextItem(1);
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) app.nextItem(-1);
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) running = false;
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          if (app.frontActive() && e.button.button == SDL_BUTTON_LEFT) { int ww = 1, wh = 1; SDL_GetWindowSize(window, &ww, &wh); app.frontMouse(e.button.x / ww, e.button.y / wh, true); }
          break;
        case SDL_EVENT_TEXT_INPUT:
          if (app.netUiOpen()) app.netText(e.text.text);
          break;
        case SDL_EVENT_KEY_DOWN:
          if (app.frontActive()) {  // menus
            if (e.key.repeat && e.key.key != SDLK_LEFT && e.key.key != SDLK_RIGHT && e.key.key != SDLK_UP && e.key.key != SDLK_DOWN) break;
            switch (e.key.key) {
              case SDLK_UP: case SDLK_W: app.frontKey(PauseMenu::Key::Up); break;
              case SDLK_DOWN: case SDLK_S: app.frontKey(PauseMenu::Key::Down); break;
              case SDLK_LEFT: case SDLK_A: app.frontKey(PauseMenu::Key::Left); break;
              case SDLK_RIGHT: case SDLK_D: app.frontKey(PauseMenu::Key::Right); break;
              case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: app.frontKey(PauseMenu::Key::Select); break;
              case SDLK_ESCAPE: app.frontKey(PauseMenu::Key::Back); break;
              default: break;
            }
            break;
          }
          if (app.netUiOpen()) {  // multiplayer menu
            switch (e.key.key) {
              case SDLK_UP: app.netKey(PauseMenu::Key::Up); break;
              case SDLK_DOWN: app.netKey(PauseMenu::Key::Down); break;
              case SDLK_LEFT: app.netKey(PauseMenu::Key::Left); break;
              case SDLK_RIGHT: app.netKey(PauseMenu::Key::Right); break;
              case SDLK_RETURN: case SDLK_KP_ENTER: if (!e.key.repeat) app.netKey(PauseMenu::Key::Select); break;
              case SDLK_ESCAPE: if (!e.key.repeat) app.netKey(PauseMenu::Key::Back); break;
              case SDLK_BACKSPACE: app.netBackspace(); break;
              case SDLK_W: if (!app.netWantsText()) app.netKey(PauseMenu::Key::Up); break;
              case SDLK_S: if (!app.netWantsText()) app.netKey(PauseMenu::Key::Down); break;
              case SDLK_A: if (!app.netWantsText()) app.netKey(PauseMenu::Key::Left); break;
              case SDLK_D: if (!app.netWantsText()) app.netKey(PauseMenu::Key::Right); break;
              default: break;
            }
            break;
          }
          if (app.paused()) {  // pause menu keys (key repeat allowed for the volume sliders)
            switch (e.key.key) {
              case SDLK_UP: case SDLK_W: app.menuKey(PauseMenu::Key::Up); break;
              case SDLK_DOWN: case SDLK_S: app.menuKey(PauseMenu::Key::Down); break;
              case SDLK_LEFT: case SDLK_A: app.menuKey(PauseMenu::Key::Left); break;
              case SDLK_RIGHT: case SDLK_D: app.menuKey(PauseMenu::Key::Right); break;
              case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: if (!e.key.repeat) app.menuKey(PauseMenu::Key::Select); break;
              case SDLK_ESCAPE: if (!e.key.repeat) app.menuKey(PauseMenu::Key::Back); break;
              default: break;
            }
            break;
          }
          if (e.key.repeat) break;
          switch (e.key.key) {
            case SDLK_ESCAPE: if (app.pausable()) app.openPause(); else running = false; break;
            case SDLK_H: app.toggleHud(); break;
            case SDLK_F10: app.openNetMenu(); break;
            case SDLK_RIGHTBRACKET: app.nextItem(1); break;
            case SDLK_LEFTBRACKET: app.nextItem(-1); break;
            case SDLK_SPACE: app.toggleDrive(); break;
            case SDLK_F1: app.setMode(AppMode::Track); break;
            case SDLK_F2: app.setMode(AppMode::Model); break;
            case SDLK_F3: app.setMode(AppMode::Sprite); break;
            case SDLK_TAB: if (!app.netRacing()) app.toggleCulling(); break;
            case SDLK_F4: copyDebug(app); break;
            case SDLK_F5: app.toggleVisibility(); break;
            case SDLK_F6: app.togglePainter(); break;
            case SDLK_F7: app.toggleAllScenery(); break;
            case SDLK_F8: app.toggleAssist(); break;
            case SDLK_F9: app.toggleAI(); break;
            case SDLK_M: if (e.key.mod & SDL_KMOD_SHIFT) app.toggleMusic(); else app.toggleMap(); break;
            case SDLK_N: app.toggleSfx(); break;
            case SDLK_X: app.cycleWeapon(); break;
            case SDLK_V: app.toggleCamera(); break;
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
    if (app.wantsQuit()) running = false;
    {
      static bool relative = true;
      const bool wantRelative = !app.frontActive();
      if (wantRelative != relative) { SDL_SetWindowRelativeMouseMode(window, wantRelative); relative = wantRelative; }
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
    in.showList = k[SDL_SCANCODE_TAB];
    in.fire = k[SDL_SCANCODE_F] || (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST));
    in.steer = std::clamp(in.steer, -1.0f, 1.0f);

    {
      const bool wantText = app.netWantsText();
      if (wantText != textInput) { if (wantText) SDL_StartTextInput(window); else SDL_StopTextInput(window); textInput = wantText; }
    }
    app.update(dt, in);
    app.render();

    SDL_UpdateTexture(tex, nullptr, app.renderer().pixels(), opt.width * int(sizeof(uint32_t)));
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, nullptr, nullptr);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    float y = 6;
    if (!app.hudActive())
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
