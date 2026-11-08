#include "PluginJobs.h"

namespace pluginjobs {

uint32_t Queue::enqueue(const std::string& plugin, const std::string& action, const std::string& args,
                        uint32_t nowMs) {
  (void)nowMs;
  if (plugin.empty() || plugin.size() > MAX_NAME || action.empty() || action.size() > MAX_NAME ||
      args.size() > MAX_JSON) {
    return 0;
  }
  Slot* target = nullptr;
  for (auto& s : slots_) {
    if (s.state == State::Unknown) {
      target = &s;
      break;
    }
  }
  if (!target) {  // recycle the oldest finished job
    for (auto& s : slots_) {
      if ((s.state == State::Done || s.state == State::Error) && (!target || s.serial < target->serial)) target = &s;
    }
  }
  if (!target) return 0;
  *target = Slot{};
  target->id = nextId_++;
  if (nextId_ == 0) nextId_ = 1;
  target->serial = ++serial_;
  target->state = State::Pending;
  target->plugin = plugin;
  target->action = action;
  target->args = args.empty() ? "{}" : args;
  return target->id;
}

Claimed Queue::claim(const std::string& plugin, uint32_t nowMs) {
  Slot* best = nullptr;
  for (auto& s : slots_) {
    if (s.plugin != plugin) continue;
    const bool expired = s.state == State::Running && nowMs - s.leaseStart >= LEASE_MS;
    if ((s.state == State::Pending || expired) && (!best || s.serial < best->serial)) best = &s;
  }
  Claimed out;
  if (!best) return out;
  best->state = State::Running;
  best->claim = nextClaim_++;
  if (nextClaim_ == 0) nextClaim_ = 1;
  best->leaseStart = nowMs;
  out.id = best->id;
  out.claim = best->claim;
  out.action = best->action;
  out.args = best->args;
  return out;
}

bool Queue::complete(uint32_t id, uint32_t claim, bool ok, const std::string& result) {
  if (id == 0) return false;
  for (auto& s : slots_) {
    if (s.id != id || s.state != State::Running || s.claim != claim) continue;
    s.state = ok ? State::Done : State::Error;
    s.result = result.size() > MAX_JSON ? std::string("{}") : result;
    return true;
  }
  return false;
}

Status Queue::status(uint32_t id) const {
  Status out;
  if (id == 0) return out;
  for (const auto& s : slots_) {
    if (s.id != id) continue;
    out.state = s.state;
    out.result = s.result;
    return out;
  }
  return out;
}

}  // namespace pluginjobs
