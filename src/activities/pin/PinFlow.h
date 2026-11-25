#pragma once

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace pin_flow {
// Apps > PIN lock. No PIN yet: choose one (twice). PIN set: choose "Change PIN" or "Turn off", each after entering
// the current PIN. Runs on top of `host`.
void open(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput);
}  // namespace pin_flow
