#pragma once
#include <string>

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace shelf_flow {
// "Add to shelf": pick a shelf (or make a new one) for the book, save, show a toast. Runs on top of `host`.
void addBook(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& bookPath);
}  // namespace shelf_flow
