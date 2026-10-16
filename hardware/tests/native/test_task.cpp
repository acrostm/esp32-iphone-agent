#include "task_model.hpp"
#include <cassert>
int main() {
  taskui::State s;
  assert(!s.stale(100000));
  assert(!s.apply("oops", "tap", "", 0, 1, 0, 0, false));
  assert(s.apply("waiting", "starting", "first_frame", 0, 0, 0, 100, true));
  assert(s.apply("running", "tap", "", 2, 200, 500, 600, false));
  assert(s.elapsed_now(700) == 600);
  assert(!s.stale(15600));
  assert(s.stale(15601));
  assert(s.apply("running", "tap", "", 2, 200, 500, 15602, false));
  assert(!s.stale(15603));
  assert(s.apply("failed", "error", "no_video", 2, 0, 800, 900, false));
  assert(s.terminal());
  assert(!s.stale(100000));
  assert(s.elapsed_now(5000) == 800);
  assert(!s.apply("running", "tap", "", 10001, 0, 0, 0, false));
  for (int i = 0; i < 8; i++)
    s.log("move", i);
  assert(s.lines[3][0]);
  assert(s.apply("waiting", "starting", "", 0, 0, 0, 0, true));
  assert(s.lines[1][0] == 0);
}
