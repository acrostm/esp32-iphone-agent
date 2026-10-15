#include "stopwatch_model.h"
#include <assert.h>
#include <string.h>

int main() {
  using namespace stopwatch;
  for (int bits = 0; bits < 16; ++bits) {
    const bool healthy = bits & 1, usb = bits & 2, ble = bits & 4, mirror = bits & 8;
    const Phase p = phase(healthy, usb, ble, mirror);
    assert((p == Phase::ready) == (healthy && usb && ble && mirror));
    if (!healthy)
      assert(p == Phase::fault);
    else if (!usb)
      assert(p == Phase::usb);
    else if (!ble)
      assert(p == Phase::app);
    else if (!mirror)
      assert(p == Phase::mirror);
    assert(strlen(headline(p)) > 0);
  }
  char time[20];
  uptime(0, time, sizeof time);
  assert(!strcmp(time, "00:00:00"));
  uptime(3661000, time, sizeof time);
  assert(!strcmp(time, "01:01:01"));
  uptime(UINT32_MAX, time, sizeof time);
  assert(!strcmp(time, "1193:02:47"));
  assert(due(4, UINT32_MAX - 995, 1000));
  assert(!due(3, UINT32_MAX - 995, 1000));
}
