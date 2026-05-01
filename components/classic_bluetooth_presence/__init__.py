import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_ID,
)
from esphome.core import CORE

CODEOWNERS = ["@marianomd"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["binary_sensor"]

CONF_DEVICES = "devices"
CONF_DISCOVERY = "discovery"
CONF_BT_ADDR = "bt_addr"
CONF_SCAN_DURATION = "scan_duration"
CONF_PRESENCE_TIMEOUT = "presence_timeout"
CONF_RELEASE_BLE = "release_ble"
CONF_STARTUP_DELAY = "startup_delay"
CONF_ENABLED = "enabled"

MAC_ADDRESS_RE = re.compile(r"^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")


def validate_mac_address(value):
    value = cv.string_strict(value)
    if not MAC_ADDRESS_RE.match(value):
        raise cv.Invalid("Bluetooth address must use format AA:BB:CC:DD:EE:FF")
    return value.upper()


classic_bt_presence_ns = cg.esphome_ns.namespace("classic_bluetooth_presence")
ClassicBluetoothPresence = classic_bt_presence_ns.class_(
    "ClassicBluetoothPresence", cg.PollingComponent
)

DEVICE_SCHEMA = binary_sensor.binary_sensor_schema(
    icon="mdi:bluetooth",
    entity_category="diagnostic",
).extend(
    {
        cv.Required(CONF_BT_ADDR): validate_mac_address,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ClassicBluetoothPresence),
        cv.Optional(CONF_DISCOVERY, default=False): cv.boolean,
        cv.Optional(CONF_ENABLED, default=False): cv.boolean,
        cv.Optional(CONF_SCAN_DURATION, default="10s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_PRESENCE_TIMEOUT, default="90s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_RELEASE_BLE, default=False): cv.boolean,
        cv.Optional(CONF_STARTUP_DELAY, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_DEVICES, default=[]): cv.ensure_list(DEVICE_SCHEMA),
    }
).extend(cv.polling_component_schema("30s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_discovery(config[CONF_DISCOVERY]))
    cg.add(var.set_enabled(config[CONF_ENABLED]))
    cg.add(var.set_scan_duration(config[CONF_SCAN_DURATION].total_milliseconds))
    cg.add(var.set_presence_timeout(config[CONF_PRESENCE_TIMEOUT].total_milliseconds))
    cg.add(var.set_release_ble(config[CONF_RELEASE_BLE]))
    cg.add(var.set_startup_delay(config[CONF_STARTUP_DELAY].total_milliseconds))

    for device_config in config[CONF_DEVICES]:
        sens = await binary_sensor.new_binary_sensor(device_config)
        cg.add(var.add_device(device_config[CONF_BT_ADDR], sens))

    try:
        from esphome.components.esp32 import include_builtin_idf_component

        include_builtin_idf_component("bt")
    except ImportError:
        pass

    if CORE.using_arduino:
        cg.add_library("BluetoothSerial", None)
    if CORE.is_esp32:
        try:
            from esphome.components import esp32

            esp32.add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
            esp32.add_idf_sdkconfig_option("CONFIG_BT_CLASSIC_ENABLED", True)
            esp32.add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", True)
            esp32.add_idf_sdkconfig_option("CONFIG_BT_SPP_ENABLED", True)
        except Exception:
            pass
