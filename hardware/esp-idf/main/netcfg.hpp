#pragma once
#include "platform.hpp"

void net_begin();
void net_poll();                           // call from loop(): applies a pending `net` write
std::string net_json();                    // value of the `net` characteristic
void net_request(const std::string &json); // `net` write; applied by net_poll()
const char *net_mode();                    // "ap" | "sta" | "off" | "none" (station not connected)
std::string net_ip();
int net_peers(); // stations on our SoftAP, or 1 when connected as a station
bool net_up();   // has an IP address
bool net_coex(
    const std::string &pref); // Wi-Fi/BLE radio share: "bt" | "balance" (boot default) | "wifi"
const char *net_coex_mode();

uint16_t cfg_width();
uint16_t cfg_height();
uint8_t cfg_fps();
void cfg_set_res(uint16_t w, uint16_t h);
void cfg_set_fps(uint8_t fps);
void cfg_ed25519_seed(uint8_t seed[32]); // created once, stable across boots
std::string board_suffix();              // "1A2B": last two BT MAC bytes
