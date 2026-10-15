#include "ble_link.hpp"
#include "netcfg.hpp"
#include <mutex>
extern "C" {
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
void ble_store_config_init(void);
}
namespace {
#define UUID(x)                                                                                    \
  BLE_UUID128_INIT(x, 0x00, 0x3c, 0x8a, 0x6f, 0x6d, 0x39, 0x9c, 0x1b, 0x4b, 0xe8, 0xa9, 0x01,      \
                   0x00, 0x40, 0x6e)
const ble_uuid128_t uuids[] = {UUID(0), UUID(1), UUID(2), UUID(3), UUID(4), UUID(5)};
struct Peer {
  std::atomic<uint16_t> handle{BLE_HS_CONN_HANDLE_NONE};
  std::atomic<bool> sub[3]{};
};
Peer peers[2];
uint16_t handles[5];
ble_gatt_chr_def chars[6] = {};
ble_gatt_svc_def services[2] = {};
uint8_t own_type;
QueueHandle_t commands, replies;
std::mutex status_lock;
std::string status_value = "{}";
std::atomic<bool> status_pending{false};
std::atomic<uint32_t> drops{0}, rdrops{0}, bytes{0};
struct Text {
  char data[512];
};
void advertise();
int access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctx, void *arg) {
  const auto idx = reinterpret_cast<uintptr_t>(arg);
  if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    std::string s;
    if (idx == 4)
      s = net_json();
    else {
      std::lock_guard<std::mutex> l(status_lock);
      s = status_value;
    }
    return os_mbuf_append(ctx->om, s.data(), s.size()) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
  }
  if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
    return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
  Text t = {};
  uint16_t n = 0;
  if (OS_MBUF_PKTLEN(ctx->om) > sizeof(t.data) - 1)
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  if (ble_hs_mbuf_to_flat(ctx->om, t.data, sizeof(t.data) - 1, &n))
    return BLE_ATT_ERR_UNLIKELY;
  if (idx == 4)
    net_request(t.data);
  else if (xQueueSend(commands, &t, 0) != pdTRUE)
    return BLE_ATT_ERR_INSUFFICIENT_RES;
  return 0;
}
int gap(ble_gap_event *e, void *) {
  switch (e->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (e->connect.status == 0) {
      for (auto &p : peers)
        if (p.handle == BLE_HS_CONN_HANDLE_NONE) {
          for (auto &s : p.sub)
            s = false;
          p.handle = e->connect.conn_handle;
          break;
        }
      ble_gap_upd_params params = {};
      params.itvl_min = 12;
      params.itvl_max = 12;
      params.supervision_timeout = 400;
      ble_gap_update_params(e->connect.conn_handle, &params);
      ble_gap_set_prefered_le_phy(e->connect.conn_handle, BLE_GAP_LE_PHY_2M_MASK,
                                  BLE_GAP_LE_PHY_2M_MASK, 0);
      ble_gap_set_data_len(e->connect.conn_handle, 251, 2120);
    }
    advertise();
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    for (auto &p : peers)
      if (p.handle == e->disconnect.conn.conn_handle) {
        p.handle = BLE_HS_CONN_HANDLE_NONE;
        for (auto &s : p.sub)
          s = false;
      }
    advertise();
    break;
  case BLE_GAP_EVENT_ADV_COMPLETE:
    advertise();
    break;
  case BLE_GAP_EVENT_SUBSCRIBE:
    for (auto &p : peers)
      if (p.handle == e->subscribe.conn_handle)
        for (int i = 0; i < 3; i++)
          if (e->subscribe.attr_handle == handles[i + 1])
            p.sub[i] = e->subscribe.cur_notify;
    break;
  case BLE_GAP_EVENT_REPEAT_PAIRING: {
    ble_gap_conn_desc d;
    if (ble_gap_conn_find(e->repeat_pairing.conn_handle, &d) == 0)
      ble_store_util_delete_peer(&d.peer_id_addr);
    return BLE_GAP_REPEAT_PAIRING_RETRY;
  }
  default:
    break;
  }
  return 0;
}
void advertise() {
  if (ble_gap_adv_active() || ble_peers() >= 2)
    return;
  ble_hs_adv_fields f = {};
  f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  f.uuids128 = const_cast<ble_uuid128_t *>(&uuids[0]);
  f.num_uuids128 = 1;
  f.uuids128_is_complete = 1;
  if (ble_gap_adv_set_fields(&f))
    return;
  ble_hs_adv_fields rsp = {};
  auto name = ble_svc_gap_device_name();
  rsp.name = (uint8_t *)name;
  rsp.name_len = strlen(name);
  rsp.name_is_complete = 1;
  ble_gap_adv_rsp_set_fields(&rsp);
  ble_gap_adv_params p = {};
  p.conn_mode = BLE_GAP_CONN_MODE_UND;
  p.disc_mode = BLE_GAP_DISC_MODE_GEN;
  ble_gap_adv_start(own_type, nullptr, BLE_HS_FOREVER, &p, gap, nullptr);
}
void host_sync() {
  if (ble_hs_id_infer_auto(0, &own_type) == 0)
    advertise();
}
void host(void *) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}
bool notify(Peer &p, int index, const uint8_t *data, size_t n, uint32_t timeout) {
  uint32_t start = now_ms();
  do {
    auto h = p.handle.load();
    if (h == BLE_HS_CONN_HANDLE_NONE || !p.sub[index] || n > ble_att_mtu(h) - 3u)
      return false;
    if (index == 2 && os_msys_num_free() < 16) {
      sleep_ms(2);
      continue;
    }
    auto *om = ble_hs_mbuf_from_flat(data, n);
    if (om) {
      int rc = ble_gatts_notify_custom(h, handles[index + 1], om);
      if (!rc)
        return true;
      if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EAGAIN && rc != BLE_HS_EBUSY)
        return false;
    }
    sleep_ms(2);
  } while (now_ms() - start < timeout);
  return false;
}
void transmit(void *) {
  for (;;) {
    Text t;
    bool got = xQueueReceive(replies, &t, pdMS_TO_TICKS(10)) == pdTRUE;
    if (got) {
      bool sent = false;
      for (auto &p : peers)
        if (p.sub[0])
          sent = notify(p, 0, (uint8_t *)t.data, strlen(t.data), 500) || sent;
      if (!sent)
        rdrops++;
    }
    if (status_pending.exchange(false)) {
      std::string s;
      {
        std::lock_guard<std::mutex> l(status_lock);
        s = status_value;
      }
      for (auto &p : peers)
        if (p.sub[1])
          notify(p, 1, (uint8_t *)s.data(), s.size(), 100);
    }
  }
}
} // namespace
void ble_begin(const std::string &name) {
  commands = xQueueCreate(6, sizeof(Text));
  replies = xQueueCreate(8, sizeof(Text));
  ESP_ERROR_CHECK(commands && replies ? ESP_OK : ESP_ERR_NO_MEM);
  ESP_ERROR_CHECK(nimble_port_init());
  ble_svc_gap_init();
  ble_svc_gatt_init();
  for (int i = 0; i < 5; i++) {
    chars[i].uuid = &uuids[i + 1].u;
    chars[i].access_cb = access;
    chars[i].arg = reinterpret_cast<void *>(uintptr_t(i));
    chars[i].val_handle = &handles[i];
  }
  chars[0].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP;
  chars[1].flags = BLE_GATT_CHR_F_NOTIFY;
  chars[2].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
  chars[3].flags = BLE_GATT_CHR_F_NOTIFY;
  chars[4].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ_ENC |
                   BLE_GATT_CHR_F_WRITE_ENC;
  services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
  services[0].uuid = &uuids[0].u;
  services[0].characteristics = chars;
  ESP_ERROR_CHECK(ble_gatts_count_cfg(services));
  ESP_ERROR_CHECK(ble_gatts_add_svcs(services));
  ble_svc_gap_device_name_set(name.c_str());
  ble_att_set_preferred_mtu(517);
  ble_hs_cfg.sync_cb = host_sync;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = ble_hs_cfg.sm_our_key_dist;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_store_config_init();
  nimble_port_freertos_init(host);
  ESP_ERROR_CHECK(xTaskCreate(transmit, "ble_tx", 4096, nullptr, 3, nullptr) == pdPASS
                      ? ESP_OK
                      : ESP_ERR_NO_MEM);
}
int ble_peers() {
  int n = 0;
  for (auto &p : peers)
    n += p.handle != BLE_HS_CONN_HANDLE_NONE;
  return n;
}
bool ble_connected() { return ble_peers() > 0; }
bool ble_pop_cmd(std::string &line, uint32_t wait) {
  Text t;
  if (xQueueReceive(commands, &t, pdMS_TO_TICKS(wait)) != pdTRUE)
    return false;
  line = t.data;
  return true;
}
void ble_reply(const std::string &line) {
  Text t = {};
  if (line.size() >= sizeof t.data) {
    rdrops++;
    return;
  }
  memcpy(t.data, line.data(), line.size());
  if (xQueueSend(replies, &t, 0) != pdTRUE)
    rdrops++;
}
void ble_status(const std::string &s) {
  {
    std::lock_guard<std::mutex> l(status_lock);
    status_value = s;
  }
  status_pending = true;
}
uint16_t ble_mtu() {
  uint16_t result = 517;
  for (auto &p : peers)
    if (p.handle != BLE_HS_CONN_HANDLE_NONE)
      result = std::min<uint16_t>(result, ble_att_mtu(p.handle));
  return ble_connected() ? result : 23;
}
uint8_t ble_phy() {
  for (auto &p : peers)
    if (p.handle != BLE_HS_CONN_HANDLE_NONE) {
      uint8_t tx = 1, rx = 1;
      ble_gap_read_le_phy(p.handle, &tx, &rx);
      return tx;
    }
  return 1;
}
size_t ble_video_room() {
  size_t n = 512;
  bool active = false;
  for (auto &p : peers)
    if (p.handle != BLE_HS_CONN_HANDLE_NONE && p.sub[2]) {
      active = true;
      n = std::min<size_t>(n, std::max<int>(0, ble_att_mtu(p.handle) - 6));
    }
  return active ? n : 0;
}
bool ble_video_send(const uint8_t *data, size_t n) {
  bool active = false;
  for (auto &p : peers)
    if (p.sub[2]) {
      active = true;
      uint32_t start = now_ms();
      while ((uxQueueMessagesWaiting(replies) || status_pending) && now_ms() - start < 1000)
        sleep_ms(2);
      if (!notify(p, 2, data, n, 1000)) {
        drops++;
        return false;
      }
      bytes += n;
    }
  return active;
}
uint32_t ble_kbps() {
  static std::mutex lock;
  std::lock_guard<std::mutex> guard(lock);
  static uint32_t at = 0, total = 0, rate = 0;
  uint32_t now = now_ms(), b = bytes;
  if (now - at >= 1000) {
    rate = (b - total) * 8 / (now - at);
    total = b;
    at = now;
  }
  return rate;
}
uint32_t ble_drops() { return drops; }
uint32_t ble_reply_drops() { return rdrops; }
void ble_unpair() {
  uint32_t at = now_ms();
  while (uxQueueMessagesWaiting(replies) && now_ms() - at < 1000)
    sleep_ms(10);
  sleep_ms(100);
  ble_store_clear();
  for (auto &p : peers)
    if (p.handle != BLE_HS_CONN_HANDLE_NONE)
      ble_gap_terminate(p.handle, BLE_ERR_REM_USER_CONN_TERM);
}
