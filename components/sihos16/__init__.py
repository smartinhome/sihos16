"""sihos16 — odbiornik wM-Bus dla SIH Reader 1.6 (CC1101) z własnym dekoderem."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi
from esphome.const import CONF_FREQUENCY, CONF_ID

CODEOWNERS = ["@smartinhome"]
DEPENDENCIES = ["esp32", "spi"]

CONF_GDO0_PIN = "gdo0_pin"
CONF_GDO2_PIN = "gdo2_pin"

sihos16_ns = cg.esphome_ns.namespace("sihos16")
Sihos16 = sihos16_ns.class_("Sihos16", cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Sihos16),
            cv.Required(CONF_GDO0_PIN): pins.internal_gpio_input_pin_schema,
            cv.Required(CONF_GDO2_PIN): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_FREQUENCY, default=868.95): cv.float_range(min=868.0, max=870.0),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    cg.add(var.set_gdo0_pin(await cg.gpio_pin_expression(config[CONF_GDO0_PIN])))
    cg.add(var.set_gdo2_pin(await cg.gpio_pin_expression(config[CONF_GDO2_PIN])))
    cg.add(var.set_frequency(config[CONF_FREQUENCY]))
