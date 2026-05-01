#pragma once

#include <array>
#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"

#ifdef USE_ARDUINO
class BTAdvertisedDevice;
#else
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#endif

namespace esphome {
namespace classic_bluetooth_presence {

class ClassicBluetoothPresence : public PollingComponent {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_discovery(bool discovery) { this->discovery_ = discovery; }
  void set_scan_duration(uint32_t scan_duration_ms) { this->scan_duration_ms_ = scan_duration_ms; }
  void set_presence_timeout(uint32_t presence_timeout_ms) { this->presence_timeout_ms_ = presence_timeout_ms; }
  void set_release_ble(bool release_ble) { this->release_ble_ = release_ble; }
  void set_startup_delay(uint32_t startup_delay_ms) { this->startup_delay_ms_ = startup_delay_ms; }
  void add_device(const std::string &address, binary_sensor::BinarySensor *sensor);

 protected:
  struct Device {
    std::array<uint8_t, 6> address{};
    std::string address_text;
    binary_sensor::BinarySensor *sensor{nullptr};
    uint32_t last_seen{0};
    bool has_published{false};
    bool published_state{false};
  };

  bool init_bluetooth_();
  bool parse_address_(const std::string &address, std::array<uint8_t, 6> *out) const;
  void start_scan_();
#ifdef USE_ARDUINO
  void stop_scan_();
  void handle_advertised_device_(BTAdvertisedDevice *device);
  static void advertised_device_callback_(BTAdvertisedDevice *device);
#else
  void handle_gap_event_(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
  void handle_discovery_result_(esp_bt_gap_cb_param_t *param);
  static void gap_callback_(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
#endif
  void publish_presence_();
  static ClassicBluetoothPresence *active_instance_;

#ifdef USE_ARDUINO
  void *serial_bt_{nullptr};
  uint32_t scan_end_time_{0};
#endif
  std::vector<Device> devices_;
  bool discovery_{false};
  bool release_ble_{true};
  bool bt_ready_{false};
  bool bt_init_attempted_{false};
  bool scanning_{false};
  bool scan_requested_{false};
  bool warned_no_devices_{false};
  uint32_t scan_duration_ms_{10000};
  uint32_t presence_timeout_ms_{90000};
  uint32_t startup_delay_ms_{30000};
};

}  // namespace classic_bluetooth_presence
}  // namespace esphome
