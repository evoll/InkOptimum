#include "ActivityManager.h"
#include "util/NightScheduleRuntime.h"

#include <BlePageTurner.h>

#include <HalDisplay.h>
#include <HalPowerManager.h>

#include <algorithm>

#ifndef SIMULATOR
#include "network/BootHealth.h"
#endif
#include "InkMODState.h"
#include "InkMODSettings.h"
#include "GlobalActions.h"
#include "OpdsServerStore.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "home/AlertActivity.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/HomeActivity.h"
#include "home/RecentBooksActivity.h"
#include "home/RecentBooksGridActivity.h"
#include "network/InkMODWebServerActivity.h"
#include "network/NearbyStatsSyncActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/FullScreenMessageActivity.h"
#include "util/FrontlightPanelActivity.h"

void ActivityManager::begin() {
  xTaskCreate(&renderTaskTrampoline, "ActivityManagerRender",
              16384,  // Stack size — increased from 8192; createSectionFile() puts ChapterHtmlSlimParser (~700 bytes)
                      // on stack during silentIndexNextChapterIfNeeded
              this,   // Parameters
              1,      // Priority
              &renderTaskHandle  // Task handle
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    RenderLock lock;
    if (currentActivity) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      display.setInverted(NightScheduleRuntime::inverted());
      // Boot/sleep/recovery screens are not proof of a healthy reader UI.
      const bool healthScreen =
          currentActivity->name != "Boot" && currentActivity->name != "Sleep" && currentActivity->name != "SdFirmwareUpdate";
      renderPassActive.store(true, std::memory_order_release);
      const uint32_t renderingVisit = screenVisit;
      currentActivity->render(std::move(lock));
      paintedVisit.store(renderingVisit, std::memory_order_release);
      renderPassActive.store(false, std::memory_order_release);
#ifndef SIMULATOR
      if (healthScreen) boot_health::rendered();
#else
      (void)healthScreen;
#endif
    }
    // Notify any task blocked in requestUpdateAndWait() that the render is done.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&renderStateMux);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&renderStateMux);
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

bool ActivityManager::executeConfiguredHomeShortcut(const uint8_t rawAction) {
  const auto action = static_cast<InkMODSettings::SHORT_PWRBTN>(rawAction);
  if (action == InkMODSettings::SHORT_PWRBTN::IGNORE) return false;

  // enterDeepSleep() calls ActivityManager::goToSleep(), which recursively
  // enters ActivityManager::loop() and replaces its current Activity.
  // Never execute it from this very loop's Home-key event handler.
  if (action == InkMODSettings::SHORT_PWRBTN::SLEEP ||
      action == InkMODSettings::SHORT_PWRBTN::QUICK_RESUME_SLEEP) {
    requestDeferredHomeSleep(action == InkMODSettings::SHORT_PWRBTN::QUICK_RESUME_SLEEP);
    return true;
  }

  // Prefer the currently visible activity so reader-only actions (bookmark,
  // stats, clipping, page turn, etc.) can reuse the exact same code path as
  // the configurable Power shortcuts. Global actions are the safe fallback.
  if (currentActivity && currentActivity->handleShortcutAction(rawAction)) return true;
  return handleGlobalShortcutAction(action);
}

bool ActivityManager::performHomeNavigation() {
  if (!currentActivity || currentActivity->name == "Home") return false;
  if (currentActivity->handleHomeGesture()) return true;
  goHome();
  return true;
}

void ActivityManager::loop() {
  if (currentActivity) {
    mappedInput.setPowerAsConfirmInReaderMode(currentActivity->allowPowerAsConfirmInReaderMode());

    // X4 Pro global touch gestures, kept outside individual activities exactly
    // so inkMOD's existing activity logic remains untouched.  Do not inspect
    // global gestures on the first input frame after an activity transition:
    // a release that occurred during a long synchronous book/cache load is
    // first observed only after the new activity is already active.
    const bool quarantineGlobalTouch = mappedInput.hasTouch() && globalTouchGestureQuarantineFrames > 0;
    if (quarantineGlobalTouch) {
      --globalTouchGestureQuarantineFrames;
    } else if (mappedInput.hasHomeKey()) {
      const uint32_t now = millis();

      // The SDK emits the long-press event while the key is still held and
      // suppresses the later short-tap event, so this cannot accidentally
      // trigger Home on release. A hold also cancels any pending first tap.
      if (mappedInput.wasHomeKeyHold()) {
        pendingHomeSingleTap = false;
        if (executeConfiguredHomeShortcut(SETTINGS.homeLongPressAction)) return;
      }

      // If the first tap was not followed by a second tap in time, preserve
      // the original one-tap Home behaviour. No delay is introduced at all
      // when the user leaves the double-tap shortcut set to Ignore.
      if (pendingHomeSingleTap && now - pendingHomeSingleTapMs > HOME_DOUBLE_TAP_WINDOW_MS) {
        pendingHomeSingleTap = false;
        if (performHomeNavigation()) return;
      }

      if (mappedInput.wasHomeGesture()) {
        if (SETTINGS.homeDoublePressAction == InkMODSettings::SHORT_PWRBTN::IGNORE) {
          if (performHomeNavigation()) return;
        } else if (pendingHomeSingleTap && now - pendingHomeSingleTapMs <= HOME_DOUBLE_TAP_WINDOW_MS) {
          pendingHomeSingleTap = false;
          if (executeConfiguredHomeShortcut(SETTINGS.homeDoublePressAction)) return;
        } else {
          pendingHomeSingleTap = true;
          pendingHomeSingleTapMs = now;
          return;
        }
      }
    } else if (currentActivity->name != "Home" && mappedInput.wasHomeGesture()) {
      // Boards without a capacitive Home key retain the original bottom-edge
      // Home gesture with no double-tap state machine.
      if (currentActivity->handleHomeGesture()) return;
      goHome();
      return;
    }

    bool statusBarTap = false;
    if (mappedInput.hasTouch() &&
        (currentActivity->name == "Home" || currentActivity->name == "FileBrowser" ||
         currentActivity->name == "Settings" || currentActivity->name == "NetworkModeSelection")) {
      int tx = 0, ty = 0;
      statusBarTap = mappedInput.wasScreenTapped(tx, ty) && ty < 44;
    }
    if (!quarantineGlobalTouch && currentActivity->name != "FrontlightPanel" &&
        (statusBarTap || mappedInput.wasLightPanelGesture())) {
      pushActivity(std::make_unique<FrontlightPanelActivity>(renderer, mappedInput));
      return;
    }

    // A requested transition (especially sleep) must not run one more activity tick: it could turn the
    // page or start background work after the user stopped reading. (Paperio-Reader 27ef95f.)
    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    if (pendingAction == PendingAction::None) currentActivity->loop();
  } else {
    mappedInput.setPowerAsConfirmInReaderMode(false);
  }

  while (pendingAction != PendingAction::None) {
    // The page turner's radio gives its heap back before the next screen allocates. Its teardown
    // can span loop passes and runs outside the render lock.
    if (pendingAction != PendingAction::ReplaceForSleep &&
        !bleturner::beforeScreenChange(0, pendingActivity && pendingActivity->isReaderActivity())) {
      return;
    }
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ++screenVisit;
      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        lock.unlock();  // goHome may acquire its own lock
        goHome();
        continue;  // Will launch goHome immediately

      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        armGlobalTouchGestureQuarantine();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        // Handle result if necessary
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");

          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          handler(pendingResult);
        }

        // Queue an update to ensure the popped activity gets re-rendered.
        // Do not block here: result handlers may transiently take RenderLock while
        // reconciling state, and a synchronous wait at this point can trip the
        // deadlock guard even though the queued repaint is sufficient.
        if (pendingAction == PendingAction::None) {
          lock.unlock();
          requestUpdate();
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity) {
      // Current activity has requested a new activity to be launched
      RenderLock lock;

      if (pendingAction == PendingAction::Replace || pendingAction == PendingAction::ReplaceForSleep) {
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        // Give the outgoing activity a chance to free heap-heavy state it can
        // safely regenerate on return (e.g. the reader's decoded Section) -
        // unlike Replace, a pushed activity is kept fully alive on the stack
        // for as long as whatever gets pushed on top of it runs, so its RAM
        // stays committed the whole time unless it releases some itself here.
        if (currentActivity) {
          currentActivity->releaseHeavyResourcesForBackgroundActivity();
        }
        // Move current activity to stack
        stackActivities.push_back(std::move(currentActivity));
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      ++screenVisit;
      pendingAction = PendingAction::None;
      currentActivity = std::move(pendingActivity);

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();
      armGlobalTouchGestureQuarantine();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (APP_STATE.hasPendingAlert.load(std::memory_order_acquire) && pendingAction == PendingAction::None) {
    APP_STATE.hasPendingAlert.store(false, std::memory_order_relaxed);
    pushActivity(std::make_unique<AlertActivity>(renderer, mappedInput));
  }

  if (requestedUpdate) {
    requestedUpdate = false;
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  backgroundTransitionPending = false;
  if (currentActivity) {
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately
    currentActivity = std::move(newActivity);
    currentActivity->onEnter();
    armGlobalTouchGestureQuarantine();
  }
}

void ActivityManager::goToFileTransfer(std::string returnBookPath) {
  replaceActivity(std::make_unique<InkMODWebServerActivity>(renderer, mappedInput, std::move(returnBookPath)));
}

void ActivityManager::goToCalibreWireless(std::string returnBookPath) {
  replaceActivity(std::make_unique<InkMODWebServerActivity>(renderer, mappedInput, NetworkMode::CONNECT_CALIBRE,
                                                                std::move(returnBookPath)));
}

void ActivityManager::goToJoinNetworkFileTransfer(std::string returnBookPath) {
  replaceActivity(std::make_unique<InkMODWebServerActivity>(renderer, mappedInput, NetworkMode::JOIN_NETWORK,
                                                                std::move(returnBookPath)));
}

void ActivityManager::goToHotspotFileTransfer(std::string returnBookPath) {
  replaceActivity(std::make_unique<InkMODWebServerActivity>(renderer, mappedInput, NetworkMode::CREATE_HOTSPOT,
                                                                std::move(returnBookPath)));
}

void ActivityManager::goToNearbyStatsSync() {
  replaceActivity(std::make_unique<NearbyStatsSyncActivity>(renderer, mappedInput));
}

void ActivityManager::goToSettings() { replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput)); }

void ActivityManager::goToFileBrowser(std::string path) {
  replaceActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput, std::move(path)));
}

void ActivityManager::goToRecentBooks() {
  if (SETTINGS.recentBooksView == InkMODSettings::RECENT_BOOKS_GRID) {
    replaceActivity(std::make_unique<RecentBooksGridActivity>(renderer, mappedInput));
  } else {
    replaceActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput));
  }
}

void ActivityManager::goToBrowser() {
  const auto& servers = OPDS_STORE.getServers();
  // Skip the server picker when there's only one server configured
  if (servers.size() == 1) {
    replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, servers[0]));
  } else {
    replaceActivity(std::make_unique<OpdsServerListActivity>(renderer, mappedInput, true));
  }
}

void ActivityManager::goToReader(std::string path, const bool suppressBackRelease) {
  replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, std::move(path), suppressBackRelease));
}

void ActivityManager::goToSleep(bool fromTimeout) {
  const bool canSnapshotOverlay = currentActivity && currentActivity->canSnapshotForSleepOverlay();
  replaceActivity(
      std::make_unique<SleepActivity>(renderer, mappedInput, canSnapshotOverlay, getCurrentBookPath(), fromTimeout));
  if (pendingAction == PendingAction::Replace) pendingAction = PendingAction::ReplaceForSleep;
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot() { replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goHome(HomeMenuItem initialMenuItem) {
  if (initialMenuItem == HomeMenuItem::NONE && currentActivity) {
    const auto& activityName = currentActivity->name;
    if (activityName == "FileBrowser") {
      initialMenuItem = HomeMenuItem::FILE_BROWSER;
    } else if (activityName == "RecentBooks") {
      initialMenuItem = HomeMenuItem::RECENTS;
    } else if (activityName == "OpdsBookBrowser") {
      initialMenuItem = HomeMenuItem::OPDS_BROWSER;
    } else if (activityName == "InkMODWebServer") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "NearbyStatsSync") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "Settings") {
      initialMenuItem = HomeMenuItem::SETTINGS_MENU;
    }
  }
  replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInput, initialMenuItem));
}
void ActivityManager::goToCrashReport() { replaceActivity(std::make_unique<CrashActivity>(renderer, mappedInput)); }

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
  // pendingAction now covers the render-task guard; no need for both flags.
  backgroundTransitionPending = false;
}

void ActivityManager::popActivity() {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
  backgroundTransitionPending = false;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::isReaderActivity() const {
  if (currentActivity && currentActivity->isReaderActivity()) {
    return true;
  }

  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity && activity->isReaderActivity(); });
}

bool ActivityManager::isForegroundReader() const {
  return pendingAction == PendingAction::None && currentActivity && currentActivity->isReaderActivity();
}

bool ActivityManager::isRemoteReaderContext() const {
  return pendingAction == PendingAction::None && currentActivity && currentActivity->acceptsRemoteInput();
}

bool ActivityManager::remoteClick(const RemoteButton button) {
  if (!isRemoteReaderContext() || RenderLock::peek() || requestedUpdate) return false;
  return mappedInput.injectRemoteClick(button);
}

bool ActivityManager::isForegroundHomeShown() const {
  return pendingAction == PendingAction::None && currentActivity && currentActivity->name == "Home" &&
         paintedVisit.load(std::memory_order_acquire) == screenVisit;
}

bool ActivityManager::remoteShortcut(const bool readerMenu) {
  if (!isRemoteReaderContext() || mappedInput.hasRemoteClick()) return false;
  if (RenderLock::peek() || currentActivity->remoteInputBlocked()) return false;
  return currentActivity->remoteShortcut(readerMenu);
}

bool ActivityManager::remoteTurn(const bool forward, const bool chapter) {
  if (!isRemoteReaderContext() || mappedInput.hasRemoteClick()) return false;
  // A menu or panel over the page owns the keys: the turn goes to it as a click, as a device key would.
  if (currentActivity->remoteInputBlocked()) {
    return !chapter && remoteClick(forward ? RemoteButton::Next : RemoteButton::Previous);
  }
  if (RenderLock::peek() || requestedUpdate) return false;
  return currentActivity->remoteTurn(forward, chapter);
}

bool ActivityManager::isCurrentReaderActivity() const {
  return currentActivity && currentActivity->isReaderActivity();
}

void ActivityManager::releaseCurrentActivityHeavyResources() {
  // Unlike the automatic release pushActivity() does for you, this runs
  // immediately - for callers that need to free the current activity's RAM
  // (e.g. the reader's decoded Section) *before* doing their own heavy
  // allocation, not just once their activity actually gets pushed. Calling
  // pushActivity() alone is too late for that: it only queues the push: this
  // activity keeps holding its memory until the ActivityManager loop
  // processes that queued action later, by which point the caller's own
  // heavy work has often already run.
  //
  // Takes the RenderLock itself: this is called from contexts (e.g. the main
  // loop, before pushActivity()) that don't already hold one, unlike
  // pushActivity()'s own internal call to the same override, which runs
  // inside a lock the caller there already holds.
  if (currentActivity) {
    RenderLock lock;
    currentActivity->releaseHeavyResourcesForBackgroundActivity();
    backgroundTransitionPending = true;
  }
}

bool ActivityManager::canSnapshotForSleepOverlay() const {
  return currentActivity && currentActivity->canSnapshotForSleepOverlay();
}

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

std::string ActivityManager::getCurrentBookPath() const {
  if (currentActivity) {
    const std::string path = currentActivity->getCurrentBookPath();
    if (!path.empty()) {
      return path;
    }
  }

  for (auto it = stackActivities.rbegin(); it != stackActivities.rend(); ++it) {
    if (*it) {
      const std::string path = (*it)->getCurrentBookPath();
      if (!path.empty()) {
        return path;
      }
    }
  }

  return {};
}

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    return currentActivity->getScreenshotInfo();
  }
  return {};
}

void ActivityManager::requestUpdate(bool immediate) {
  if (immediate) {
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  } else {
    // Deferring the update until current loop is finished
    // This is to avoid multiple updates being requested in the same loop
    requestedUpdate = true;
  }
}
RequestUpdateResult ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return RequestUpdateResult::Rejected;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&renderStateMux);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&renderStateMux);

  if (isRenderTask) {
    LOG_ERR("ACT", "requestUpdateAndWait() called from render task; rejecting sync update");
    return RequestUpdateResult::Rejected;
  }

  if (alreadyWaiting) {
    LOG_ERR("ACT", "requestUpdateAndWait() called while another task is waiting; rejecting sync update");
    return RequestUpdateResult::Rejected;
  }

  // Cannot call while holding RenderLock or it will cause a deadlock
  if (holdingRenderLock) {
    LOG_ERR("ACT", "requestUpdateAndWait() called while holding RenderLock; rejecting sync update");
    return RequestUpdateResult::Rejected;
  }

  xTaskNotify(renderTaskHandle, 1, eIncrement);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  return RequestUpdateResult::Rendered;
}

// RenderLock

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::RenderLock(const Mode mode) {
  isLocked = xSemaphoreTake(activityManager.renderingMutex, mode == Mode::Try ? 0 : portMAX_DELAY) == pdTRUE;
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

/**
 * Checks if renderingMutex is held by any task, including the calling task.
 *
 * @return true if renderingMutex has an owner (any task), false otherwise.
 *
 * @note Must not be called from ISR context — xSemaphoreGetMutexHolder is not ISR-safe.
 */
bool RenderLock::peek() { return xSemaphoreGetMutexHolder(activityManager.renderingMutex) != nullptr; }
