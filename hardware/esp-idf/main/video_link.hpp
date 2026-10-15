#pragma once
#include "platform.hpp"

bool video_begin();
void video_on_config(const uint8_t *sps_pps, size_t len); // AirPlay callbacks (mirror task)
void video_on_au(const uint8_t *au, size_t len, bool idr);
void video_stream(bool on);
void video_snap(const std::string &id);               // replies "<id> ok" when the GOP is sent
void video_bench(const std::string &id, uint32_t kb); // replies "<id> ok <ms>"

struct VideoStats {
  uint32_t kbps, idr_ms, gop_bytes;
  float fps;
  uint32_t config_bytes, frames, overflows;
  bool waiting_idr, replayable, streaming;
};
VideoStats video_stats();
