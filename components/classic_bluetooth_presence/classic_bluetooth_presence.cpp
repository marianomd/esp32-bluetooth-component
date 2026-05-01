#include "classic_bluetooth_presence.h"

#include <algorithm>
#include <cstdio>

#include "esphome/core/log.h"

#ifdef USE_ARDUINO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <BluetoothSerial.h>
#include <BTAdvertisedDevice.h>
#pragma GCC diagnostic pop
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace esphome {
namespace classic_bluetooth_presence {

static const char *const TAG = "classic_bluetooth_presence";

ClassicBluetoothPresence *ClassicBluetoothPresence::active_instance_ = nullptr;

void ClassicBluetoothPresence::setup() {
  if (active_instance_ != nullptr && active_instance_ != this) {
    this->mark_failed(LOG_STR("Only one classic_bluetooth_presence hub can be configured"));
    return;
  }
  active_instance_ = this;

  if (!this->enabled_) {
    ESP_LOGW(TAG, "Bluetooth Classic scanner is disabled. Set enabled: true to start it.");
    return;
  }

  if (this->devices_.empty() && !this->discovery_) {
    ESP_LOGW(TAG, "No devices configured and discovery is disabled");
    this->warned_no_devices_ = true;
    return;
  }

  ESP_LOGI(TAG, "Bluetooth Classic scanner will start after %.1f s", this->startup_delay_ms_ / 1000.0f);
}

void ClassicBluetoothPresence::loop() {
  if (!this->enabled_ || this->warned_no_devices_) {
    this->publish_presence_();
    return;
  }

  if (!this->bt_init_attempted_ && millis() >= this->startup_delay_ms_) {
    this->bt_init_attempted_ = true;
    this->bt_ready_ = this->init_bluetooth_();
    if (!this->bt_ready_) {
      this->status_set_error(LOG_STR("Bluetooth Classic init failed"));
      ESP_LOGE(TAG, "Bluetooth Classic init failed; scanner disabled");
    } else {
      this->status_clear_error();
      ESP_LOGI(TAG, "Bluetooth Classic scanner ready");
    }
  }

  if (!this->bt_ready_) {
    this->publish_presence_();
    return;
  }

#ifdef USE_ARDUINO
  if (this->scanning_ && millis() >= this->scan_end_time_) {
    this->stop_scan_();
  }
#endif

  this->publish_presence_();

  if (this->scan_requested_ && !this->scanning_) {
    this->scan_requested_ = false;
    this->start_scan_();
  }
}

void ClassicBluetoothPresence::update() {
  if (!this->enabled_ || this->warned_no_devices_ || !this->bt_ready_)
    return;

  if (!this->scanning_) {
    this->start_scan_();
  } else {
    this->scan_requested_ = true;
  }

  this->publish_presence_();
}

void ClassicBluetoothPresence::dump_config() {
  ESP_LOGCONFIG(TAG, "Classic Bluetooth Presence:");
  ESP_LOGCONFIG(TAG, "  Enabled: %s", YESNO(this->enabled_));
  ESP_LOGCONFIG(TAG, "  Discovery logging: %s", YESNO(this->discovery_));
  ESP_LOGCONFIG(TAG, "  Scan duration: %.1f s", this->scan_duration_ms_ / 1000.0f);
  ESP_LOGCONFIG(TAG, "  Presence timeout: %.1f s", this->presence_timeout_ms_ / 1000.0f);
  ESP_LOGCONFIG(TAG, "  Startup delay: %.1f s", this->startup_delay_ms_ / 1000.0f);
  ESP_LOGCONFIG(TAG, "  Release BLE memory: %s", YESNO(this->release_ble_));
  for (const auto &device : this->devices_) {
    LOG_BINARY_SENSOR("  ", "Device", device.sensor);
    ESP_LOGCONFIG(TAG, "    BT address: %s", device.address_text.c_str());
  }
}

void ClassicBluetoothPresence::add_device(const std::string &address, binary_sensor::BinarySensor *sensor) {
  Device device;
  device.address_text = address;
  device.sensor = sensor;
  if (!this->parse_address_(address, &device.address)) {
    ESP_LOGE(TAG, "Invalid Bluetooth address: %s", address.c_str());
    return;
  }
  this->devices_.push_back(device);
}

bool ClassicBluetoothPresence::init_bluetooth_() {
#ifdef USE_ARDUINO
  if (this->serial_bt_ == nullptr) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    this->serial_bt_ = new BluetoothSerial();
#pragma GCC diagnostic pop
  }
  auto *serial_bt = static_cast<BluetoothSerial *>(this->serial_bt_);
  if (!serial_bt->begin("ESPHomeBTPresence", true, this->release_ble_)) {
    ESP_LOGE(TAG, "BluetoothSerial.begin failed");
    return false;
  }
  return true;
#else
  esp_err_t err;

  if (this->release_ble_) {
    err = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG, "BLE memory release failed: %s", esp_err_to_name(err));
    }
  }

  esp_bt_controller_status_t controller_status = esp_bt_controller_get_status();
  if (controller_status == ESP_BT_CONTROLLER_STATUS_IDLE) {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_bt_controller_init failed: %s", esp_err_to_name(err));
      return false;
    }
    controller_status = esp_bt_controller_get_status();
  }

  if (controller_status != ESP_BT_CONTROLLER_STATUS_ENABLED) {
    err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s", esp_err_to_name(err));
      return false;
    }
  }

  esp_bluedroid_status_t bluedroid_status = esp_bluedroid_get_status();
  if (bluedroid_status == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
    err = esp_bluedroid_init();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_bluedroid_init failed: %s", esp_err_to_name(err));
      return false;
    }
    bluedroid_status = esp_bluedroid_get_status();
  }

  if (bluedroid_status != ESP_BLUEDROID_STATUS_ENABLED) {
    err = esp_bluedroid_enable();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGE(TAG, "esp_bluedroid_enable failed: %s", esp_err_to_name(err));
      return false;
    }
  }

  err = esp_bt_gap_register_callback(ClassicBluetoothPresence::gap_callback_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_bt_gap_register_callback failed: %s", esp_err_to_name(err));
    return false;
  }

  esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  return true;
#endif
}

bool ClassicBluetoothPresence::parse_address_(const std::string &address, std::array<uint8_t, 6> *out) const {
  unsigned int values[6];
  if (std::sscanf(address.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &values[0], &values[1], &values[2], &values[3],
                  &values[4], &values[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; i++) {
    if (values[i] > 0xFF)
      return false;
    (*out)[i] = static_cast<uint8_t>(values[i]);
  }
  return true;
}

void ClassicBluetoothPresence::start_scan_() {
#ifdef USE_ARDUINO
  auto *serial_bt = static_cast<BluetoothSerial *>(this->serial_bt_);
  if (serial_bt == nullptr) {
    ESP_LOGE(TAG, "BluetoothSerial is not initialized");
    return;
  }
  if (serial_bt->discoverAsync(ClassicBluetoothPresence::advertised_device_callback_, this->scan_duration_ms_)) {
    this->scanning_ = true;
    this->scan_end_time_ = millis() + this->scan_duration_ms_ + 250;
    ESP_LOGD(TAG, "Started Bluetooth Classic inquiry for %.2f s", this->scan_duration_ms_ / 1000.0f);
  } else {
    ESP_LOGE(TAG, "BluetoothSerial.discoverAsync failed");
  }
#else
  uint8_t inquiry_len = static_cast<uint8_t>((this->scan_duration_ms_ + 1279) / 1280);
  inquiry_len = std::max<uint8_t>(1, std::min<uint8_t>(48, inquiry_len));

  esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, inquiry_len, 0);
  if (err == ESP_OK) {
    this->scanning_ = true;
    ESP_LOGD(TAG, "Started Bluetooth Classic inquiry for %.2f s", inquiry_len * 1.28f);
  } else if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "Bluetooth inquiry is already running");
    this->scanning_ = true;
  } else {
    ESP_LOGE(TAG, "esp_bt_gap_start_discovery failed: %s", esp_err_to_name(err));
  }
#endif
}

#ifdef USE_ARDUINO
void ClassicBluetoothPresence::stop_scan_() {
  auto *serial_bt = static_cast<BluetoothSerial *>(this->serial_bt_);
  if (serial_bt != nullptr) {
    serial_bt->discoverAsyncStop();
  }
  this->scanning_ = false;
  this->publish_presence_();
  ESP_LOGD(TAG, "Bluetooth Classic inquiry finished");
}

void ClassicBluetoothPresence::advertised_device_callback_(BTAdvertisedDevice *device) {
  if (active_instance_ != nullptr) {
    active_instance_->handle_advertised_device_(device);
  }
}

void ClassicBluetoothPresence::handle_advertised_device_(BTAdvertisedDevice *device) {
  if (device == nullptr)
    return;

  std::string address = device->getAddress().toString(true).c_str();
  std::string name = device->haveName() ? device->getName() : "";

  if (this->discovery_) {
    if (device->haveRSSI()) {
      ESP_LOGI(TAG, "Discovered BT Classic device: %s RSSI=%d name=\"%s\"", address.c_str(), device->getRSSI(),
               name.c_str());
    } else {
      ESP_LOGI(TAG, "Discovered BT Classic device: %s name=\"%s\"", address.c_str(), name.c_str());
    }
  }

  for (auto &configured : this->devices_) {
    if (configured.address_text == address) {
      configured.last_seen = millis();
      ESP_LOGD(TAG, "Matched configured device %s", configured.address_text.c_str());
    }
  }
}
#else
void ClassicBluetoothPresence::gap_callback_(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  if (active_instance_ != nullptr) {
    active_instance_->handle_gap_event_(event, param);
  }
}

void ClassicBluetoothPresence::handle_gap_event_(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
      this->handle_discovery_result_(param);
      break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        this->scanning_ = false;
        this->publish_presence_();
        ESP_LOGD(TAG, "Bluetooth Classic inquiry finished");
      }
      break;
    default:
      break;
  }
}

void ClassicBluetoothPresence::handle_discovery_result_(esp_bt_gap_cb_param_t *param) {
  const uint8_t *bda = param->disc_res.bda;
  int rssi = 0;
  bool has_rssi = false;
  std::string name;

  for (int i = 0; i < param->disc_res.num_prop; i++) {
    esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];
    if (prop->type == ESP_BT_GAP_DEV_PROP_RSSI) {
      rssi = *reinterpret_cast<int8_t *>(prop->val);
      has_rssi = true;
    } else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
      uint8_t len = 0;
      uint8_t *eir = static_cast<uint8_t *>(prop->val);
      uint8_t *eir_name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
      if (eir_name == nullptr) {
        eir_name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
      }
      if (eir_name != nullptr && len > 0) {
        name.assign(reinterpret_cast<char *>(eir_name), len);
      }
    }
  }

  char address[18];
  std::snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2], bda[3], bda[4],
                bda[5]);

  if (this->discovery_) {
    if (has_rssi) {
      ESP_LOGI(TAG, "Discovered BT Classic device: %s RSSI=%d name=\"%s\"", address, rssi, name.c_str());
    } else {
      ESP_LOGI(TAG, "Discovered BT Classic device: %s name=\"%s\"", address, name.c_str());
    }
  }

  for (auto &device : this->devices_) {
    if (std::equal(device.address.begin(), device.address.end(), bda)) {
      device.last_seen = millis();
      ESP_LOGD(TAG, "Matched configured device %s", device.address_text.c_str());
    }
  }
}
#endif

void ClassicBluetoothPresence::publish_presence_() {
  const uint32_t now = millis();
  for (auto &device : this->devices_) {
    if (device.sensor == nullptr)
      continue;

    const bool present = device.last_seen != 0 && now - device.last_seen <= this->presence_timeout_ms_;
    if (!device.has_published || present != device.published_state) {
      device.sensor->publish_state(present);
      device.has_published = true;
      device.published_state = present;
    }
  }
}

}  // namespace classic_bluetooth_presence
}  // namespace esphome
