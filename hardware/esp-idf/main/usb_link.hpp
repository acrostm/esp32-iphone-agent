#pragma once
#include "platform.hpp"
bool usb_begin();
bool usb_ready();
const char *usb_state();
void usb_release_all();
std::string usb_command(const std::string &line);
bool usb_pop_line(std::string &line);
void usb_reply(const std::string &line);
