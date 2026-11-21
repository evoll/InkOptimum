#pragma once

#include "../../lib/ReadingSessions/ReadingSessions.h"

#include <cstdint>
#include <string>
#include <vector>

// Reading-session history for the CrossPoint Sync server (crosspoint #4531).
// /.crosspoint/reading_sessions.jsonl is append-only; /.crosspoint/reading_sessions_sync.json keeps how many bytes of it
// the server has acknowledged. The pure line/batch rules live in lib/ReadingSessions.
namespace ReadingSessionLog {

struct Entry {
  std::string document;  // KOReader document id (32 hex)
  std::string title;
  std::string path;
  int64_t startEpoch = 0;
  uint32_t seconds = 0;
  int startBp = 0;  // 0..10000
  int endBp = 0;
};

// Append one finished session. Returns false when it was invalid or the write failed.
bool append(const Entry& entry);

struct Pending {
  std::vector<readingsessions::Session> sessions;  // next batch candidates, oldest first
  uint32_t endOffset = 0;                          // file offset right after the last line that was read
  bool more = false;                               // lines remain beyond this batch
};

// Reads up to readingsessions::BATCH_SIZE uploadable sessions starting at the acknowledged offset. Lines that can never
// be uploaded (corrupt, too short, ...) are skipped and counted as handled.
Pending readPending(int64_t now);

// Records that everything up to `offset` is handled.
bool markUploaded(uint32_t offset);

}  // namespace ReadingSessionLog
