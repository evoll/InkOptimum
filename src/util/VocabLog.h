#pragma once
// Personal vocabulary: every successful dictionary lookup is appended to /.crosspoint/vocab.jsonl when the
// "Save looked-up words" setting is on (idea: Folio's vocab.jsonl, crosspoint vocabulary history #4294).
// One JSON object per line: {"w": word as selected, "h": matched headword, "t": unix time (0 = clock unset)}.
// The companion app reads it through GET /api/v1/vocab.

namespace VocabLog {
constexpr const char* PATH = "/.crosspoint/vocab.jsonl";
void append(const char* selected, const char* headword);
}  // namespace VocabLog
