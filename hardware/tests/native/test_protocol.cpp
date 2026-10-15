#include "protocol.hpp"
#include <cassert>
int main() {
  auto p = protocol::parse("0 ping\r\n");
  assert(p.valid && p.id == "0" && p.command == "ping");
  p = protocol::parse("42 type hello world  \n");
  assert(p.valid && p.command == "type hello world");
  for (auto s :
       {"", "1", "1 ", "-1 click", "abc home", "1x move 2 3", "1234567890123456 ping", " ping"})
    assert(!protocol::parse(s).valid);
  assert(protocol::parse("123456789012345 ping").valid);
  assert(!protocol::parse("1 " + std::string(510, 'x')).valid);
}
