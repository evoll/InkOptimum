#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Reading-session history for the CrossPoint Sync server (crosspoint #4531). The firmware appends one JSON object per
// line to /.crosspoint/reading_sessions.jsonl and uploads the lines the server has not acknowledged to
// POST /api/v1/stats/sessions. This library is the pure part: the line format, the limits the server enforces, the
// upload body and the check of the server's answer.
namespace readingsessions {

inline constexpr size_t MAX_LINE = 512;
inline constexpr int MAX_TITLE_BYTES = 80;
inline constexpr size_t BATCH_SIZE = 20;  // server limit per request
// Server limits; a session outside them would make the server reject its whole batch.
inline constexpr int64_t MIN_START = 946684800;  // 2000-01-01
inline constexpr uint32_t MIN_SECONDS = 10;
inline constexpr uint32_t MAX_SECONDS = 7u * 86400u;
inline constexpr int64_t MAX_FUTURE_SECONDS = 300;

struct Session {
  std::string doc;  // 32 hex characters: the KOReader document id
  int64_t start = 0;  // Unix time of the first counted page
  uint32_t secs = 0;  // active reading seconds
  int tz = 0;         // UTC offset in minutes at the start
  uint16_t from = 0;  // whole-book progress in basis points at the start
  uint16_t to = 0;    // ... at the end
  std::string title;  // at most MAX_TITLE_BYTES (UTF-8 safe); never uploaded
  std::string path;   // SD path of the book when read; never uploaded
};

bool isDocumentId(const std::string& doc);

// One log line without the trailing newline; "" when the session has no valid document id or the line would pass
// MAX_LINE even without its path.
std::string encodeLine(const Session& session);
bool decodeLine(const std::string& line, Session& out);

// Whether the server would accept the session. `now` = 0: the clock is not trusted, the future check is skipped.
bool uploadable(const Session& session, int64_t now);

// The server-side id of a session: "<start>-<mac as 12 hex>", unique per device.
std::string sessionId(int64_t start, const uint8_t mac[6]);

// {"device_id":..., "items":[{session_id, document, started_at, ended_at, seconds, utc_offset_minutes}, ...]}.
// Sessions that are not uploadable or share a start with an earlier one in the batch are left out; `ids` receives the
// session ids that were included, in order.
std::string buildBatch(const std::string& deviceId, const std::vector<Session>& sessions, const uint8_t mac[6],
                       int64_t now, std::vector<std::string>& ids);

// True when the server's answer lists every id in `ids` under "accepted_session_ids" (an older server answers 200
// without storing anything).
bool allAccepted(const std::string& responseBody, const std::vector<std::string>& ids);

}  // namespace readingsessions
