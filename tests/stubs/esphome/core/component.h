#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <deque>
#include <functional>
#include <string>
#include <vector>
namespace esphome {
namespace setup_priority { constexpr float LATE = -100; }
class Component {
 public:
  virtual ~Component() = default;
  virtual float get_setup_priority() const { return 0; }
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
};
}
