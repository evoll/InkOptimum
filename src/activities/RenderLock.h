#pragma once

class Activity;  // forward declaration

// RAII helper to lock rendering mutex for the duration of a scope.
class RenderLock {
  bool isLocked = false;

 public:
  enum class Mode { Wait, Try };
  explicit RenderLock();
  // Try: never waits; ownsLock() says whether the lock was taken.
  explicit RenderLock(Mode mode);
  explicit RenderLock(Activity&);  // unused for now, but keep for compatibility
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock();
  void unlock();
  bool ownsLock() const { return isLocked; }
  static bool peek();
};
