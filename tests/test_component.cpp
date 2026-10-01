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

std::vector<uint8_t> reply(uint16_t command, std::vector<uint8_t> payload, uint16_t status = 0) {
  const size_t length = payload.size() + 4;
  std::vector<uint8_t> frame = {0xFD,0xFC,0xFB,0xFA,uint8_t(length),uint8_t(length >> 8),
                              uint8_t(command),1,uint8_t(status),uint8_t(status >> 8)};
  frame.insert(frame.end(), payload.begin(), payload.end());
  frame.insert(frame.end(), {4,3,2,1});
  return frame;
}

void test_parameter_readback() {
  HLKLD2402Component radar;
  radar.on_write = [&](const std::vector<uint8_t> &frame) {
    assert(frame == std::vector<uint8_t>({0xFD,0xFC,0xFB,0xFA,6,0,8,0,1,0,4,0,4,3,2,1}));
    radar.feed(reply(CMD_GET_VERSION, {})); // Unrelated stale reply must be skipped.
    radar.feed(reply(CMD_GET_PARAMS, {100,0,0,0,4,3,2,1})); // Embedded footer is a value.
  };
  std::vector<uint32_t> values;
  assert(radar.get_parameters_batch_({1,4}, values));
  assert(values == std::vector<uint32_t>({100,0x01020304}));
  for (int failure = 0; failure < 5; failure++) {
    radar.on_write = [&](const std::vector<uint8_t> &) {
      auto frame = reply(CMD_GET_PARAMS, {1,0,0,128}, failure == 0 ? 1 : 0);
      if (failure == 1) frame.back() = 0; // Wrong footer.
      if (failure == 2) frame.resize(11); // Truncated frame.
      if (failure == 3) frame[4] = 255; // Too long for supplied data.
      if (failure == 4) frame = reply(CMD_GET_PARAMS, {1,0}); // Short value.
      radar.feed(frame);
    };
    uint32_t value = 99;
    assert(!radar.get_parameter_(1, value));
    assert(value == 99);
  }
  radar.on_write = [&](const std::vector<uint8_t> &) {radar.feed(reply(CMD_GET_PARAMS, {1,0,0,128}));};
  uint32_t value = 0;
  assert(radar.get_parameter_(1, value) && value == 0x80000001u);
}

void test_diagnostics(bool fail_read, bool fail_exit) {
  HLKLD2402Component radar;
  radar.on_write = [&](const std::vector<uint8_t> &frame) {
    const uint16_t cmd = frame[6] | (uint16_t(frame[7]) << 8);
    assert(cmd == CMD_ENABLE_CONFIG || cmd == CMD_DISABLE_CONFIG || cmd == CMD_GET_PARAMS);
    if (cmd == CMD_GET_PARAMS) {
      const size_t count = (frame.size() - 12) / 2;
      std::vector<uint8_t> data(count * 4, 0);
      for (size_t i = 0; i < count; i++) data[i * 4] = 100;
      radar.feed(reply(cmd, data, fail_read ? 1 : 0));
    } else if (!(cmd == CMD_DISABLE_CONFIG && fail_exit)) radar.feed(reply(cmd, {}));
  };
  assert(radar.read_radar_diagnostics() == (!fail_read && !fail_exit));
  assert(radar.tx.back()[6] == CMD_DISABLE_CONFIG);
  assert(radar.config_mode_ == fail_exit);
  assert(radar.tx.size() == (fail_read ? 3 : 7) + (fail_exit ? 2 : 0));
  radar.tx.clear();
  radar.calibration_in_progress_ = true;
  assert(!radar.read_radar_diagnostics() && radar.tx.empty());
}

void test_diagnostic_entry_failure() {
  HLKLD2402Component radar;
  radar.on_write = [&](const std::vector<uint8_t> &frame) {
    assert(frame[6] == CMD_ENABLE_CONFIG || frame[6] == CMD_DISABLE_CONFIG);
    if (frame[6] == CMD_DISABLE_CONFIG) radar.feed(reply(CMD_DISABLE_CONFIG, {}));
  };
  assert(!radar.read_radar_diagnostics());
  assert(!radar.config_mode_ && radar.tx.size() == 2);
}

void test_active_version() {
  HLKLD2402Component radar;
  radar.config_mode_ = true;
  radar.on_write = [&](const std::vector<uint8_t> &) {
    radar.feed(reply(CMD_GET_VERSION, {6,0,'v','3','.','3','.','5'}));
  };
  radar.get_firmware_version_();
  assert(radar.firmware_version_ == "v3.3.5");
}

void test_apply_settings(int failure) {
  HLKLD2402Component radar;
  radar.set_max_distance(3.5f);
  radar.set_timeout(5);
  bool saved = false;
  radar.on_write = [&](const std::vector<uint8_t> &frame) {
    const uint16_t cmd = frame[6];
    assert(cmd == CMD_ENABLE_CONFIG || cmd == CMD_DISABLE_CONFIG || cmd == CMD_SET_PARAMS ||
           cmd == CMD_GET_PARAMS || cmd == CMD_SAVE_PARAMS);
    if (cmd == CMD_SET_PARAMS) {
      const std::vector<uint8_t> data(frame.begin()+8, frame.end()-4);
      assert(data == std::vector<uint8_t>({1,0,35,0,0,0,4,0,5,0,0,0}));
    }
    if (cmd == CMD_GET_PARAMS) {
      radar.feed(reply(cmd, {uint8_t(failure == 2 ? 85 : 35),0,0,0,5,0,0,0}));
    } else {
      if (cmd == CMD_SAVE_PARAMS) saved = true;
      if (failure == 4 && cmd == CMD_DISABLE_CONFIG) return;
      radar.feed(reply(cmd, {}, ((failure == 1 && cmd == CMD_SET_PARAMS) ||
                                (failure == 3 && cmd == CMD_SAVE_PARAMS)) ? 1 : 0));
    }
  };
  assert(radar.apply_detection_settings() == (failure == 0));
  assert(saved == (failure != 1 && failure != 2));
  assert(radar.tx.back()[6] == CMD_DISABLE_CONFIG);
  assert(radar.config_mode_ == (failure == 4));
  radar.tx.clear();
  radar.set_max_distance(11);
  assert(!radar.apply_detection_settings() && radar.tx.empty());
  radar.set_max_distance(3.5f);
  radar.calibration_in_progress_ = true;
  assert(!radar.apply_detection_settings() && radar.tx.empty());
}

void test_exit_retry_and_recovery() {
  HLKLD2402Component radar;
  radar.config_mode_ = true;
  radar.operating_mode_ = "Normal";
  unsigned attempts = 0;
  radar.on_write = [&](const std::vector<uint8_t> &frame) {
    assert(frame[6] == CMD_DISABLE_CONFIG); // Recovery must not re-save or rewrite settings.
    if (++attempts == 2) radar.feed(reply(CMD_DISABLE_CONFIG, {}));
  };
  radar.set_normal_mode_direct();
  assert(attempts == 2 && !radar.config_mode_);
  radar.set_normal_mode_direct();
  assert(attempts == 2);
}

void test_engineering_reports() {
  // Manufacturer V1.08 example: 102 cm, first motion=4598, first micro=206769.
  std::vector<uint8_t> frame(141, 0);
  frame[0]=0xF4; frame[1]=0xF3; frame[2]=0xF2; frame[3]=0xF1;
  frame[4]=131; frame[6]=1; frame[7]=102;
  frame[9]=0xF6; frame[10]=0x11;
  frame[73]=0xB1; frame[74]=0x27; frame[75]=3;
  for (int i=0;i<4;i++) frame[137+i]=0xF8-i;
  for (size_t split=1;split<frame.size();split++) {
    HLKLD2402Component radar; prepare(radar); test_time=10000;
    sensor::Sensor energy, distance;
    binary_sensor::BinarySensor presence, micro;
    radar.set_energy_gate_sensor(0,&energy); radar.set_distance_sensor(&distance);
    radar.set_presence_binary_sensor(&presence); radar.set_micromovement_binary_sensor(&micro);
    radar.feed(std::vector<uint8_t>(frame.begin(),frame.begin()+split)); radar.loop();
    assert(presence.values.empty());
    radar.feed(std::vector<uint8_t>(frame.begin()+split,frame.end())); radar.loop();
    assert(presence.values.back() && !micro.values.back());
    assert(distance.values.back()==102);
    assert(energy.values.back()>36.61f && energy.values.back()<36.63f);
    auto stationary=frame; stationary[6]=2;
    auto absent=frame; absent[6]=0;
    radar.feed(stationary); radar.feed(absent); radar.loop();
    assert(micro.values[micro.values.size()-2] && !micro.values.back());
    assert(!presence.values.back() && radar.report_buffer_.empty());
    auto bad=frame; bad.back()=0;
    const size_t before=presence.values.size();
    radar.feed(bad); radar.loop(); assert(presence.values.size()==before);
    radar.feed(std::vector<uint8_t>(frame.begin(),frame.begin()+50)); radar.loop();
    test_time+=101; radar.feed(frame); radar.loop(); assert(presence.values.back());
  }
}

void test_presence_beyond_five_metres() {
  HLKLD2402Component radar;
  prepare(radar);
  radar.set_max_distance(6.0f);
  binary_sensor::BinarySensor presence;
  radar.set_presence_binary_sensor(&presence);
  for (const char *line : {"distance:490", "distance:510", "distance:599", "distance:662"}) {
    radar.process_line_(line);
    assert(presence.values.back());
  }
  // A local range setting must not contradict a target still reported by hardware.
  radar.process_line_("OFF");
  assert(!presence.values.back());
  radar.process_line_("distance:550");
  assert(presence.values.back());
  radar.process_line_("distance:0");
  assert(!presence.values.back());
}

int main() {
  for (int failure = 0; failure < 5; failure++) test_apply_settings(failure);
  test_presence_beyond_five_metres();
  test_engineering_reports();
  test_exit_retry_and_recovery();
  test_parameter_readback();
  test_active_version();
  test_diagnostic_entry_failure();
  for (bool fail_read : {false,true})
    for (bool fail_exit : {false,true}) test_diagnostics(fail_read, fail_exit);
  test_events_and_throttling();
  test_fragmented_text_and_instances();
  test_header_mismatch();
  test_passive_version();
  for (bool sensor : {false, true})
    for (bool legacy : {false, true}) test_calibration(sensor, legacy);
  std::cout << "All component regression cases passed, including 140 engineering frame splits.\n";
}
