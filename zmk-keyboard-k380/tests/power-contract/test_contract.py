import unittest
from types import SimpleNamespace

from check_contract import check_config, check_controllers, check_wake


class PowerContractTests(unittest.TestCase):
    def setUp(self):
        self.config = """CONFIG_K380_KSCAN_NO_DIODE_MATRIX=y
CONFIG_ZMK_PM_SOFT_OFF=y
CONFIG_K380_LOW_POWER_COORDINATOR=y
CONFIG_K380_SOFT_OFF=y
CONFIG_K380_BATTERY_POLICY=y
CONFIG_K380_BLE_SLOT_POLICY=y
CONFIG_ZMK_BATTERY_REPORT_INTERVAL=60
CONFIG_K380_BLE_WAIT_TIMEOUT_MS=30000
CONFIG_K380_BLE_PAIRING_TIMEOUT_MS=60000
CONFIG_K380_STARTUP_QUALIFICATION_BUDGET_MS=1000
"""
        self.matrix = SimpleNamespace(props={
            "wakeup-source": SimpleNamespace(val=True),
            "row-gpios": SimpleNamespace(val=[None] * 8),
            "col-gpios": SimpleNamespace(val=[None] * 15),
        })
        self.source = SimpleNamespace(props={
            "wakeup-sources": SimpleNamespace(val=[self.matrix]),
        })
        self.edt = SimpleNamespace(compat2okay={
            "k380,kscan-no-diode-matrix": [self.matrix],
            "zmk,soft-off-wakeup-sources": [self.source],
        })

    def test_safe_formal_config_and_matrix_reference(self):
        check_config(self.config, "formal")
        check_wake(self.edt)

    def test_unsafe_or_missing_formal_config_is_rejected(self):
        for name, value in (
            ("CONFIG_K380_AUTO_SYSTEM_OFF", "y"),
            ("CONFIG_ZMK_SLEEP", "y"),
            ("CONFIG_ZMK_RGB_UNDERGLOW", "y"),
            ("CONFIG_ZMK_KSCAN_MATRIX_POLLING", "y"),
            ("CONFIG_ZMK_BATTERY_REPORT_INTERVAL", "61"),
            ("CONFIG_K380_LOW_POWER_COORDINATOR", "n"),
            ("CONFIG_K380_STARTUP_QUALIFICATION_BUDGET_MS", "1001"),
        ):
            config = "\n".join(line for line in self.config.splitlines()
                               if not line.startswith(name + "="))
            with self.subTest(name=name), self.assertRaises(AssertionError):
                check_config(config + f"\n{name}={value}\n", "formal")
        with self.assertRaises(AssertionError):
            check_config(self.config.replace("CONFIG_ZMK_PM_SOFT_OFF=y", ""), "formal")

    def test_missing_or_wrong_wake_reference_is_rejected(self):
        self.matrix.props["wakeup-source"].val = False
        with self.assertRaises(AssertionError):
            check_wake(self.edt)
        self.matrix.props["wakeup-source"].val = True
        self.source.props["wakeup-sources"].val = []
        with self.assertRaises(AssertionError):
            check_wake(self.edt)

    def test_driver_requires_wake_but_not_board_policy_owners(self):
        check_config("CONFIG_K380_KSCAN_NO_DIODE_MATRIX=y\nCONFIG_ZMK_PM_SOFT_OFF=y\n", "driver")

    def test_formal_controllers_must_be_enabled(self):
        self.edt.label2node = {
            label: SimpleNamespace(status="okay") for label in ("gpio0", "gpio1", "gpiote")
        }
        check_controllers(self.edt)
        for label in ("gpio0", "gpio1", "gpiote"):
            self.edt.label2node[label].status = "disabled"
            with self.subTest(label=label), self.assertRaises(AssertionError):
                check_controllers(self.edt)
            self.edt.label2node[label].status = "okay"
        del self.edt.label2node["gpiote"]
        with self.assertRaises(AssertionError):
            check_controllers(self.edt)


if __name__ == "__main__":
    unittest.main()
