#include <initializer_list>
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace taskui {
inline bool phase_ok(const char *s) {
  for (auto v : {"waiting", "running", "done", "failed", "stopped"})
    if (!strcmp(s, v))
      return true;
  return false;
}
inline bool action_ok(const char *s) {
  for (auto v : {"starting", "video",  "look", "think", "tap",    "swipe", "path",    "circle",
                 "type",     "key",    "home", "back",  "wait",   "done",  "error",   "stopped",
                 "plan",     "replan", "move", "click", "scroll", "press", "release", "homescreen"})
    if (!strcmp(s, v))
      return true;
  return false;
}
struct State {
  char phase[12] = "idle", action[16] = "", code[28] = "";
  char lines[4][40]{};
  uint32_t step = 0, duration = 0, elapsed = 0, updated = 0;
  bool terminal() const {
    return !strcmp(phase, "failed") || !strcmp(phase, "done") || !strcmp(phase, "stopped");
  }
  void log(const char *action_name, uint32_t ms) {
    for (int i = 3; i > 0; --i)
      memcpy(lines[i], lines[i - 1], sizeof lines[i]);
    snprintf(lines[0], sizeof lines[0], "#%lu %-10s %lums", (unsigned long)step, action_name,
             (unsigned long)ms);
  }
  bool apply(const char *p, const char *a, const char *c, uint32_t n, uint32_t ms, uint32_t total,
             uint32_t now, bool reset) {
    if (!phase_ok(p) || !action_ok(a) || n > 10000 || ms > 3600000 || total > 86400000 ||
        strlen(c) >= sizeof code)
      return false;
    if (reset)
      *this = State{};
    const bool changed = strcmp(phase, p) || strcmp(action, a) || step != n || duration != ms;
    snprintf(phase, sizeof phase, "%s", p);
    snprintf(action, sizeof action, "%s", a);
    snprintf(code, sizeof code, "%s", c);
    step = n;
    duration = ms;
    elapsed = total;
    updated = now;
    if (changed)
      log(a, ms);
    return true;
  }
  bool stale(uint32_t now) const {
    return strcmp(phase, "idle") && !terminal() && uint32_t(now - updated) > 15000;
  }
  uint32_t elapsed_now(uint32_t now) const { return elapsed + (terminal() ? 0 : now - updated); }
};
} // namespace taskui
