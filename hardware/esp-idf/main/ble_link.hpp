#pragma once
#include "platform.hpp"

void ble_begin(const std::string &name);
bool ble_connected();
bool ble_pop_cmd(std::string &line, uint32_t wait_ms); // next `cmd` write, oldest first
void ble_reply(const std::string &line);               // queued; sent before any further video
void ble_status(const std::string &json);              // latest wins; sent before any further video
size_t ble_video_room(); // payload bytes per video notify; 0 = nobody listening
bool ble_video_send(const uint8_t *p, size_t n); // waits for free buffers; false = gave up
void ble_unpair();

uint16_t ble_mtu();
uint8_t ble_phy();          // 1 or 2 (Mbit)
uint32_t ble_kbps();        // video bytes out, last second
uint32_t ble_drops();       // video notifies we failed to send
uint32_t ble_reply_drops(); // replies that never got out to a subscribed peer
int ble_peers();            // connected centrals

int ble_encrypted_peers();
int ble_security_result();
int ble_disconnect_reason();
