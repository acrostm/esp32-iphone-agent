#include "board.hpp"
#include "ble_link.hpp"
#include "cJSON.h"
#include "netcfg.hpp"
#include "src/airplay/airplay.h"
#include "stopwatch_model.h"
#include "task_model.hpp"
#include "tusb.h"
#include "video_link.hpp"
#include <M5Unified.h>
#include <array>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <mutex>

namespace {
M5Canvas canvas(&M5.Display);
bool display_ok = false, video_ok = false, airplay_ok = false, task_ok = false;
unsigned page = 0;
bool dirty = true;
std::mutex task_lock;
taskui::State task_state;
std::atomic<bool> task_dirty{false}, task_focus{false};
std::array<std::string, 5> video_messages;
uint32_t last_draw = 0, last_battery = 0;
int battery = -1;
unsigned brightness_index = 1;
constexpr uint8_t brightness[] = {32, 80, 144, 220};
constexpr uint32_t background = 0x07131B, muted = 0x8FA5B3, ink = 0xE8F3F5;
constexpr uint32_t accent = 0x60E5BE, warning = 0xFFCA70;

void center(const char *text, int y, uint32_t color, float scale = 1) {
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(color, background);
  canvas.setTextSize(scale);
  canvas.drawString(text, 233, y);
}
void row(int y, const char *label, const std::string &value, bool good = true) {
  canvas.setTextSize(1);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(muted, background);
  canvas.drawString(label, 80, y);
  canvas.setTextDatum(middle_right);
  canvas.setTextColor(good ? ink : warning, background);
  // No credentials, HID command text, or phone screen contents are rendered.
  canvas.drawString(value.c_str(), 386, y);
}
void draw() {
  taskui::State task;
  {
    std::lock_guard<std::mutex> lock(task_lock);
    task = task_state;
  }
  const bool usb = tud_mounted() && !tud_suspended();
  const bool ble = ble_connected();
  const int ap = airplay_state();
  const bool healthy = video_ok && airplay_ok && task_ok;
  const auto state = stopwatch::phase(healthy, usb, ble, ap == AIRPLAY_STREAMING);
  const auto color = state == stopwatch::Phase::ready ? accent : warning;
  canvas.fillScreen(background);
  canvas.drawCircle(233, 233, 226, color);
  center("iPHONE AGENT", 57, muted);
  center(page == 1 ? "Task Monitor" : "StopWatch", 93, ink, 1.5f);
  char level[16];
  if (battery < 0)
    snprintf(level, sizeof level, "BAT --");
  else
    snprintf(level, sizeof level, "BAT %d%%", battery);
  // Right side of the upper header, within the round screen's visible chord.
  canvas.setTextDatum(middle_right);
  canvas.setTextSize(0.75f);
  canvas.setTextColor(muted, background);
  canvas.drawString(level, 372, 119);
  if (page == 1) {
    const bool stale = task.stale(now_ms());
    const uint32_t tone = stale || !strcmp(task.phase, "failed") ? warning : accent;
    center(stale ? "STATUS LOST" : !strcmp(task.phase, "idle") ? "NO TASK" : task.phase, 158, tone, 1.25f);
    row(205, "Step / action", std::to_string(task.step) + " / " + task.action);
    for (int i = 0; i < 4; i++)
      center(task.lines[i], 239 + i * 28, i == 0 ? ink : muted, 0.85f);
    if (task.code[0])
      center(task.code, 355, warning, 0.8f);
  } else if (page == 0) {
    center(stopwatch::headline(state), 158, color);
    row(205, "USB HID",
        !tud_mounted()    ? "Not connected"
        : tud_suspended() ? "Suspended"
                          : "Connected",
        usb);
    row(239, "Bluetooth", std::to_string(ble_peers()) + " peer(s)", ble);
    row(273, "Wi-Fi", std::string(net_mode()) + " / " + std::to_string(net_peers()) + " peer(s)",
        net_up());
    const char *mirror = ap == AIRPLAY_STREAMING   ? "Streaming"
                         : ap == AIRPLAY_CONNECTED ? "Connected"
                                                   : "Waiting";
    row(307, "AirPlay", airplay_ok ? mirror : "Start failed", airplay_ok);
    row(341, "Address", net_ip());
  } else {
    center("DIAGNOSTICS", 158, color);
    const auto video = video_stats();
    row(205, "Video", video_ok ? std::to_string(int(video.fps)) + " fps" : "Buffer failed",
        video_ok);
    row(239, "BLE", std::to_string(ble_kbps()) + " kbps");
    row(273, "RAM free",
        std::to_string(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) + " KB");
    row(307, "PSRAM free",
        std::to_string(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024) + " KB");
    row(341, "Video config",
        std::to_string(video.config_bytes) + " B / " + std::to_string(video.frames) + " frames",
        video.config_bytes && video.frames);
  }
  char elapsed[20];
  stopwatch::uptime(page == 1 && strcmp(task.phase, "idle") ? task.elapsed_now(now_ms()) : now_ms(),
                    elapsed, sizeof elapsed);
  center(elapsed, 382, muted);
  center("A: page   B: brightness", 411, muted, 0.75f);
  canvas.pushSprite(0, 0);
}
} // namespace

bool stopwatch_begin() {
  auto cfg = M5.config();
  cfg.output_power = false; // USB device role: never enable Grove/extension 5 V output.
  cfg.internal_spk = cfg.internal_mic = false;
  cfg.internal_imu = cfg.internal_rtc = false;
  cfg.external_display_value = 0;
  cfg.fallback_board = m5::board_t::board_unknown;
  M5.begin(cfg);
  if (M5.getBoard() != m5::board_t::board_M5StopWatch) {
    ESP_LOGE("board", "%s", "stopwatch: board detection failed; bridge not started");
    return false;
  }
  M5.Display.setRotation(0);
  M5.Display.setBrightness(brightness[brightness_index]);
  M5.Display.fillScreen(background);
  M5.Display.setTextColor(ink, background);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("Starting iPhone bridge...", 233, 233);
  if (!esp_psram_is_initialized() || esp_psram_get_size() < 4 * 1024 * 1024) {
    M5.Display.drawString("PSRAM unavailable - check build", 233, 267);
    ESP_LOGE("board", "%s", "stopwatch: OPI PSRAM unavailable; bridge not started");
    return false;
  }
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  canvas.setFont(&fonts::Font2);
  if (!canvas.createSprite(466, 466)) {
    M5.Display.drawString("Display buffer allocation failed", 233, 267);
    return false;
  }
  display_ok = true;
  return true;
}
void stopwatch_services(bool video, bool airplay, bool task) {
  video_ok = video;
  airplay_ok = airplay;
  task_ok = task;
  dirty = true;
}
void stopwatch_poll() {
  if (!display_ok)
    return;
  M5.update();
  if (task_dirty.exchange(false)) dirty = true;
  if (task_focus.exchange(false)) page = 1;
  if (M5.BtnA.wasClicked()) {
    page = (page + 1) % 3;
    dirty = true;
  }
  if (M5.BtnB.wasClicked()) {
    brightness_index = (brightness_index + 1) % (sizeof brightness / sizeof brightness[0]);
    M5.Display.setBrightness(brightness[brightness_index]);
    dirty = true;
  }
  const uint32_t now = now_ms();
  if (last_battery == 0 || stopwatch::due(now, last_battery, 10000)) {
    battery = M5.Power.getBatteryLevel();
    if (battery < 0 || battery > 100)
      battery = -1;
    last_battery = now;
  }
  if (dirty || stopwatch::due(now, last_draw, 1000)) {
    draw();
    last_draw = now;
    dirty = false;
  }
}

bool stopwatch_task(const std::string &json) {
  cJSON *j = cJSON_Parse(json.c_str());
  if (!j)
    return false;
  auto text = [&](const char *name, const char *fallback) {
    auto *v = cJSON_GetObjectItemCaseSensitive(j, name);
    return cJSON_IsString(v) ? v->valuestring : fallback;
  };
  auto number = [&](const char *name) {
    auto *v = cJSON_GetObjectItemCaseSensitive(j, name);
    return cJSON_IsNumber(v) && v->valuedouble >= 0 && v->valuedouble <= 86400000 ? v->valuedouble
                                                                                  : 0.0;
  };
  const bool reset = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "reset"));
  bool ok;
  {
    std::lock_guard<std::mutex> lock(task_lock);
    ok = task_state.apply(text("phase", ""), text("action", ""), text("code", ""), number("step"),
                          number("ms"), number("elapsed"), now_ms(),
                          reset);
  }
  cJSON_Delete(j);
  if (ok) {
    task_dirty = true;
    std::lock_guard<std::mutex> lock(task_lock);
    if (reset || task_state.terminal()) task_focus = true;
  }
  return ok;
}
void stopwatch_hid(const char *action, uint32_t duration) {
  if (!taskui::action_ok(action))
    return;
  std::lock_guard<std::mutex> lock(task_lock);
  task_state.log(action, duration);
}
void stopwatch_airplay_log(const char *message) {
  // These protocol-only lines contain no credentials, typed text or image payload.
  if (strncmp(message, "mirror:", 7) && strncmp(message, "pair-", 5) &&
      strncmp(message, "fp-", 3) && strncmp(message, "setup: mirror", 13) &&
      strcmp(message, "record") && strncmp(message, "teardown", 8))
    return;
  std::lock_guard<std::mutex> lock(task_lock);
  for (size_t i = video_messages.size() - 1; i > 0; --i)
    video_messages[i] = video_messages[i - 1];
  video_messages[0] = std::string(message).substr(0, 80);
}
std::string stopwatch_diagnostics() {
  std::lock_guard<std::mutex> lock(task_lock);
  cJSON *j = cJSON_CreateArray();
  if (!j)
    return "[]";
  for (const auto &line : video_messages)
    if (!line.empty())
      cJSON_AddItemToArray(j, cJSON_CreateString(line.c_str()));
  char *raw = cJSON_PrintUnformatted(j);
  std::string result = raw ? raw : "[]";
  cJSON_free(raw);
  cJSON_Delete(j);
  return result.size() < 470 ? result : "[]";
}
