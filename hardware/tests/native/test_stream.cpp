#include "stream_cursor.hpp"
#include <cassert>
int main() {
  video::StreamCursor c;
  using S = video::StreamCursor::Step;
  assert(c.prepare(true, true, 10, true) == S::config);
  c.next = 123;
  for (int run = 0; run < 1000; ++run) {
    assert(c.prepare(false, true, 10, false) == S::idle);
    assert(c.next == 123 && c.generation == 10);
    assert(c.prepare(true, true, 10, false) == S::frames);
    assert(c.next == 123);
  }
  c.replayed(10, 456);
  assert(c.prepare(true, true, 10, false) == S::frames && c.next == 456);
  assert(c.prepare(true, false, 10, false) == S::idle);
  assert(c.prepare(true, true, 10, false) == S::wait_keyframe);
  assert(c.prepare(true, true, 11, true) == S::config && c.next == 0);
  c.next = 5;
  assert(c.prepare(true, true, 12, true) == S::config && c.next == 0);
}
