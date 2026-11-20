#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Per-book reader layout overrides.
//
// A book may carry its own font size and line spacing. The reader applies them
// over the live global settings when the book opens and puts the global values
// back when it closes, so every other screen keeps seeing the user's global
// choices. While overrides are applied, settings written to flash always carry
// the *global* values (see InkMODSettings::saveToFile), so an override can never
// leak into the saved global configuration.
//
// This header is platform-free: the core logic is unit tested natively. File
// persistence lives in BookOverrides.cpp.
namespace book_overrides {

// A field holding kInherit is not overridden: the book follows the global value for it.
constexpr uint8_t kInherit = 0xFF;

struct Values {
  uint8_t fontSize = 0;
  uint8_t lineHeightPercent = 100;
  // Added in file version 2. Default: follow the global settings.
  uint8_t screenMargin = kInherit;
  uint8_t paragraphAlignment = kInherit;
  uint8_t extraParagraphSpacing = kInherit;
  uint8_t hyphenationEnabled = kInherit;
  // Added in file version 3. Top / bottom margin values keep the global meaning (0 = as screen margin).
  uint8_t screenMarginTop = kInherit;
  uint8_t screenMarginBottom = kInherit;
  uint8_t bionicReadingEnabled = kInherit;
  uint8_t guideReadingEnabled = kInherit;

  bool operator==(const Values& o) const {
    return fontSize == o.fontSize && lineHeightPercent == o.lineHeightPercent && screenMargin == o.screenMargin &&
           paragraphAlignment == o.paragraphAlignment && extraParagraphSpacing == o.extraParagraphSpacing &&
           hyphenationEnabled == o.hyphenationEnabled && screenMarginTop == o.screenMarginTop &&
           screenMarginBottom == o.screenMarginBottom && bionicReadingEnabled == o.bionicReadingEnabled &&
           guideReadingEnabled == o.guideReadingEnabled;
  }
};

struct Record {
  bool present = false;
  Values values;
};

// Text format: "v3 <fontSize> <lineHeight> <margin> <align> <paraSpacing> <hyphenation> <marginTop> <marginBottom>
// <bionic> <guide>\n" (255 = follow global). Versions 2 (first six numbers) and 1 ("v1 <fontSize> <lineHeightPercent>")
// are still read; fields they do not carry follow the global values.
// Returns false on malformed input.
std::string serialize(const Record& record);
bool parse(const char* text, size_t length, Record& out);

// Applies `record` to `live`, remembering the globals. Re-applying replaces the override
// but keeps the originally remembered globals. A record that is not present restores.
class Session {
 public:
  // Returns true when `live` changed.
  bool apply(Values& live, const Record& record);
  // Puts the remembered globals back into `live`. Returns true when `live` changed.
  bool restore(Values& live);

  bool active() const { return active_; }
  // Globals while active; meaningless otherwise.
  const Values& globals() const { return globals_; }

 private:
  bool active_ = false;
  Values globals_;
};

// File persistence (cache directory of the book). Missing file means "no override".
bool load(const std::string& cacheDir, Record& out);
bool save(const std::string& cacheDir, const Record& record);  // not present -> removes the file

// Process-wide session shared by the reader and InkMODSettings::saveToFile.
Session& session();

}  // namespace book_overrides
