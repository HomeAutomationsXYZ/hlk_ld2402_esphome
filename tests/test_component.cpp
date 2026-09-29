// Compile the real component against a deterministic UART/clock test double.
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#define private public
#define protected public
#include "hlk_ld2402.h"
#undef private
#undef protected
#include <cassert>
#include <iostream>
using namespace esphome;
using namespace esphome::hlk_ld2402;

void prepare(HLKLD2402Component &radar) {
  radar.firmware_check_done_ = true;
  radar.power_check_done_ = true;
}

void test_events_and_throttling() {
  HLKLD2402Component radar;
  prepare(radar);
  binary_sensor::BinarySensor presence;
  sensor::Sensor distance;
  radar.set_presence_binary_sensor(&presence);
  radar.set_distance_sensor(&distance);
  test_time = 3000;
  radar.feed(std::string("distance:100\nOFF\ndistance:120\n"));
  radar.loop();
  assert((presence.values == std::vector<bool>{true, false, true}));
  assert(distance.values.size() == 1); // Numeric publication stays throttled.
}

void test_fragmented_text_and_instances() {
  HLKLD2402Component a, b;
  prepare(a); prepare(b);
  binary_sensor::BinarySensor pa, pb;
  a.set_presence_binary_sensor(&pa); b.set_presence_binary_sensor(&pb);
  test_time = 4000;
  a.feed(std::string("distance:")); a.loop();
  test_time += 30;
  a.feed(std::string("100\n")); a.loop();
  b.feed(std::string("distance:100\n")); b.loop();
  assert(pa.values.size() == 1 && pb.values.size() == 1);
  assert(a.byte_count_ == 13 && b.byte_count_ == 13);
  a.feed(std::string("OFF")); a.loop();
  test_time += 101;
  b.feed(std::string("OFF\n")); b.loop();
  a.loop();
  assert(a.line_buffer_.empty()); // B's traffic cannot reset A's idle timer.
}

void test_header_mismatch() {
  HLKLD2402Component radar;
  prepare(radar);
  test_time = 5000;
  radar.feed(std::vector<uint8_t>{0xF4, 'X', 'a', 'b', 'c'});
  radar.loop();
  assert(radar.line_buffer_ == std::string("\xF4Xabc", 5));
}

void test_passive_version() {
  HLKLD2402Component radar;
  prepare(radar);
  text_sensor::TextSensor version;
  radar.set_firmware_version_text_sensor(&version);
  radar.begin_passive_version_detection_();
  radar.feed(std::string("version v3.5.5\n")); radar.loop();
  assert(version.values.back() == "v3.5.5");
}

void test_calibration(bool with_sensor, bool legacy_format) {
  HLKLD2402Component radar;
  prepare(radar);
  sensor::Sensor progress;
  if (with_sensor) radar.set_calibration_progress_sensor(&progress);
  radar.config_mode_ = true;
  radar.calibration_in_progress_ = true;
  radar.last_calibration_check_ = 0;
  test_time = 6000;
  radar.on_write = [&](const std::vector<uint8_t> &frame) {
    if (frame[6] == CMD_GET_CALIBRATION_STATUS) {
      if (legacy_format)
        radar.feed(std::vector<uint8_t>{0xFD,0xFC,0xFB,0xFA,0,0,100,0,4,3,2,1});
      else
        radar.feed(std::vector<uint8_t>{0xFD,0xFC,0xFB,0xFA,6,0,10,1,0,0,100,0,4,3,2,1});
    }
  };
  radar.loop();
  assert(!radar.calibration_in_progress_);
  assert(!radar.config_mode_);
  assert(radar.tx.size() == 2 && radar.tx.back()[6] == CMD_DISABLE_CONFIG);
  if (with_sensor) assert(progress.values.back() == 100);
}

int main() {
  test_events_and_throttling();
  test_fragmented_text_and_instances();
  test_header_mismatch();
  test_passive_version();
  for (bool sensor : {false, true})
    for (bool legacy : {false, true}) test_calibration(sensor, legacy);
  std::cout << "All 8 component regression cases passed.\n";
}
