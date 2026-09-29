#pragma once
#include "esphome/core/component.h"
namespace esphome { namespace text_sensor {
class TextSensor {
 public:
  std::vector<std::string> values;
  void publish_state(const std::string &value) { values.push_back(value); }
};
}}
