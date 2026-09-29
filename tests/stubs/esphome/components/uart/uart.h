#pragma once
#include "esphome/core/component.h"
namespace esphome { namespace uart {
enum UARTParityOptions { UART_CONFIG_PARITY_NONE };
class UARTComponent {
 public:
  void set_baud_rate(int) {}
  void set_stop_bits(int) {}
  void set_data_bits(int) {}
  void set_parity(UARTParityOptions) {}
};
class UARTDevice {
 public:
  std::deque<uint8_t> rx;
  std::vector<std::vector<uint8_t>> tx;
  std::function<void(const std::vector<uint8_t> &)> on_write;
  void feed(const std::string &s) { rx.insert(rx.end(), s.begin(), s.end()); }
  void feed(const std::vector<uint8_t> &v) { rx.insert(rx.end(), v.begin(), v.end()); }
  int available() const { return static_cast<int>(rx.size()); }
  bool read_byte(uint8_t *c) {
    if (rx.empty()) return false;
    *c = rx.front(); rx.pop_front(); return true;
  }
  void write_array(const uint8_t *p, size_t n) {
    std::vector<uint8_t> frame(p, p+n);
    tx.push_back(frame);
    if (on_write) on_write(frame);
  }
  void write_str(const char *s) { write_array(reinterpret_cast<const uint8_t *>(s), std::string(s).size()); }
  void flush() {}
 protected:
  UARTComponent uart_;
  UARTComponent *parent_ = &uart_;
};
}}
