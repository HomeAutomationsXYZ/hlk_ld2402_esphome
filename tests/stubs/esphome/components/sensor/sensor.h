#pragma once
#include "esphome/core/component.h"
namespace esphome { namespace sensor {
class Sensor {
 public:
  std::vector<float> values;
  void publish_state(float value) { values.push_back(value); }
};
}}
