#pragma once
#include <cstdint>
namespace video {
struct StreamCursor {
  enum class Step { idle, wait_keyframe, config, frames };
  uint32_t generation = UINT32_MAX;
  int next = 0;
  Step prepare(bool enabled, bool subscribed, uint32_t gen, bool replayable) {
    if (!subscribed) { invalidate(); return Step::idle; }
    if (!enabled) return Step::idle; // pause is not a new decoder
    if (gen == generation) return Step::frames;
    if (!replayable) return Step::wait_keyframe;
    generation = gen;
    next = 0;
    return Step::config;
  }
  void replayed(uint32_t gen, int count) { generation = gen; next = count; }
  void invalidate() { generation = UINT32_MAX; next = 0; }
};
}
