#pragma once

#include <string>

#include "BookOverrides.h"

// Glue between the platform-free override core and the live global SETTINGS.
namespace book_overrides {

// The live layout fields an override can change, read from / written to SETTINGS.
Values captureLive();
void storeLive(const Values& values);

// Record for "the current reader settings, for this book only" (every field explicit).
Record currentAsRecord();

// Loads the book's override (if any) and applies it over SETTINGS. Call before layout/fonts are used.
// Returns true when live layout settings changed.
bool enterBook(const std::string& cacheDir);

// Puts the global values back. Call when the reader closes.
void exitBook();

// Applies and persists an override for the bound book; `present == false` removes it.
// Returns true when live layout settings changed.
bool setForBook(const Record& record);

// Persists the current live layout as this book's override (used when the user edits
// reader options while the book already has one). No-op when no override is active.
void persistLiveIfActive();

bool bookHasOverride();

}  // namespace book_overrides
