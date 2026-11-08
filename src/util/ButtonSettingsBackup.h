#pragma once

// Backup / restore of the button configuration (power, home, front, side, long-press actions)
// to a small text file on the SD card, so it survives a settings reset or a firmware switch.
namespace button_backup {

constexpr char kPath[] = "/.inkmod/button-settings.txt";

bool save();
bool exists();
// Returns the number of values applied, or -1 when the file is missing/unreadable.
int restore();

}  // namespace button_backup
