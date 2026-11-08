#include "FlowContext.h"

#include <CjkPunct.h>

#include "css/CssStyle.h"

namespace {
VerticalText gRequest = VerticalText::Automatic;
}

void FlowContext::setRequest(const VerticalText request) { gRequest = request; }
VerticalText FlowContext::request() { return gRequest; }
VerticalText FlowContext::effectiveRequest() { return requestFor(gRequest, CjkPunctuation::bookProfile().grid); }

VerticalText FlowContext::requestFor(const VerticalText setting, const bool verticalAllowed) {
  return verticalAllowed ? setting : VerticalText::Off;
}

bool FlowContext::verticalFor(const VerticalText request, const CssWritingMode rootMode, const PageProgression ppd) {
  if (request == VerticalText::Off) return false;
  if (request == VerticalText::On) return true;
  // Only vertical-rl has a turned frame, so horizontal-tb and vertical-lr stay horizontal; with no writing mode set a
  // right-to-left book is the Japanese vertical convention.
  if (rootMode == CssWritingMode::Unset) return ppd == PageProgression::Rtl;
  return rootMode == CssWritingMode::VerticalRl;
}

bool FlowContext::pageProgressionRtlFor(const VerticalText request, const bool vertical, const PageProgression ppd) {
  if (request == VerticalText::Off) return false;
  // A horizontal colophon in a vertical book keeps the book's direction, and columns advance right to left.
  return vertical || ppd == PageProgression::Rtl;
}

FlowContext FlowContext::of(const VerticalText request, const bool vertical, const PageProgression ppd) {
  return FlowContext{vertical, pageProgressionRtlFor(request, vertical, ppd)};
}
