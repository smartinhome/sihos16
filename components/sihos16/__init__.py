"""sihos16 — odbiornik wM-Bus dla SIH Reader 1.6 (CC1101) z własnym dekoderem.

Liczniki konfiguruje się na stronie modułu (/sihos16), nie w YAML. Przy kompilacji
powstaje pula 8 urządzeń z encjami; przy starcie moduł nadaje im nazwy i jednostki
z konfiguracji zapisanej w pamięci, a nieużyte encje pozostają ukryte.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import light, output, spi, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_FREQUENCY, CONF_ID
from esphome.core import CORE, ID
from esphome.core.entity_helpers import register_device_class, register_unit_of_measurement
from esphome.helpers import fnv1a_32bit_hash

CODEOWNERS = ["@smartinhome"]
DEPENDENCIES = ["esp32", "spi", "network"]
AUTO_LOAD = ["sensor", "text_sensor", "web_server_base"]

CONF_GDO0_PIN = "gdo0_pin"
CONF_GDO2_PIN = "gdo2_pin"
CONF_RGB_LED = "rgb_led"
CONF_STATUS_LED = "status_led"
CONF_RESET_PIN = "factory_reset_pin"

MAX_METERS = 8
MAX_NUM_FIELDS = 16
MAX_TEXT_FIELDS = 4

# Jednostki i klasy, które dekoder może nadać polom (muszą trafić do tablic ESPHome przy kompilacji).
UNITS = ["kWh", "GJ", "m³", "kg", "h", "d", "s", "W", "var", "kvarh", "V", "A", "°C", "K", "bar", "m³/h", "kg/h", "dBm"]
DEVICE_CLASSES = [
    "energy", "water", "gas", "volume", "weight", "duration", "power", "reactive_power", "reactive_energy",
    "voltage", "current", "temperature", "pressure", "volume_flow_rate", "signal_strength",
]

sihos16_ns = cg.esphome_ns.namespace("sihos16")
Sihos16 = sihos16_ns.class_("Sihos16", cg.Component, spi.SPIDevice)
SlotSensor = sihos16_ns.class_("SlotSensor", cg.EntityBase)
SlotText = sihos16_ns.class_("SlotText", cg.EntityBase)
Device = cg.esphome_ns.class_("Device")

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Sihos16),
            cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
            cv.Required(CONF_GDO0_PIN): pins.internal_gpio_input_pin_schema,
            cv.Required(CONF_GDO2_PIN): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_FREQUENCY, default=868.95): cv.float_range(min=868.0, max=870.0),
            # Dioda RGB pokazująca stan modułu (light), dioda mrugająca przy ramkach (output),
            # przycisk: przytrzymanie 10 s = ustawienia fabryczne (sieć WiFi i liczniki).
            cv.Optional(CONF_RGB_LED): cv.use_id(light.LightState),
            cv.Optional(CONF_STATUS_LED): cv.use_id(output.BinaryOutput),
            cv.Optional(CONF_RESET_PIN): pins.internal_gpio_input_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


def _entity_fields(internal: bool) -> int:
    return (1 << 24) if internal else 0


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    cg.add(var.set_gdo0_pin(await cg.gpio_pin_expression(config[CONF_GDO0_PIN])))
    cg.add(var.set_gdo2_pin(await cg.gpio_pin_expression(config[CONF_GDO2_PIN])))
    cg.add(var.set_frequency(config[CONF_FREQUENCY]))
    if CONF_RGB_LED in config:
        cg.add(var.set_rgb_led(await cg.get_variable(config[CONF_RGB_LED])))
    if CONF_STATUS_LED in config:
        cg.add(var.set_status_led(await cg.get_variable(config[CONF_STATUS_LED])))
    if CONF_RESET_PIN in config:
        cg.add(var.set_reset_pin(await cg.gpio_pin_expression(config[CONF_RESET_PIN])))
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    cg.add(var.set_web_base(base))

    cg.add_define("USE_ENTITY_UNIT_OF_MEASUREMENT")
    cg.add_define("USE_ENTITY_DEVICE_CLASS")
    for u in UNITS:
        cg.add(var.add_unit(u, register_unit_of_measurement(u)))
    for dc in DEVICE_CLASSES:
        cg.add(var.add_device_class(dc, register_device_class(dc)))

    # Urządzenia rejestrowane są w trakcie startu, tylko dla zajętych miejsc.
    cg.add_define("USE_DEVICES")
    cg.add_define("ESPHOME_DEVICE_COUNT", MAX_METERS)
    for s in range(MAX_METERS):
        dev_id = f"sihos16_licznik_{s + 1}"
        dev = cg.new_Pvariable(ID(dev_id, is_declaration=True, type=Device))
        cg.add(dev.set_device_id(fnv1a_32bit_hash(dev_id)))
        cg.add(dev.set_name(f"Licznik {s + 1}"))
        cg.add(var.set_device(s, dev))
        for i in range(MAX_NUM_FIELDS):
            e = cg.new_Pvariable(ID(f"sihos16_m{s + 1}_n{i + 1}", is_declaration=True, type=SlotSensor))
            cg.add(e.set_device_(dev))
            cg.add(cg.App.register_sensor(e, f"sihos16 m{s + 1} n{i + 1}", 0, _entity_fields(True)))
            CORE.register_platform_component("sensor", e)
            cg.add(var.set_num_entity(s, i, e))
        for i in range(MAX_TEXT_FIELDS):
            e = cg.new_Pvariable(ID(f"sihos16_m{s + 1}_t{i + 1}", is_declaration=True, type=SlotText))
            cg.add(e.set_device_(dev))
            cg.add(cg.App.register_text_sensor(e, f"sihos16 m{s + 1} t{i + 1}", 0, _entity_fields(True)))
            CORE.register_platform_component("text_sensor", e)
            cg.add(var.set_text_entity(s, i, e))
