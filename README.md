# ESPHome Bluetooth Classic Presence

External ESPHome component for ESP32 boards with Bluetooth Classic support. It periodically runs a Classic Bluetooth GAP inquiry, logs discovered devices when discovery mode is enabled, and publishes binary sensors for configured Bluetooth addresses.

This is for Bluetooth Classic, not BLE. It is useful for devices that do not advertise over BLE but can be found by Classic inquiry.

## Example

```yaml
external_components:
  - source:
      type: local
      path: ./components

logger:
  level: DEBUG

classic_bluetooth_presence:
  discovery: true
  update_interval: 30s
  scan_duration: 10s
  presence_timeout: 90s
  devices:
    - name: "Bluetooth speaker present"
      bt_addr: "AA:BB:CC:DD:EE:FF"
```

With `discovery: true`, watch the ESPHome logs. Each scan prints lines like:

```text
[I][classic_bluetooth_presence:xxx]: Discovered BT Classic device: AA:BB:CC:DD:EE:FF RSSI=-63 name="Speaker"
```

Copy the address into `bt_addr`, then set `discovery: false` once you no longer need the device list.

## Options

- `discovery`: log all discovered Bluetooth Classic devices. Defaults to `false`.
- `update_interval`: how often a scan is started. Defaults to `30s`.
- `scan_duration`: inquiry duration. Defaults to `10s`.
- `presence_timeout`: how long a device remains present after the last successful sighting. Defaults to `90s`.
- `release_ble`: release BLE memory before enabling Classic Bluetooth. Defaults to `true`. Set it to `false` only if you know you need BLE and your ESP32/framework combination supports coexistence.
- `devices`: list of binary sensors, each with `name` and `bt_addr`.

## Notes

Do not use this together with ESPHome BLE tracking unless you have explicitly tested coexistence. Bluetooth Classic scanning uses the ESP-IDF GAP APIs and requires an ESP32 variant with Classic Bluetooth support; ESP32-C3, ESP32-C6, and ESP32-S3 do not support Bluetooth Classic.

The component calls ESPHome's `include_builtin_idf_component("bt")` when available, which is needed by newer ESPHome versions that exclude unused ESP-IDF components from builds by default.
