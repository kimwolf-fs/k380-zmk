import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]


def read(path):
    return (REPO_ROOT / path).read_text(encoding="utf-8")


def kconfig_body(source, symbol):
    match = re.search(
        rf"(?ms)^config {re.escape(symbol)}\n(?P<body>.*?)(?=^config |\Z)",
        source,
    )
    if not match:
        return ""
    return match.group("body")


def binding_refs(layer_body):
    bindings = re.search(r"bindings\s*=\s*<(?P<value>.*?)>\s*;", layer_body, re.S)
    if not bindings:
        return []
    return re.findall(r"&[A-Za-z0-9_]+", re.sub(r"\s+", " ", bindings.group("value")))


class K380ConfigToolContract(unittest.TestCase):
    def test_config_tool_symbol_owns_host_configuration_channel(self):
        source = read("zmk-keyboard-k380/Kconfig")
        body = kconfig_body(source, "K380_CONFIG_TOOL")

        for dependency in (
            "K380_DYNAMIC_CONFIG",
            "K380_DYNAMIC_KEYMAP",
            "K380_DYNAMIC_MACRO",
            "K380_DYNAMIC_PRESET_SWITCH",
        ):
            self.assertRegex(body, rf"\b{dependency}\b")

        for selected in (
            "K380_DYNAMIC_PROTOCOL",
            "K380_DYNAMIC_TRANSPORT",
        ):
            self.assertRegex(body, rf"(?m)^\s*select {selected}\s*$")

        self.assertNotRegex(
            body,
            r"(?m)^\s*select K380_DYNAMIC_(CONFIG|KEYMAP|MACRO|PRESET_SWITCH)\s*$",
        )
        self.assertNotIn("ZMK_STUDIO", kconfig_body(source, "K380_DYNAMIC_PROTOCOL"))
        self.assertNotIn("ZMK_STUDIO_TRANSPORT_UART", kconfig_body(source, "K380_DYNAMIC_TRANSPORT"))

    def test_k380_board_defconfig_enables_runtime_dynamic_behavior_without_host_tool(self):
        source = read("app/boards/kimwolf/k380/k380_nrf52840_zmk_defconfig")

        self.assertNotRegex(source, r"(?m)^CONFIG_K380_CONFIG_TOOL=y$")
        for required in (
            "CONFIG_K380_DYNAMIC_CONFIG=y",
            "CONFIG_K380_DYNAMIC_KEYMAP=y",
            "CONFIG_K380_DYNAMIC_MACRO=y",
            "CONFIG_K380_DYNAMIC_PRESET_SWITCH=y",
        ):
            self.assertRegex(source, rf"(?m)^{required}$")

        self.assertNotRegex(source, r"(?m)^CONFIG_K380_DYNAMIC_PROTOCOL=y$")
        self.assertNotRegex(source, r"(?m)^CONFIG_K380_DYNAMIC_TRANSPORT=y$")

    def test_config_usb_uart_snippet_enables_host_tool(self):
        source = read("app/snippets/k380-config-usb-uart/k380-config-usb-uart.conf")

        self.assertRegex(source, r"(?m)^CONFIG_K380_CONFIG_TOOL=y$")

    def test_formal_k380_ci_builds_without_zmk_studio(self):
        source = read(".github/workflows/k380-ci.yml")

        self.assertIn("-S k380-config-usb-uart", source)
        self.assertNotIn("-S studio-rpc-usb-uart", source)
        self.assertNotIn("-DCONFIG_ZMK_STUDIO=y", source)
        self.assertIn('"CONFIG_K380_CONFIG_TOOL": "y"', source)
        self.assertIn("# CONFIG_ZMK_STUDIO is not set", source)
        self.assertIn("k380,config-uart", source)
        self.assertIn('"zmk,studio-rpc-uart" not in dts', source)

    def test_k380_keymap_does_not_bind_studio_unlock(self):
        source = read("app/boards/kimwolf/k380/k380.keymap")
        self.assertNotIn("&studio_unlock", source)

        fn = re.search(r"fn_layer\s*\{(?P<body>.*?)\n\s*\};", source, re.S)
        self.assertIsNotNone(fn)
        refs = binding_refs(fn.group("body"))
        self.assertEqual(80, len(refs))
        self.assertEqual("&bootloader", refs[14])
        self.assertEqual("&trans", refs[28])
        self.assertEqual(["&k380_preset"] * 4, refs[16:20])

    def test_private_protocol_is_not_a_studio_settings_cleanup_hook(self):
        source = read("zmk-keyboard-k380/src/dynamic_protocol.c")

        self.assertNotIn("<zmk/studio/core.h>", source)
        self.assertNotIn("zmk_keymap_reset_settings", source)
        self.assertNotIn("zmk_studio_core_get_lock_state", source)


if __name__ == "__main__":
    unittest.main()
