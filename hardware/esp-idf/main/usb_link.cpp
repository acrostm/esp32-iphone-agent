#include "usb_link.hpp"
#include "esp_mac.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"
#include <array>

namespace {
enum { KEYBOARD = 1, MOUSE = 2, CONSUMER = 3 };
constexpr uint16_t AC_HOME = 0x0223;
const uint8_t reports[] = {TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(KEYBOARD)),
                           TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(MOUSE)),
                           TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(CONSUMER))};
enum { ITF_CDC, ITF_CDC_DATA, ITF_HID, ITF_COUNT };
const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0,
                          TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_HID_DESCRIPTOR(ITF_HID, 5, HID_ITF_PROTOCOL_NONE, sizeof reports, 0x83, 64, 8)};
char serial_number[13];
const char language[] = {0x09, 0x04};
const char *strings[] = {language,      "M5Stack",     "StopWatch iPhone Agent",
                         serial_number, "Diagnostics", "Mouse and keyboard"};
const tusb_desc_device_t device = {.bLength = sizeof(tusb_desc_device_t),
                                   .bDescriptorType = TUSB_DESC_DEVICE,
                                   .bcdUSB = 0x0200,
                                   .bDeviceClass = TUSB_CLASS_MISC,
                                   .bDeviceSubClass = MISC_SUBCLASS_COMMON,
                                   .bDeviceProtocol = MISC_PROTOCOL_IAD,
                                   .bMaxPacketSize0 = 64,
                                   .idVendor = 0x303A,
                                   .idProduct = 0x4001,
                                   .bcdDevice = 0x0100,
                                   .iManufacturer = 1,
                                   .iProduct = 2,
                                   .iSerialNumber = 3,
                                   .bNumConfigurations = 1};
QueueHandle_t lines;
uint8_t buttons = 0;
bool wait_hid() {
  const uint32_t start = now_ms();
  while (tud_mounted() && !tud_suspended() && !tud_hid_ready() && now_ms() - start < 300)
    sleep_ms(1);
  return tud_mounted() && !tud_suspended() && tud_hid_ready();
}
bool mouse(int x = 0, int y = 0, int wheel = 0, int pan = 0) {
  return wait_hid() && tud_hid_mouse_report(MOUSE, buttons, x, y, wheel, pan);
}
bool keyboard(uint8_t modifiers, const uint8_t *keys) {
  return wait_hid() && tud_hid_keyboard_report(KEYBOARD, modifiers, keys);
}
bool consumer(uint16_t value) {
  return wait_hid() && tud_hid_report(CONSUMER, &value, sizeof value);
}
const uint8_t ascii[128][2] = {HID_ASCII_TO_KEYCODE};
uint8_t named_key(const std::string &key) {
  if (key.size() == 1 && uint8_t(key[0]) < 128)
    return ascii[uint8_t(key[0])][1];
  if (key == "enter")
    return HID_KEY_ENTER;
  if (key == "esc")
    return HID_KEY_ESCAPE;
  if (key == "tab")
    return HID_KEY_TAB;
  if (key == "backspace")
    return HID_KEY_BACKSPACE;
  if (key == "space")
    return HID_KEY_SPACE;
  if (key == "up")
    return HID_KEY_ARROW_UP;
  if (key == "down")
    return HID_KEY_ARROW_DOWN;
  if (key == "left")
    return HID_KEY_ARROW_LEFT;
  if (key == "right")
    return HID_KEY_ARROW_RIGHT;
  return 0;
}
} // namespace

extern "C" const uint8_t *tud_hid_descriptor_report_cb(uint8_t) { return reports; }
extern "C" uint16_t tud_hid_get_report_cb(uint8_t, uint8_t, hid_report_type_t, uint8_t *,
                                          uint16_t) {
  return 0;
}
extern "C" void tud_hid_set_report_cb(uint8_t, uint8_t, hid_report_type_t, const uint8_t *,
                                      uint16_t) {}
extern "C" void tud_cdc_rx_cb(uint8_t) {
  static char line[512];
  static size_t used;
  static bool overflow;
  while (tud_cdc_available()) {
    char c = char(tud_cdc_read_char());
    if (c == '\n') {
      if (!overflow && used) {
        line[used] = 0;
        xQueueSend(lines, line, 0);
      }
      used = 0;
      overflow = false;
    } else if (c != '\r') {
      if (used < sizeof line - 1)
        line[used++] = c;
      else
        overflow = true;
    }
  }
}
bool usb_begin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(serial_number, sizeof serial_number, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  lines = xQueueCreate(6, 512);
  if (!lines)
    return false;
  tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
  cfg.descriptor.device = &device;
  cfg.descriptor.full_speed_config = configuration;
  cfg.descriptor.string = strings;
  cfg.descriptor.string_count = sizeof strings / sizeof strings[0];
  return tinyusb_driver_install(&cfg) == ESP_OK;
}
bool usb_ready() {
  if (tud_suspended()) {
    tud_remote_wakeup();
    for (int i = 0; i < 50 && tud_suspended(); ++i)
      sleep_ms(10);
  }
  return tud_mounted() && !tud_suspended();
}
const char *usb_state() { return !tud_mounted() ? "off" : tud_suspended() ? "asleep" : "ok"; }
void usb_release_all() {
  buttons = 0;
  if (!usb_ready())
    return;
  mouse();
  keyboard(0, nullptr);
  consumer(0);
}
bool usb_pop_line(std::string &line) {
  if (!lines)
    return false;
  char data[512];
  if (xQueueReceive(lines, data, 0) != pdTRUE)
    return false;
  line = data;
  return true;
}
void usb_reply(const std::string &line) {
  if (!tud_cdc_connected())
    return;
  const std::string data = line + "\r\n";
  const uint32_t start = now_ms();
  size_t offset = 0;
  while (offset < data.size() && now_ms() - start < 300 && tud_cdc_connected()) {
    offset += tud_cdc_write(data.data() + offset, data.size() - offset);
    tud_cdc_write_flush();
    if (offset < data.size())
      sleep_ms(1);
  }
}
std::string usb_command(const std::string &line) {
  const size_t sp = line.find(' ');
  const auto cmd = line.substr(0, sp);
  const auto arg = sp == std::string::npos ? std::string{} : line.substr(sp + 1);
  if (cmd == "ping")
    return "ok";
  if (!usb_ready()) {
    buttons = 0;
    return "err phone usb asleep (unlock the phone)";
  }
  long a = 0, b = 0;
  const int count = sscanf(arg.c_str(), "%ld %ld", &a, &b);
  bool ok = true;
  if (cmd == "home") {
    for (int i = 0; i < 50 && ok; i++) {
      ok = mouse(-127, -127);
      sleep_ms(2);
    }
  } else if (cmd == "move") {
    if (count != 2 || a < -4096 || a > 4096 || b < -4096 || b > 4096)
      return "err invalid move";
    while ((a || b) && ok) {
      const int x = std::clamp<long>(a, -10, 10), y = std::clamp<long>(b, -10, 10);
      ok = mouse(x, y);
      a -= x;
      b -= y;
      sleep_ms(8);
    }
  } else if (cmd == "press") {
    buttons = MOUSE_BUTTON_LEFT;
    ok = mouse();
  } else if (cmd == "release") {
    buttons = 0;
    ok = mouse();
  } else if (cmd == "click") {
    buttons = MOUSE_BUTTON_LEFT;
    ok = mouse();
    sleep_ms(60);
    buttons = 0;
    ok = mouse() && ok;
  } else if (cmd == "scroll") {
    if (count < 1 || a < -127 || a > 127 || b < -127 || b > 127)
      return "err invalid scroll";
    ok = mouse(0, 0, a, b);
  } else if (cmd == "type") {
    for (unsigned char c : arg)
      if (c >= 128 || (!ascii[c][1] && c))
        return "err ASCII only";
    for (unsigned char c : arg) {
      uint8_t keys[6] = {ascii[c][1]};
      ok = keyboard(ascii[c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0, keys);
      ok = keyboard(0, nullptr) && ok;
      if (!ok)
        break;
    }
  } else if (cmd == "key") {
    uint8_t modifiers = 0, keys[6] = {};
    size_t offset = 0, n = 0;
    for (;;) {
      const auto end = arg.find('+', offset);
      const auto key = arg.substr(offset, end == std::string::npos ? end : end - offset);
      if (key == "cmd")
        modifiers |= KEYBOARD_MODIFIER_LEFTGUI;
      else if (key == "ctrl")
        modifiers |= KEYBOARD_MODIFIER_LEFTCTRL;
      else if (key == "alt")
        modifiers |= KEYBOARD_MODIFIER_LEFTALT;
      else if (key == "shift")
        modifiers |= KEYBOARD_MODIFIER_LEFTSHIFT;
      else {
        const uint8_t k = named_key(key);
        if (!k || n == 6)
          return "err unknown key";
        if (key.size() == 1 && ascii[uint8_t(key[0])][0])
          modifiers |= KEYBOARD_MODIFIER_LEFTSHIFT;
        keys[n++] = k;
      }
      if (end == std::string::npos)
        break;
      offset = end + 1;
    }
    ok = keyboard(modifiers, keys);
    sleep_ms(30);
    ok = keyboard(0, nullptr) && ok;
  } else if (cmd == "homescreen") {
    ok = consumer(AC_HOME);
    sleep_ms(40);
    ok = consumer(0) && ok;
  } else
    return "err unknown command";
  if (!ok) {
    buttons = 0;
    mouse();
    keyboard(0, nullptr);
  }
  return ok ? "ok" : "err usb report timeout";
}
