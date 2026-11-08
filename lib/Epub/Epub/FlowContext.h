#pragma once

#include <cstdint>

#include "PageProgression.h"

enum class CssWritingMode : uint8_t;

// The reader's choice for a book, in CrossPointSettings::VERTICAL_TEXT's order (stored by number).
enum class VerticalText : uint8_t { Automatic = 0, On = 1, Off = 2 };

// How a section runs and turns: resolved once per book and section, kept in the section header.
struct FlowContext {
  bool vertical = false;            // set vertical-rl through the turned frame
  bool pageProgressionRtl = false;  // its pages turn right to left

  // The request a book's sections are built with: the setting; Off for a language that is never set vertically.
  // The reader's setting for the sections being built; part of the section cache's key.
  static void setRequest(VerticalText request);
  static VerticalText request();
  // The request the section being built runs under: the setting, Off for a book whose language is never set
  // vertically (only Chinese and Japanese are).
  static VerticalText effectiveRequest();
  static VerticalText requestFor(VerticalText setting, bool verticalAllowed);
  // Whether a section is set vertically, from the writing mode in effect at its <body> and the book's ppd.
  static bool verticalFor(VerticalText request, CssWritingMode rootMode, PageProgression ppd);
  static bool pageProgressionRtlFor(VerticalText request, bool vertical, PageProgression ppd);
  static FlowContext of(VerticalText request, bool vertical, PageProgression ppd);
};
