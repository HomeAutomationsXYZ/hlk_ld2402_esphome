#pragma once
#include "esphome/core/component.h"
namespace esphome { namespace binary_sensor {
class BinarySensor {
 public:
  std::vector<bool> values;
  void publish_state(bool value) { values.push_back(value); }
};
}}
