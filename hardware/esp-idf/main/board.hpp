#pragma once
#include "platform.hpp"
bool stopwatch_begin();
void stopwatch_services(bool, bool, bool);
void stopwatch_poll();

bool stopwatch_task(const std::string &json);
void stopwatch_hid(const char *action, uint32_t duration);
void stopwatch_airplay_log(const char *message);
std::string stopwatch_diagnostics();
