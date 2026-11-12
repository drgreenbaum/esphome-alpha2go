import esphome.codegen as cg
from esphome.components import ble_client

CODEOWNERS = ["@parameter-pollution", "@drgreenbaum"]

alpha2_ns = cg.esphome_ns.namespace("alpha2")
Alpha2 = alpha2_ns.class_("Alpha2", ble_client.BLEClientNode, cg.PollingComponent)

CONF_ALPHA2_ID = "alpha2_id"
