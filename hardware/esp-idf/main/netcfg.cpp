#include "netcfg.hpp"
#include "cJSON.h"
#include "esp_coexist.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs.h"
#include <mutex>
namespace {
nvs_handle_t prefs;
std::recursive_mutex guard;
std::mutex pending_guard;
std::string mode = "ap", apSsid, apPass, staSsid, staPass, pending;
esp_netif_t *ap, *sta;
std::atomic<bool> connected{false};
std::atomic<int> coex{ESP_COEX_PREFER_BALANCE};
std::atomic<uint16_t> width{480}, height{1040};
std::atomic<uint8_t> fps{5};
uint32_t retryAt, retryGap = 5000;
std::string get(const char *key, const char *fallback = "") {
  size_t size = 0;
  if (nvs_get_str(prefs, key, nullptr, &size) != ESP_OK || !size)
    return fallback;
  std::string s(size, '\0');
  if (nvs_get_str(prefs, key, s.data(), &size) != ESP_OK)
    return fallback;
  s.resize(size - 1);
  return s;
}
void put(const char *key, const std::string &value) {
  ESP_ERROR_CHECK(nvs_set_str(prefs, key, value.c_str()));
}
std::string field(cJSON *j, const char *key) {
  auto *v = cJSON_GetObjectItemCaseSensitive(j, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}
void event(void *, esp_event_base_t base, int32_t id, void *) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    connected = false;
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    connected = true;
}
void start() {
  esp_wifi_stop();
  connected = false;
  if (mode == "off")
    return;
  wifi_config_t c = {};
  ESP_ERROR_CHECK(esp_wifi_set_mode(mode == "sta" ? WIFI_MODE_STA : WIFI_MODE_AP));
  if (mode == "sta") {
    memcpy(c.sta.ssid, staSsid.data(), staSsid.size());
    memcpy(c.sta.password, staPass.data(), staPass.size());
    c.sta.threshold.authmode = staPass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &c));
  } else {
    memcpy(c.ap.ssid, apSsid.data(), apSsid.size());
    memcpy(c.ap.password, apPass.data(), apPass.size());
    c.ap.ssid_len = apSsid.size();
    c.ap.channel = 6;
    c.ap.max_connection = 4;
    c.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &c));
  }
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(20));
  if (mode == "sta") {
    esp_wifi_connect();
    retryGap = 5000;
    retryAt = now_ms() + retryGap;
  } else {
    uint8_t off = 0;
    esp_netif_dhcps_stop(ap);
    esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &off,
                           sizeof off);
    esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &off, sizeof off);
    esp_netif_dhcps_start(ap);
  }
}
} // namespace
std::string board_suffix() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char s[5];
  snprintf(s, sizeof s, "%02X%02X", mac[4], mac[5]);
  return s;
}
void net_begin() {
  std::lock_guard<std::recursive_mutex> l(guard);
  ESP_ERROR_CHECK(nvs_open("agent", NVS_READWRITE, &prefs));
  apSsid = get("ap_ssid");
  apPass = get("ap_pass");
  if (apSsid.empty() || apPass.size() < 8) {
    apSsid = "ESP32Agent-" + board_suffix();
    apPass.clear();
    const char *abc = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < 12; i++)
      apPass += abc[esp_random() % strlen(abc)];
    put("ap_ssid", apSsid);
    put("ap_pass", apPass);
  }
  mode = get("mode", "ap");
  staSsid = get("sta_ssid");
  staPass = get("sta_pass");
  if ((mode != "ap" && mode != "sta" && mode != "off") || (mode == "sta" && staSsid.empty()))
    mode = "ap";
  uint16_t w = 480, h = 1040;
  uint8_t f = 5;
  nvs_get_u16(prefs, "w", &w);
  nvs_get_u16(prefs, "h", &h);
  nvs_get_u8(prefs, "fps", &f);
  width = w;
  height = h;
  fps = f;
  ESP_ERROR_CHECK(nvs_commit(prefs));
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ap = esp_netif_create_default_wifi_ap();
  sta = esp_netif_create_default_wifi_sta();
  esp_netif_set_hostname(ap, "iphone-agent");
  esp_netif_set_hostname(sta, "iphone-agent");
  wifi_init_config_t c = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&c));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, nullptr));
  start();
  net_coex("balance");
}
void net_request(const std::string &json) {
  if (json.size() > 511)
    return;
  std::lock_guard<std::mutex> l(pending_guard);
  pending = json;
}
void net_poll() {
  std::lock_guard<std::recursive_mutex> l(guard);
  if (mode == "sta" && !connected && (int32_t)(now_ms() - retryAt) >= 0) {
    esp_wifi_disconnect();
    esp_wifi_connect();
    retryGap = std::min<uint32_t>(retryGap * 2, 60000);
    retryAt = now_ms() + retryGap;
  }
  std::string request;
  {
    std::lock_guard<std::mutex> pending_lock(pending_guard);
    request.swap(pending);
  }
  if (request.empty())
    return;
  auto j = cJSON_Parse(request.c_str());
  if (!j)
    return;
  auto m = field(j, "mode"), s = field(j, "ssid"), p = field(j, "pass");
  cJSON_Delete(j);
  if (m == "sta") {
    if (s.empty() || s.size() > 32 || p.size() > 63 || (!p.empty() && p.size() < 8))
      return;
    staSsid = s;
    staPass = p;
    put("sta_ssid", s);
    put("sta_pass", p);
  } else if (m != "ap" && m != "off")
    return;
  mode = m;
  put("mode", mode);
  ESP_ERROR_CHECK(nvs_commit(prefs));
  start();
}
const char *net_mode() {
  std::lock_guard<std::recursive_mutex> l(guard);
  return mode == "off" ? "off" : mode == "sta" ? (connected ? "sta" : "none") : "ap";
}
std::string net_ip() {
  std::lock_guard<std::recursive_mutex> l(guard);
  esp_netif_ip_info_t info = {};
  if (mode == "off" || (mode == "sta" && !connected))
    return "0.0.0.0";
  esp_netif_get_ip_info(mode == "sta" ? sta : ap, &info);
  char buf[20];
  snprintf(buf, sizeof buf, IPSTR, IP2STR(&info.ip));
  return buf;
}
int net_peers() {
  std::lock_guard<std::recursive_mutex> l(guard);
  if (mode == "off")
    return 0;
  if (mode == "sta")
    return connected;
  wifi_sta_list_t list = {};
  return esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}
bool net_up() { return net_ip() != "0.0.0.0"; }
std::string net_json() {
  std::lock_guard<std::recursive_mutex> l(guard);
  auto *j = cJSON_CreateObject();
  if (!j)
    return "{}";
  cJSON_AddStringToObject(j, "mode", mode.c_str());
  cJSON_AddStringToObject(j, "ssid", (mode == "sta" ? staSsid : apSsid).c_str());
  cJSON_AddStringToObject(j, "pass", (mode == "sta" ? staPass : apPass).c_str());
  cJSON_AddStringToObject(j, "ip", net_ip().c_str());
  char *p = cJSON_PrintUnformatted(j);
  std::string s = p ? p : "{}";
  cJSON_free(p);
  cJSON_Delete(j);
  return s;
}
bool net_coex(const std::string &s) {
  const char *names[] = {"wifi", "bt", "balance"};
  for (int i = 0; i < 3; i++)
    if (s == names[i] && esp_coex_preference_set((esp_coex_prefer_t)i) == ESP_OK) {
      coex = i;
      return true;
    }
  return false;
}
const char *net_coex_mode() {
  const char *names[] = {"wifi", "bt", "balance"};
  return names[coex];
}
uint16_t cfg_width() { return width; }
uint16_t cfg_height() { return height; }
uint8_t cfg_fps() { return fps; }
void cfg_set_res(uint16_t w, uint16_t h) {
  std::lock_guard<std::recursive_mutex> l(guard);
  ESP_ERROR_CHECK(nvs_set_u16(prefs, "w", w));
  ESP_ERROR_CHECK(nvs_set_u16(prefs, "h", h));
  ESP_ERROR_CHECK(nvs_commit(prefs));
  width = w;
  height = h;
}
void cfg_set_fps(uint8_t f) {
  std::lock_guard<std::recursive_mutex> l(guard);
  ESP_ERROR_CHECK(nvs_set_u8(prefs, "fps", f));
  ESP_ERROR_CHECK(nvs_commit(prefs));
  fps = f;
}
void cfg_ed25519_seed(uint8_t seed[32]) {
  std::lock_guard<std::recursive_mutex> l(guard);
  size_t n = 32;
  if (nvs_get_blob(prefs, "ed_seed", seed, &n) == ESP_OK && n == 32)
    return;
  esp_fill_random(seed, 32);
  ESP_ERROR_CHECK(nvs_set_blob(prefs, "ed_seed", seed, 32));
  ESP_ERROR_CHECK(nvs_commit(prefs));
}
