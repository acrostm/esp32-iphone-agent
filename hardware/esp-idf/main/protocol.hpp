#pragma once
#include <string>
namespace protocol {
struct Request {
  std::string id, command;
  bool valid = false;
};
inline Request parse(std::string line) {
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
    line.pop_back();
  const auto space = line.find(' ');
  Request r{line.substr(0, space), space == std::string::npos ? "" : line.substr(space + 1), false};
  if (line.size() > 511 || r.id.empty() || r.id.size() > 15 || r.command.empty())
    return r;
  for (char c : r.id)
    if (c < '0' || c > '9')
      return r;
  r.valid = true;
  return r;
}
} // namespace protocol
