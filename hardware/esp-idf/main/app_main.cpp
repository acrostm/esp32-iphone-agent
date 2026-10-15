#include "ble_link.hpp"
#include "board.hpp"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "netcfg.hpp"
#include "nvs_flash.h"
#include "platform.hpp"
#include "protocol.hpp"
#include "src/airplay/airplay.h"
#include "usb_link.hpp"
#include "video_link.hpp"
static std::string receiverName = "ESP32 Agent";
std::string statusJson(bool full);
const char *resetReason() {
  switch (esp_reset_reason()) {
  case ESP_RST_POWERON:
    return "power-on";
  case ESP_RST_BROWNOUT:
    return "brownout";
  case ESP_RST_TASK_WDT:
  case ESP_RST_INT_WDT:
  case ESP_RST_WDT:
    return "watchdog";
  case ESP_RST_PANIC:
    return "crash";
  case ESP_RST_SW:
    return "software";
  case ESP_RST_USB:
    return "usb";
  default:
    return "other";
  }
}

std::string board(const std::string &line, const std::string &id) {
  auto sp = line.find(' ');
  auto cmd = line.substr(0, sp);
  auto arg = sp == std::string::npos ? std::string{} : line.substr(sp + 1);
  long a = 0, b = 0;
  int n = sscanf(arg.c_str(), "%ld %ld", &a, &b);
  if (cmd == "status")
    return statusJson(true); // read-only diagnostics; no HID reports
  if (cmd == "stream") {
    if (arg != "on" && arg != "off")
      return "err usage: stream on|off";
    video_stream(arg == "on");
    return "ok";
  }
  if (cmd == "app" ||
      cmd == "log") { // debug relay: Mac -> app ("app run ...") and app -> Mac ("log ...")
    ble_reply(cmd + " " + arg); // every peer sees it; each side ignores lines it doesn't handle
    return "ok";
  }
  if (cmd == "snap") {
    video_snap(id);
    return "";
  } // replies when the GOP is sent
  if (cmd == "bench") {
    if (n < 1 || a <= 0 || a > 4096)
      return "err usage: bench KB (1-4096)";
    video_bench(id, a);
    return "";
  }
  if (cmd == "res") {
    if (n < 2 || a < 160 || b < 160 || a > 1920 || b > 2560)
      return "err usage: res W H";
    cfg_set_res(a, b);
    airplay_set_display(a, b, cfg_fps());
    return "ok";
  }
  if (cmd == "fps") {
    if (n < 1 || a < 1 || a > 60)
      return "err usage: fps N (1-60)";
    cfg_set_fps(a);
    airplay_set_display(cfg_width(), cfg_height(), a);
    return "ok";
  }
  if (cmd == "coex") { // A/B knob, not stored: every boot starts at balance
    if (arg.size() && !net_coex(arg))
      return "err usage: coex bt|balance|wifi";
    return std::string("ok ") + net_coex_mode();
  }
  if (cmd == "unpair") {
    ble_reply(id + " ok"); // answer before the link goes down
    ble_unpair();
    return "";
  }
  return usb_command(line);
}

std::string statusJson(bool full) {
  static const char *apStates[] = {"idle", "connected", "streaming"};
  VideoStats v = video_stats();
  char s[384];
  snprintf(s, sizeof s,
           "{\"up\":%lu,\"rst\":\"%s\",\"heap\":%u,\"psram\":%u,\"usb\":\"%s\",\"net\":\"%s\","
           "\"ip\":\"%s\",\"sta\":%d,"
           "\"ap\":\"%s\",\"w\":%u,\"h\":%u,\"kbps\":%lu,\"fps\":%.1f,\"idr\":%lu,\"gop\":%lu,"
           "\"ble\":{\"mtu\":%u,\"phy\":%u,\"kbps\":%lu,\"drop\":%lu,\"rdrop\":%lu,\"n\":%d}}",
           (unsigned long)now_ms(), resetReason(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), usb_state(), net_mode(),
           net_ip().c_str(), net_peers(), apStates[airplay_state()], cfg_width(), cfg_height(),
           (unsigned long)v.kbps, v.fps, (unsigned long)v.idr_ms, (unsigned long)v.gop_bytes,
           ble_mtu(), ble_phy(), (unsigned long)ble_kbps(), (unsigned long)ble_drops(),
           (unsigned long)ble_reply_drops(), ble_peers());
  if (full)
    return s;
  // MTU below ~250 (before the MTU exchange): keep only the fields the app needs to stay alive
  snprintf(s, sizeof s,
           "{\"up\":%lu,\"rst\":\"%s\",\"usb\":\"%s\",\"net\":\"%s\",\"ap\":\"%s\",\"ble\":{"
           "\"mtu\":%u}}",
           (unsigned long)now_ms(), resetReason(), usb_state(), net_mode(),
           apStates[airplay_state()], ble_mtu());
  return s;
}

static void status_task(void *) {
  for (;;) {
    sleep_ms(1000);
    if (ble_connected()) {
      auto s = statusJson(true);
      if (s.size() > ble_mtu() - 3u)
        s = statusJson(false);
      ble_status(s);
    }
  }
}
static void command_task(void *) {
  bool linked = false;
  for (;;) {
    if (linked && !ble_connected()) {
      usb_release_all();
      video_stream(false);
    }
    linked = ble_connected();
    std::string line;
    bool serial = usb_pop_line(line);
    if (!serial && !ble_pop_cmd(line, 20))
      continue;
    auto request = protocol::parse(line);
    auto id = request.id;
    auto reply = request.valid ? board(request.command, id) : "err missing request id";
    if (!reply.empty()) {
      if (serial)
        usb_reply(id + " " + reply);
      else
        ble_reply(id + " " + reply);
    }
  }
}
extern "C" void app_main() {
  // Do not erase stored settings automatically if initialization fails.
  ESP_ERROR_CHECK(nvs_flash_init());
  bool display = stopwatch_begin();
  ESP_ERROR_CHECK(usb_begin() ? ESP_OK : ESP_FAIL);
  if (!display) {
    ESP_LOGE("agent", "Board/display/PSRAM initialization failed; bridge disabled");
    return;
  }
  net_begin();
  bool video = video_begin();
  ble_begin("ESP32 Agent " + board_suffix());
  static airplay_cfg cfg = {};
  cfg.name = receiverName.c_str();
  esp_read_mac(cfg.hwaddr, ESP_MAC_WIFI_STA);
  cfg_ed25519_seed(cfg.ed25519_seed);
  cfg.width = cfg_width();
  cfg.height = cfg_height();
  cfg.max_fps = cfg_fps();
  cfg.on_config = video_on_config;
  cfg.on_au = video_on_au;
  bool airplay = video && airplay_start(&cfg);
  bool tasks = xTaskCreate(status_task, "status", 4096, nullptr, 1, nullptr) == pdPASS;
  tasks = (xTaskCreate(command_task, "commands", 6144, nullptr, 2, nullptr) == pdPASS) && tasks;
  stopwatch_services(video, airplay, tasks);
  ESP_LOGI("agent", "Native ESP-IDF StopWatch ready: display=%d video=%d airplay=%d tasks=%d",
           display, video, airplay, tasks);
  for (;;) {
    net_poll();
    stopwatch_poll();
    sleep_ms(20);
  }
}
