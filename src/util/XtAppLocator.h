#pragma once
#include <string>

// Finds installed Lua mini apps under /XTApps by manifest app_id or folder name (official encrypted packages and
// folders without a runnable entry script are ignored). Returns the app folder, empty when there is none.
namespace XtAppLocator {
std::string findDir(const std::string& idOrFolder);
}  // namespace XtAppLocator
