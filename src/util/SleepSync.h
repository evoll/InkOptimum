#pragma once

#include <cstdint>
#include <string>

// "Sync On Sleep" (crosspoint #4531): after the sleep screen is on the panel, the reading position of the book that
// was open (and the reading statistics / session log, when those toggles are on) goes to the sync server.
// The position is captured by the reader when it closes (saveSnapshot) because the book is not in memory any more by
// the time Wi-Fi comes up.
namespace SleepSync {

struct Snapshot {
  std::string document;  // KOReader document id
  std::string xpath;
  float percentage = 0.0f;  // 0..1
  uint16_t spine = 0;
  uint16_t page = 0;
  uint16_t totalPages = 1;
  uint16_t paragraph = 0;  // 0 = unknown
  std::string filename;
  std::string title;
  std::string authors;
};

// Whether the reader should capture a snapshot when it closes (setting on, server configured).
bool enabled();
// Persists the snapshot as "not yet sent".
bool saveSnapshot(const Snapshot& snapshot);

// Setting on, a snapshot waits, battery >= 20 % and the last network has a saved password.
bool wanted(uint16_t batteryPercent);

// Joins the last network if needed (10 s deadline), sends, and turns Wi-Fi off again if it joined. Never throws the
// sleep off: every failure leaves the snapshot for the next sleep.
void run();

}  // namespace SleepSync
