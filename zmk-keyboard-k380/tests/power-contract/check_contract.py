"""Validate generated K380 power configuration and devicetree relationships."""

import argparse
import pickle
import sys
from pathlib import Path


def check_config(text, mode):
    config = dict(line.split("=", 1) for line in text.splitlines()
                  if line.startswith("CONFIG_") and "=" in line)
    assert config.get("CONFIG_K380_KSCAN_NO_DIODE_MATRIX") == "y", "custom matrix required"
    assert config.get("CONFIG_ZMK_KSCAN_MATRIX_POLLING", "n") == "n", "interrupt wake required"
    assert config.get("CONFIG_ZMK_PM_SOFT_OFF") == "y", "soft-off required"
    if mode == "driver":
        return
    required = {
        "CONFIG_K380_LOW_POWER_COORDINATOR": "y",
        "CONFIG_K380_SOFT_OFF": "y",
        "CONFIG_K380_BATTERY_POLICY": "y",
        "CONFIG_K380_BLE_SLOT_POLICY": "y",
        "CONFIG_ZMK_BATTERY_REPORT_INTERVAL": "60",
        "CONFIG_K380_BLE_WAIT_TIMEOUT_MS": "10000",
        "CONFIG_K380_BLE_PAIRING_TIMEOUT_MS": "60000",
        "CONFIG_K380_STARTUP_QUALIFICATION_BUDGET_MS": "1000",
    }
    for name, value in required.items():
        assert config.get(name) == value, f"{name} must be {value}"
    for name in ("CONFIG_K380_AUTO_SYSTEM_OFF", "CONFIG_ZMK_SLEEP", "CONFIG_ZMK_RGB_UNDERGLOW"):
        assert config.get(name, "n") == "n", f"{name} must remain disabled"


def check_wake(edt):
    matrices = edt.compat2okay.get("k380,kscan-no-diode-matrix", [])
    sources = edt.compat2okay.get("zmk,soft-off-wakeup-sources", [])
    assert len(matrices) == 1, "exactly one enabled K380 matrix required"
    matrix = matrices[0]
    wake = matrix.props.get("wakeup-source")
    assert wake and wake.val, "K380 matrix must be a wakeup-source"
    assert len(sources) == 1, "exactly one enabled soft-off wake-source node required"
    references = sources[0].props.get("wakeup-sources")
    assert references and references.val == [matrix], "soft-off wake must reference the K380 matrix"
    assert len(matrix.props["row-gpios"].val) == 8, "wake matrix must retain 8 rows"
    assert len(matrix.props["col-gpios"].val) == 15, "wake matrix must retain 15 columns"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("driver", "formal"), required=True)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    zephyr = args.build / "zephyr"
    check_config((zephyr / ".config").read_text(), args.mode)
    assert (zephyr / "zephyr.dts").is_file(), "generated DTS missing"
    sys.path.insert(0, str(Path("zephyr/scripts/dts/python-devicetree/src").resolve()))
    # This pickle is a trusted output of this job's Zephyr devicetree compiler.
    with (zephyr / "edt.pickle").open("rb") as stream:
        check_wake(pickle.load(stream))
    print(f"K380 {args.mode} power/wake contract passed")


if __name__ == "__main__":
    main()
