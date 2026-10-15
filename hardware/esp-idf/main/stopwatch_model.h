#pragma once
#include <stdint.h>
#include <stdio.h>

// Display decisions are independent of Arduino, radio tasks, and graphics.
namespace stopwatch {
enum class Phase { fault, usb, app, mirror, ready };
inline Phase phase(bool healthy, bool usb_ready, bool ble_ready, bool streaming) {
  if (!healthy) return Phase::fault;
  if (!usb_ready) return Phase::usb;
  if (!ble_ready) return Phase::app;
  if (!streaming) return Phase::mirror;
  return Phase::ready;
}
inline const char *headline(Phase p) {
  switch (p) {
    case Phase::fault: return "CHECK DEVICE";
    case Phase::usb: return "CONNECT USB";
    case Phase::app: return "OPEN PHONE APP";
    case Phase::mirror: return "MIRROR SCREEN";
    case Phase::ready: return "BRIDGE READY";
  }
  return "CHECK DEVICE";
}
inline void uptime(uint32_t ms, char *out, size_t size) {
  const uint32_t s = ms / 1000;
  snprintf(out, size, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600),
           (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
}
inline bool due(uint32_t now, uint32_t last, uint32_t interval) {
  return uint32_t(now - last) >= interval;
}
}  // namespace stopwatch
