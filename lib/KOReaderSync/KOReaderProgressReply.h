#pragma once

#include <ArduinoJson.h>

#include <string>

// What the JSON body of a 2xx reply to GET /syncs/progress/<document> says. Pure, so the host tests
// can feed it server replies; the client keeps the transport, the JSON parse and its logging.
//
// kosync convention: no stored progress is signalled by a 2xx carrying an empty payload, not by
// 404, and what "empty" serializes to varies by implementation -- "{}", "[]", "\"\"", a bare null,
// or {"position_kinds_available":[...]} from a server withholding a position kind we did not ask
// for. A position is an XPath in "progress" or a percentage on its own: Calibre-Web NextGen serves
// what its web reader and a Kobo report as "progress": null plus a percentage, and ProgressMapper
// places a record without an XPath by its percentage. Anything holding neither is "nothing stored",
// so a zeroed-out position is never applied as real progress.
//
// Strings are read through `| ""`: as<std::string>() turns a missing field into "null", which a
// percentage-only record would pass to the XPath mapper as its locator.
// From witchhunt-reader 189f2e3c (crosspoint-reader PR #3219, Micah Halter).
namespace kosync {

// Returns false when the document holds no record; otherwise fills the common fields of `out`
// (document, progress, percentage, device, deviceId, timestamp).
template <class Progress>
bool readProgressRecord(const JsonDocument& doc, const std::string& documentHash, Progress& out) {
  if (!doc.is<JsonObjectConst>()) return false;
  const char* const progress = doc["progress"] | "";
  const bool hasPercentage = doc["percentage"].is<float>();
  if (progress[0] == '\0' && !hasPercentage) return false;

  out.document = documentHash;
  out.progress = progress;
  out.percentage = hasPercentage ? doc["percentage"].as<float>() : 0.0f;
  out.device = doc["device"] | "";
  out.deviceId = doc["device_id"] | "";
  out.timestamp = doc["timestamp"].as<int64_t>();
  return true;
}

}  // namespace kosync
