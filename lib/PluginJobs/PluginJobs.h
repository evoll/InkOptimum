#pragma once

#include <cstdint>
#include <string>

// Fixed-size pool of opaque {plugin, action, args} jobs for the web plugin runner. The firmware never
// interprets them: a page hosting the plugin claims a job, runs the registered handler and posts the result.
// Time is injected (milliseconds) so the pool is testable and independent of Arduino.
namespace pluginjobs {

inline constexpr int SLOTS = 6;
inline constexpr size_t MAX_NAME = 23;
inline constexpr size_t MAX_JSON = 191;
inline constexpr uint32_t LEASE_MS = 10 * 60 * 1000;

enum class State { Unknown, Pending, Running, Done, Error };

struct Claimed {
  uint32_t id = 0;  // 0: nothing to claim
  uint32_t claim = 0;
  std::string action, args;
};

struct Status {
  State state = State::Unknown;
  std::string result;
};

class Queue {
 public:
  // Id of the new job, 0 when no slot is free (finished jobs are recycled oldest first) or the input is invalid.
  uint32_t enqueue(const std::string& plugin, const std::string& action, const std::string& args, uint32_t nowMs);
  Claimed claim(const std::string& plugin, uint32_t nowMs);
  // False when the job is unknown or `claim` is stale.
  bool complete(uint32_t id, uint32_t claim, bool ok, const std::string& result);
  Status status(uint32_t id) const;

 private:
  struct Slot {
    uint32_t id = 0;
    uint32_t claim = 0;
    uint32_t leaseStart = 0;
    uint32_t serial = 0;  // enqueue order, for recycling
    State state = State::Unknown;
    std::string plugin, action, args, result;
  };
  Slot slots_[SLOTS];
  uint32_t nextId_ = 1;
  uint32_t nextClaim_ = 1;
  uint32_t serial_ = 0;
};

}  // namespace pluginjobs
