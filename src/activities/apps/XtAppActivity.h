#pragma once

#include <XtApp.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <string>

#include "GfxRenderer.h"
#include "activities/Activity.h"

// Runs one Lua mini app (manifest.json + index.lua in /XTApps/<id>/, official Licorice API dialect) full screen.
// The renderer is forced to portrait for the lifetime of the app: apps are laid out for 480x800.
//
// Threading: input and timers run in loop() (main task), on_draw runs in render() (render task). Both take
// luaMutex_, so the Lua state is never entered from two tasks at once.
class XtAppActivity final : public Activity {
 public:
  struct NetJob;  // worker-task state, public so the task entry point can name it
  XtAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string appDir,
                std::string presetError = {});
  ~XtAppActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return keepAwake_; }

  // Sleep screen: runs the configured lockscreen app once and draws its frame into the renderer buffer (no display
  // call). false when none is configured / it cannot run; the caller then draws its normal sleep screen.
  static bool drawLockscreen(GfxRenderer& renderer);

 private:
  struct DeviceEnv;
  struct GfxCanvas;

  void drawMessage(const char* title, const std::string& text) const;
  void drawOverlays();
  void startApp();  // creates the Env and runs the app in appDir_
  void stopApp();   // cancels network jobs, leave()s and drops the app
  void pumpNetwork();
  void pumpWebsocket();  // ctx.net:websocket: one links2004 client polled from loop()
  void closeWebsocket();
  void failWaitingNet(const char* error);
  void feedKey(const char* key, bool& exitRequested);

  std::string appDir_;
  std::string startError_;
  std::unique_ptr<DeviceEnv> env_;
  std::unique_ptr<GfxCanvas> canvas_;
  std::unique_ptr<xtapp::App> app_;
  std::mutex luaMutex_;
  GfxRenderer::Orientation savedOrientation_ = GfxRenderer::Orientation::Portrait;
  std::deque<xtapp::NetRequest> waitingNet_;
  std::vector<std::shared_ptr<NetJob>> netJobs_;
  struct WsState;
  std::unique_ptr<WsState> ws_;
  bool wifiAsked_ = false;
  bool wifiByUs_ = false;
  uint32_t keepWifiUntil_ = 0;
  std::string toast_;
  bool fullRefresh_ = false;
  bool keepAwake_ = false;
  int fastFrames_ = 0;
  uint32_t lastToastMs_ = 0;
};
