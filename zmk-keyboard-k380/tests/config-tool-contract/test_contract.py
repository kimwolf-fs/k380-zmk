import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
VECTOR_DIR = REPO_ROOT / "zmk-keyboard-k380" / "tests" / "macro-vm-vectors"
CANONICAL_VECTOR = {
    "name": "wait-600000",
    "source": "\u6309\u4e0b A\n\u7b49\u5f85 600000\n\u5f39\u8d77 A\n",
    "code_hex": "01040010c027090002040000",
    "package_crc32": "9c245561",
    "package_hex": (
        "4b564d31010000000c0000006155249c01040010c027090002040000"
    ),
}
CANONICAL_PACKAGE_LENGTH = 28
CANONICAL_PACKAGE_CRC32 = 0x9C245561


def read(path):
    return (REPO_ROOT / path).read_text(encoding="utf-8")


def macro_definition(source, symbol):
    lines = source.splitlines()
    for index, line in enumerate(lines):
        match = re.match(rf"#define\s+{re.escape(symbol)}\s+(?P<value>.*)", line)
        if not match:
            continue
        value = match.group("value")
        while value.rstrip().endswith("\\"):
            value = value.rstrip()[:-1] + " " + lines[index + 1].strip()
            index += 1
        return re.sub(r"\s+", " ", value).strip()
    raise AssertionError(f"missing macro definition: {symbol}")


def integer_macro(source, symbol):
    value = macro_definition(source, symbol)
    match = re.fullmatch(
        r"(?P<value>(?:0[xX][0-9a-fA-F]+|[0-9]+))"
        r"(?:[uU](?:[lL]{1,2})?|[lL]{1,2}[uU]?)?",
        value,
    )
    if not match:
        raise AssertionError(f"{symbol} is not a literal integer: {value}")
    literal = match.group("value")
    return int(literal, 16 if literal.lower().startswith("0x") else 10)


def c_source_without_comments(source):
    pattern = re.compile(
        r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
        re.S,
    )

    def replace(match):
        return "".join("\n" if character == "\n" else " " for character in match.group(0))

    return pattern.sub(replace, source)


def split_c_declarators(source):
    parts = []
    start = 0
    depths = {"[": 0, "{": 0}
    for index, character in enumerate(source):
        if character in depths:
            depths[character] += 1
        elif character == "]":
            depths["["] -= 1
        elif character == "}":
            depths["{"] -= 1
        elif character == "," and not any(depths.values()):
            parts.append(source[start:index])
            start = index + 1
    parts.append(source[start:])
    return parts


def record_storage_declarations(sources):
    declaration = re.compile(
        r"(?P<prefix>(?:(?:static|extern|const|volatile|register|auto|typedef)\s+)*)"
        r"struct\s+k380_dynamic_macro_record\b"
        r"(?P<declarators>[^;()]*)\s*;"
    )
    owners = []
    for path, source in sorted(sources.items()):
        source = c_source_without_comments(source)
        for match in declaration.finditer(source):
            if re.search(r"\b(?:extern|typedef)\b", match.group("prefix")):
                continue
            line = source[: match.start()].count("\n") + 1
            for declarator in split_c_declarators(match.group("declarators")):
                storage = declarator.split("=", 1)[0].strip()
                if "*" in storage:
                    continue
                name = re.match(r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)", storage)
                if name:
                    owners.append((path, line, name.group("name")))
    return owners


def validate_record_owner_inventory(sources):
    owners = record_storage_declarations(sources)
    expected = {
        ("zmk-keyboard-k380/src/dynamic_macro.c", "record"),
        ("zmk-keyboard-k380/src/dynamic_protocol.c", "record"),
    }
    actual = {(path, name) for path, _, name in owners}
    assert len(owners) == 2 and actual == expected, (
        "expected exactly two macro record storage owners "
        f"(runner.record and upload_session.record), found {owners}"
    )
    return ["runner.record", "upload_session.record"]


def workflow_job(source, name):
    match = re.search(
        rf"(?ms)^  {re.escape(name)}:\n(?P<body>.*?)(?=^  [A-Za-z0-9_-]+:\n|\Z)",
        source,
    )
    if not match:
        raise AssertionError(f"workflow is missing job: {name}")
    return match.group("body")


def workflow_named_steps(job_source):
    steps = {}
    for match in re.finditer(
        r"(?ms)^      - name: (?P<name>[^\n]+)\n"
        r"(?P<body>.*?)(?=^      - |\Z)",
        job_source,
    ):
        steps[match.group("name").strip()] = match.group(0)
    return steps


def map_objects(source):
    marker = "Linker script and memory map"
    self_map = source.partition(marker)
    if not self_map[1]:
        raise AssertionError("linker map is missing the memory map section")

    lines = self_map[2].splitlines()
    objects = {}
    for index, line in enumerate(lines):
        match = re.match(
            r"^\s+(?P<section>\.[^\s]+)"
            r"(?:\s+(?P<address>0x[0-9a-fA-F]+)"
            r"\s+(?P<size>0x[0-9a-fA-F]+))?",
            line,
        )
        if not match:
            continue
        address = match.group("address")
        size = match.group("size")
        if address is None and index + 1 < len(lines):
            value = re.match(
                r"^\s+(?P<address>0x[0-9a-fA-F]+)"
                r"\s+(?P<size>0x[0-9a-fA-F]+)",
                lines[index + 1],
            )
            if value:
                address = value.group("address")
                size = value.group("size")
        if address is None or size is None:
            continue
        address_value = int(address, 16)
        size_value = int(size, 16)
        if address_value != 0 and size_value != 0:
            name = match.group("section").rsplit(".", 1)[-1]
            objects.setdefault(name, []).append(
                (address_value, size_value)
            )
    return objects


def map_region(source, name):
    match = re.search(
        rf"(?m)^{re.escape(name)}\s+"
        r"(?P<origin>0x[0-9a-fA-F]+)\s+"
        r"(?P<length>0x[0-9a-fA-F]+)\s+",
        source,
    )
    if not match:
        raise AssertionError(f"linker map is missing the {name} region")
    return int(match.group("origin"), 16), int(match.group("length"), 16)


def map_symbol_value(source, name):
    match = re.search(
        rf"(?m)^\s*(?P<value>0x[0-9a-fA-F]+)\s+{re.escape(name)}\s*=",
        source,
    )
    if not match:
        raise AssertionError(f"linker map is missing {name}")
    return int(match.group("value"), 16)


def validate_resource_map(source):
    flash_origin, flash_length = map_region(source, "FLASH")
    ram_origin, ram_length = map_region(source, "RAM")
    assert (flash_origin, flash_length) == (0x26000, 0xA4000), (
        "Flash partition differs from 0x26000+0xA4000"
    )
    assert (ram_origin, ram_length) == (0x20000000, 0x40000), (
        "RAM partition differs from 0x20000000+0x40000"
    )

    flash_used = map_symbol_value(source, "_flash_used")
    ram_used = map_symbol_value(source, "_image_ram_size")
    assert flash_used <= flash_length, (
        f"Flash usage {flash_used:#x} exceeds partition {flash_length:#x}"
    )
    assert ram_used <= ram_length, (
        f"RAM usage {ram_used:#x} exceeds partition {ram_length:#x}"
    )

    objects = map_objects(source)
    for name in ("upload_session", "runner", "write_snapshot", "shared_config"):
        assert len(objects.get(name, [])) == 1, (
            f"expected exactly one {name} object in the formal firmware map"
        )
    for name in ("baseline_config", "shared_config", "protocol_config"):
        for _, size in objects.get(name, []):
            assert size < 0x7000, (
                f"legacy {name} is approximately the old 0x7384-byte object: "
                f"{size:#x}"
            )
    for name in ("baseline_config", "protocol_config"):
        assert not objects.get(name), f"legacy {name} object is present in the map"

    shared_size = objects["shared_config"][0][1]
    snapshot_size = objects["write_snapshot"][0][1]
    assert shared_size <= 3000, f"shared_config exceeds 3000 bytes: {shared_size}"
    assert snapshot_size <= 3000, (
        f"write_snapshot exceeds 3000 bytes: {snapshot_size}"
    )

    record_size = 48 + 1104
    upload_size = objects["upload_session"][0][1]
    runner_size = objects["runner"][0][1]
    assert record_size <= upload_size < 2 * record_size, (
        f"upload_session must contain one staging record: {upload_size} bytes"
    )
    assert record_size <= runner_size < 2 * record_size, (
        f"runner must contain one running record: {runner_size} bytes"
    )

    return {
        "flash_used": flash_used,
        "ram_used": ram_used,
        "upload_session": upload_size,
        "runner": runner_size,
        "write_snapshot": snapshot_size,
        "shared_config": shared_size,
    }


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
    def test_integer_macro_accepts_decimal_and_hexadecimal_literals(self):
        for literal, expected in (
            ("512", 512),
            ("512U", 512),
            ("0x200", 512),
            ("0x200U", 512),
            ("0X200UL", 512),
        ):
            with self.subTest(literal=literal):
                self.assertEqual(
                    expected,
                    integer_macro(f"#define TEST_VALUE {literal}\n", "TEST_VALUE"),
                )

    def test_macro_vm_vector_is_a_reviewed_literal_and_header_is_current(self):
        fixture = VECTOR_DIR / "macro_vm_v1_vectors.json"
        document = json.loads(fixture.read_text(encoding="utf-8"))

        self.assertEqual("k380-macro-vm-v1", document["format"])
        self.assertEqual([CANONICAL_VECTOR], document["vectors"])
        package = bytes.fromhex(CANONICAL_VECTOR["package_hex"])
        self.assertEqual(CANONICAL_PACKAGE_LENGTH, len(package))
        self.assertEqual(
            CANONICAL_PACKAGE_CRC32,
            int.from_bytes(package[12:16], "little"),
        )
        self.assertEqual(
            bytes.fromhex(CANONICAL_VECTOR["code_hex"]),
            package[16:],
        )

        with tempfile.TemporaryDirectory() as directory:
            generated = Path(directory) / "macro_vm_vectors.h"
            subprocess.run(
                [
                    sys.executable,
                    str(VECTOR_DIR / "generate_macro_vm_header.py"),
                    str(fixture),
                    str(generated),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                (VECTOR_DIR / "macro_vm_vectors.h").read_bytes(),
                generated.read_bytes(),
            )

    def test_macro_vm_resource_limits_and_single_record_owners_are_static_contracts(self):
        protocol_header = read(
            "zmk-keyboard-k380/include/zmk_keyboard_k380/dynamic_protocol.h"
        )
        vm_header = read(
            "zmk-keyboard-k380/include/zmk_keyboard_k380/dynamic_macro_vm_format.h"
        )
        config_header = read(
            "zmk-keyboard-k380/include/zmk_keyboard_k380/dynamic_config.h"
        )
        protocol_source = read("zmk-keyboard-k380/src/dynamic_protocol.c")
        macro_source = read("zmk-keyboard-k380/src/dynamic_macro.c")

        self.assertLessEqual(
            integer_macro(protocol_header, "K380_DYNAMIC_MAX_PAYLOAD"), 512
        )
        self.assertEqual(
            384,
            integer_macro(protocol_header, "K380_DYNAMIC_MACRO_CHUNK_MAX"),
        )
        self.assertEqual(
            1024,
            integer_macro(vm_header, "K380_MACRO_VM_MAX_CODE_BYTES"),
        )
        header_size = integer_macro(vm_header, "K380_MACRO_VM_PACKAGE_HEADER_SIZE")
        function_count = integer_macro(vm_header, "K380_MACRO_VM_MAX_FUNCTIONS")
        function_size = integer_macro(vm_header, "K380_MACRO_VM_FUNCTION_ENTRY_SIZE")
        self.assertEqual(1104, header_size + function_count * function_size + 1024)
        self.assertEqual(
            "(K380_MACRO_VM_PACKAGE_HEADER_SIZE + "
            "K380_MACRO_VM_MAX_FUNCTIONS * K380_MACRO_VM_FUNCTION_ENTRY_SIZE + "
            "K380_MACRO_VM_MAX_CODE_BYTES)",
            macro_definition(vm_header, "K380_MACRO_VM_MAX_PACKAGE_BYTES"),
        )
        self.assertRegex(
            config_header,
            r"_Static_assert\(sizeof\(struct k380_dynamic_config\) <= 3000U,",
        )

        upload = re.search(
            r"struct k380_dynamic_macro_upload_session\s*\{(?P<body>.*?)\n\};",
            protocol_source,
            re.S,
        )
        runner = re.search(
            r"struct k380_dynamic_macro_runner\s*\{(?P<body>.*?)\n\};",
            macro_source,
            re.S,
        )
        self.assertIsNotNone(upload)
        self.assertIsNotNone(runner)
        record = r"\bstruct\s+k380_dynamic_macro_record\s+record\s*;"
        self.assertEqual(1, len(re.findall(record, upload.group("body"))))
        self.assertEqual(1, len(re.findall(record, runner.group("body"))))
        self.assertEqual(
            1,
            len(
                re.findall(
                    r"static\s+struct\s+k380_dynamic_macro_upload_session\s+"
                    r"upload_session\s*;",
                    protocol_source,
                )
            ),
        )
        self.assertEqual(
            1,
            len(
                re.findall(
                    r"static\s+struct\s+k380_dynamic_macro_runner\s+runner\s*;",
                    macro_source,
                )
            ),
        )

    def test_full_production_record_owner_inventory_rejects_extra_storage(self):
        source_root = REPO_ROOT / "zmk-keyboard-k380" / "src"
        sources = {
            path.relative_to(REPO_ROOT).as_posix(): path.read_text(encoding="utf-8")
            for path in source_root.glob("*.c")
        }
        expected = ["runner.record", "upload_session.record"]
        self.assertEqual(expected, validate_record_owner_inventory(sources))

        appended_mutations = {
            "standalone staging record": (
                "zmk-keyboard-k380/src/dynamic_protocol.c",
                "\nstatic struct k380_dynamic_macro_record retry_upload_record;\n",
            ),
            "standalone running record": (
                "zmk-keyboard-k380/src/dynamic_macro.c",
                "\nstatic struct k380_dynamic_macro_record retry_running_record;\n",
            ),
            "record array": (
                "zmk-keyboard-k380/src/dynamic_protocol.c",
                "\nstatic struct k380_dynamic_macro_record retry_records[2];\n",
            ),
        }
        for label, (path, addition) in appended_mutations.items():
            with self.subTest(mutation=label):
                mutated = dict(sources)
                mutated[path] += addition
                with self.assertRaisesRegex(
                    AssertionError, "exactly two macro record storage owners"
                ):
                    validate_record_owner_inventory(mutated)

        field_mutation = dict(sources)
        runner_path = "zmk-keyboard-k380/src/dynamic_macro.c"
        runner_record = "    struct k380_dynamic_macro_record record;\n"
        field_mutation[runner_path] = field_mutation[runner_path].replace(
            runner_record,
            runner_record.rstrip("\n")
            + " struct k380_dynamic_macro_record fallback_record;\n",
            1,
        )
        self.assertNotEqual(sources[runner_path], field_mutation[runner_path])
        with self.assertRaisesRegex(
            AssertionError, "exactly two macro record storage owners"
        ):
            validate_record_owner_inventory(field_mutation)

        pointer_only = dict(sources)
        pointer_only["zmk-keyboard-k380/src/dynamic_macro_store.c"] += (
            "\nstatic struct k380_dynamic_macro_record *retry_record;\n"
        )
        self.assertEqual(expected, validate_record_owner_inventory(pointer_only))

    def test_formal_k380_ci_runs_every_macro_vm_suite(self):
        source = read(".github/workflows/k380-ci.yml")

        for suite in (
            "dynamic-macro-vm-validate",
            "dynamic-macro-store",
            "dynamic-macro-vm",
            "dynamic-protocol",
            "dynamic-macro",
            "dynamic-config",
        ):
            self.assertIn(
                f"west twister -T zmk-keyboard-k380/tests/{suite} -p native_sim",
                source,
            )

    def test_resource_map_gate_accepts_real_layout_and_rejects_regressions(self):
        valid = """Memory Configuration

Name             Origin             Length             Attributes
FLASH            0x0000000000026000 0x00000000000a4000 xr
RAM              0x0000000020000000 0x0000000000040000 xw

Linker script and memory map
 .bss.upload_session
                0x0000000020001398      0x498 app/libapp.a(dynamic_protocol.c.obj)
 .bss.runner    0x0000000020004694      0x4b8 app/libapp.a(dynamic_macro.c.obj)
 .bss.write_snapshot
                0x0000000020008e0e      0xa84 app/libapp.a(dynamic_settings.c.obj)
 .bss.shared_config
                0x0000000020009892      0xa84 app/libapp.a(dynamic_settings.c.obj)
 .noinit.legitimate_cache
                0x000000002000a316      0x480 app/libapp.a(legitimate.c.obj)
                0x00000000000383c0                _flash_used = ((LOADADDR (.last_section) + SIZEOF (.last_section)) - __rom_region_start)
                0x00000000000114fc                _image_ram_size = (_image_ram_end - _image_ram_start)
"""
        validate_resource_map(valid)

        regressions = {
            "exactly one upload_session": valid.replace(
                " .bss.runner", " .bss.upload_session 0x0000000020001830 0x498 app/libapp.a(dynamic_protocol.c.obj)\n .bss.runner"
            ),
            "shared_config exceeds 3000": valid.replace(
                " .bss.shared_config\n                0x0000000020009892      0xa84",
                " .bss.shared_config\n                0x0000000020009892      0xbb9",
            ),
            "legacy shared_config": valid.replace(
                " .bss.shared_config\n                0x0000000020009892      0xa84",
                " .bss.shared_config\n                0x0000000020009892      0x7384",
            ),
            "legacy baseline_config": valid.replace(
                " .bss.shared_config", " .noinit.baseline_config 0x0000000020009000 0x7384 app/libapp.a(dynamic_settings.c.obj)\n .bss.shared_config"
            ),
            "legacy protocol_config": valid.replace(
                " .bss.shared_config", " .k380-retained.protocol_config 0x0000000020009000 0x7384 app/libapp.a(dynamic_protocol.c.obj)\n .bss.shared_config"
            ),
            "Flash usage": valid.replace("0x00000000000383c0", "0x00000000000a4001"),
            "RAM usage": valid.replace("0x00000000000114fc", "0x0000000000040001"),
        }
        for message, resource_map in regressions.items():
            with self.subTest(message=message):
                with self.assertRaisesRegex(AssertionError, message):
                    validate_resource_map(resource_map)

    def test_formal_k380_ci_gates_flashable_artifact_in_named_step_order(self):
        source = read(".github/workflows/k380-ci.yml")
        firmware_job = workflow_job(source, "firmware-build")
        steps = workflow_named_steps(firmware_job)
        ordered_names = list(steps)
        build_name = "Build formal K380 firmware"
        gate_name = "Validate K380 firmware resource contract"
        upload_name = "Upload formal K380 firmware"

        for name in (build_name, gate_name, upload_name):
            self.assertIn(name, steps)
        self.assertLess(ordered_names.index(build_name), ordered_names.index(gate_name))
        self.assertLess(ordered_names.index(gate_name), ordered_names.index(upload_name))
        self.assertIn("west build -s app -d build/k380-firmware", steps[build_name])
        self.assertIn(
            "python3 zmk-keyboard-k380/tests/config-tool-contract/test_contract.py "
            "--resource-map build/k380-firmware/zephyr/zmk.map",
            steps[gate_name],
        )
        self.assertIn("uses: actions/upload-artifact@v7", steps[upload_name])
        self.assertRegex(steps[upload_name], r"(?m)^        if: success\(\)\s*$")
        self.assertNotRegex(steps[upload_name], r"(?m)^        if: always\(\)\s*$")
        self.assertIn("name: k380-zmk-firmware", steps[upload_name])
        self.assertIn("build/k380-firmware/**/zephyr/zmk.hex", steps[upload_name])
        self.assertIn("build/k380-firmware/**/zephyr/zmk.uf2", steps[upload_name])

    def test_dynamic_base_config_has_no_embedded_macro_payloads(self):
        header = read(
            "zmk-keyboard-k380/include/zmk_keyboard_k380/dynamic_config.h"
        )
        preset = re.search(
            r"struct k380_dynamic_preset\s*\{(?P<body>.*?)\n\};",
            header,
            re.S,
        )

        self.assertIsNotNone(preset)
        self.assertNotRegex(preset.group("body"), r"\bmacros\s*\[")
        self.assertIn(
            "_Static_assert(sizeof(struct k380_dynamic_config) <= 3000U",
            header,
        )

        settings_source = read("zmk-keyboard-k380/src/dynamic_settings.c")
        protocol_source = read("zmk-keyboard-k380/src/dynamic_protocol.c")
        self.assertNotRegex(settings_source, r"\bbaseline_config\b")
        self.assertNotRegex(protocol_source, r"\bprotocol_config\b")

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

    def test_formal_k380_ci_covers_compact_settings_and_macro_store(self):
        source = read(".github/workflows/k380-ci.yml")

        self.assertIn(
            "west twister -T zmk-keyboard-k380/tests/dynamic-macro-store",
            source,
        )
        self.assertIn('"write_snapshot = shared_config;" not in update.group(0)', source)
        self.assertNotIn(
            '"baseline_config = shared_config;" not in update.group(0)', source
        )
        self.assertIn('re.search(r"\\bbaseline_config\\b", source)', source)

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
    if len(sys.argv) == 3 and sys.argv[1] == "--resource-map":
        resource_map = Path(sys.argv[2])
        measurements = validate_resource_map(resource_map.read_text(encoding="utf-8"))
        print(
            "K380 resource contract: "
            + ", ".join(
                f"{name}={value:#x}" for name, value in measurements.items()
            )
        )
    else:
        unittest.main()
