from __future__ import annotations

import re
import unittest
from pathlib import Path

from quotaframe_bridge.targets import TARGETS_BY_ID

ROOT = Path(__file__).resolve().parents[2]
M5_PROJECT = ROOT / TARGETS_BY_ID["m5sticks3"].firmware_project


def parse_defaults(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
            continue
        match = re.fullmatch(r"# (CONFIG_[A-Z0-9_]+) is not set", line)
        if match:
            values[match.group(1)] = "n"
    return values


class FirmwareConfigurationTests(unittest.TestCase):
    def test_target_memory_and_nimble_roles_are_pinned(self) -> None:
        config = parse_defaults(M5_PROJECT / "sdkconfig.defaults")

        expected = {
            "CONFIG_IDF_TARGET": '"esp32s3"',
            "CONFIG_ESPTOOLPY_FLASHSIZE_8MB": "y",
            "CONFIG_SPIRAM": "y",
            "CONFIG_SPIRAM_MODE_OCT": "y",
            "CONFIG_BT_ENABLED": "y",
            "CONFIG_BT_NIMBLE_ENABLED": "y",
            "CONFIG_BT_NIMBLE_ROLE_CENTRAL": "n",
            "CONFIG_BT_NIMBLE_ROLE_OBSERVER": "n",
            "CONFIG_BT_NIMBLE_ROLE_BROADCASTER": "y",
            "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL": "y",
            "CONFIG_BT_NIMBLE_MAX_CONNECTIONS": "1",
            "CONFIG_BT_NIMBLE_NVS_PERSIST": "y",
            "CONFIG_BT_NIMBLE_SM_SC_ONLY": "1",
            "CONFIG_BT_NIMBLE_SM_LVL": "3",
            "CONFIG_ESP_DESKTOP_BUDDY_RX_INBOX_DEPTH": "32",
            "CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_RAW_CHUNK": "4096",
            "CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_DECODED_CHUNK": "2880",
            "CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_TRANSFER_BYTES": "3146240",
            "CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_FLAT_PATHS_ONLY": "y",
        }
        self.assertEqual(
            {key: config.get(key) for key in expected},
            expected,
        )

    def test_runtime_ble_security_policy_is_explicit(self) -> None:
        source = (
            ROOT
            / "firmware"
            / "components"
            / "usage_ble"
            / "src"
            / "ble_service.cpp"
        ).read_text()

        for statement in (
            "transport_config.security.bonding = true;",
            "transport_config.security.mitm = true;",
            "transport_config.security.secure_connections = true;",
            "ESP_DESKTOP_BUDDY_TRANSPORT_BLE_IO_CAP_DISPLAY_ONLY",
        ):
            with self.subTest(statement=statement):
                self.assertIn(statement, source)

    def test_waveshare_display_uses_adapter_lock_semantics(self) -> None:
        source = (
            ROOT
            / "firmware"
            / "targets"
            / "esp32_s3_touch_amoled_216"
            / "main"
            / "display_ui.cpp"
        ).read_text()

        self.assertIn("esp_lv_adapter_lock(-1) == ESP_OK", source)
        self.assertIn("esp_lv_adapter_unlock();", source)
        self.assertNotIn("bsp_display_lock(", source)
        self.assertNotIn("bsp_display_unlock(", source)

    def test_waveshare_v32_presentation_contract_is_structural(self) -> None:
        main = (
            ROOT
            / "firmware"
            / "targets"
            / "esp32_s3_touch_amoled_216"
            / "main"
        )
        header = (main / "display_ui.hpp").read_text()
        source = (main / "display_ui.cpp").read_text()
        page_state = (
            ROOT
            / "firmware"
            / "components"
            / "usage_panel_state"
            / "include"
            / "usage_panel_state"
            / "page_state.hpp"
        ).read_text()

        self.assertIn("const PanelPresentation& presentation", header)
        self.assertIn("bool input_enabled", header)
        self.assertIn("bool take_touch_down()", header)
        self.assertIn("constexpr int BATTERY_GROUP_W = 44;", source)
        self.assertIn("charge_symbol_", header)
        self.assertIn("LV_OPA_TRANSP", source)
        self.assertIn("screensaver_panel_", header)
        self.assertIn("montserrat_bold_120", source)
        self.assertIn("lv_obj_set_size(screensaver_panel_, SCREEN_W, SCREEN_H)", source)
        for label in ('"OVERVIEW"', '"CODEX"', '"CLAUDE"'):
            self.assertIn(label, source)
        self.assertIn(
            "enum class Page : uint8_t { Overview, Codex, Claude };",
            page_state,
        )

    def test_waveshare_gesture_and_page_request_ownership_are_explicit(self) -> None:
        source = (
            ROOT
            / "firmware"
            / "targets"
            / "esp32_s3_touch_amoled_216"
            / "main"
            / "display_ui.cpp"
        ).read_text()

        self.assertGreaterEqual(
            source.count(
                "lv_obj_remove_flag("
                "overview_panel_, LV_OBJ_FLAG_GESTURE_BUBBLE)"
            )
            + source.count(
                "lv_obj_remove_flag("
                "widgets.panel, LV_OBJ_FLAG_GESTURE_BUBBLE)"
            ),
            2,
        )
        render_body = source.split("void DisplayUi::render(", 1)[1].split(
            "void DisplayUi::tab_event(", 1
        )[0]
        self.assertNotIn("requested_page_.store(", render_body)

    def test_waveshare_ota_version_is_width_bounded(self) -> None:
        source = (
            ROOT
            / "firmware"
            / "targets"
            / "esp32_s3_touch_amoled_216"
            / "main"
            / "display_ui.cpp"
        ).read_text()

        self.assertIn(
            "lv_label_set_long_mode(ota_card_value_, LV_LABEL_LONG_DOT);",
            source,
        )

    def test_waveshare_failure_children_are_not_persistent_members(self) -> None:
        header = (
            ROOT
            / "firmware"
            / "targets"
            / "esp32_s3_touch_amoled_216"
            / "main"
            / "display_ui.hpp"
        ).read_text()

        self.assertNotIn("ota_fail_dot_", header)
        self.assertNotIn("ota_fail_label_", header)

    def test_waveshare_memory_probe_is_opt_in(self) -> None:
        target = (
            ROOT / "firmware" / "targets" / "esp32_s3_touch_amoled_216"
        )
        probe_path = target / "sdkconfig_memory_probe.defaults"
        self.assertTrue(probe_path.exists())
        probe_defaults = probe_path.read_text()
        kconfig = (target / "main" / "Kconfig.projbuild").read_text()
        header = (target / "main" / "display_ui.hpp").read_text()
        app_source = (target / "main" / "app_main.cpp").read_text()

        self.assertIn("CONFIG_WS_USAGE_PANEL_MEMORY_PROBE=y", probe_defaults)
        self.assertIn("config WS_USAGE_PANEL_MEMORY_PROBE", kconfig)
        self.assertIn("void run_memory_probe();", header)
        self.assertIn("ui.run_memory_probe();", app_source)

    def test_waveshare_memory_probe_uses_async_refresh_completion(self) -> None:
        source = (
            ROOT
            / "firmware"
            / "targets"
            / "esp32_s3_touch_amoled_216"
            / "main"
            / "display_ui.cpp"
        ).read_text()
        probe_body = source.split(
            "void DisplayUi::run_memory_probe()", 1
        )[1].split("#endif", 1)[0]

        self.assertIn("LV_EVENT_REFR_READY", probe_body)
        self.assertNotIn("lv_refr_now(display_)", probe_body)

    def test_waveshare_memory_domain_defaults_are_pinned(self) -> None:
        target = (
            ROOT / "firmware" / "targets" / "esp32_s3_touch_amoled_216"
        )
        config = parse_defaults(target / "sdkconfig.defaults")
        expected = {
            "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL": "16384",
            "CONFIG_LV_USE_BUILTIN_MALLOC": "y",
            "CONFIG_LV_USE_CLIB_MALLOC": "n",
            "CONFIG_LV_MEM_SIZE_KILOBYTES": "128",
            "CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES": "0",
            "CONFIG_LV_MEM_ADR": "0x0",
            "CONFIG_LV_USE_CLIB_STRING": "y",
            "CONFIG_LV_USE_CLIB_SPRINTF": "y",
            "CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM": "n",
            "CONFIG_LV_USE_BAR": "y",
            "CONFIG_LV_USE_BUTTON": "y",
            "CONFIG_LV_USE_LABEL": "y",
            "CONFIG_LV_USE_FLEX": "y",
            "CONFIG_LV_USE_GRID": "n",
            "CONFIG_LV_BUILD_EXAMPLES": "n",
            "CONFIG_LV_BUILD_DEMOS": "n",
            "CONFIG_ESP_DESKTOP_BUDDY_RX_INBOX_DEPTH": "32",
            "CONFIG_ESP_DESKTOP_BUDDY_TX_QUEUE_DEPTH": "4",
            "CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_RAW_CHUNK": "4096",
            "CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_DECODED_CHUNK": "2880",
            "CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_TRANSFER_BYTES": "4194816",
            "CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_FLAT_PATHS_ONLY": "y",
        }
        self.assertEqual(
            {key: config.get(key) for key in expected},
            expected,
        )

        unused_widgets = (
            "CONFIG_LV_USE_ANIMIMG",
            "CONFIG_LV_USE_ARC",
            "CONFIG_LV_USE_BUTTONMATRIX",
            "CONFIG_LV_USE_CALENDAR",
            "CONFIG_LV_USE_CANVAS",
            "CONFIG_LV_USE_CHART",
            "CONFIG_LV_USE_CHECKBOX",
            "CONFIG_LV_USE_DROPDOWN",
            "CONFIG_LV_USE_IMAGE",
            "CONFIG_LV_USE_IMAGEBUTTON",
            "CONFIG_LV_USE_KEYBOARD",
            "CONFIG_LV_USE_LED",
            "CONFIG_LV_USE_LINE",
            "CONFIG_LV_USE_LIST",
            "CONFIG_LV_USE_MENU",
            "CONFIG_LV_USE_MSGBOX",
            "CONFIG_LV_USE_ROLLER",
            "CONFIG_LV_USE_SCALE",
            "CONFIG_LV_USE_SLIDER",
            "CONFIG_LV_USE_SPAN",
            "CONFIG_LV_USE_SPINBOX",
            "CONFIG_LV_USE_SPINNER",
            "CONFIG_LV_USE_SWITCH",
            "CONFIG_LV_USE_TABLE",
            "CONFIG_LV_USE_TABVIEW",
            "CONFIG_LV_USE_TEXTAREA",
            "CONFIG_LV_USE_TILEVIEW",
            "CONFIG_LV_USE_WIN",
        )
        self.assertEqual(
            {key: config.get(key) for key in unused_widgets},
            {key: "n" for key in unused_widgets},
        )

    def test_waveshare_lvgl_pool_is_external_only(self) -> None:
        target = (
            ROOT / "firmware" / "targets" / "esp32_s3_touch_amoled_216"
        )
        header_path = target / "main" / "lvgl_psram_pool.h"
        source_path = target / "main" / "lvgl_psram_pool.c"
        self.assertTrue(header_path.exists())
        self.assertTrue(source_path.exists())
        header = header_path.read_text()
        source = source_path.read_text()
        cmake = (target / "main" / "CMakeLists.txt").read_text()

        signature = "void *lvgl_psram_pool_alloc(size_t bytes)"
        self.assertIn(signature, header)
        self.assertIn(signature, source)
        self.assertIn("bytes != configured_bytes", source)
        self.assertIn(
            "heap_caps_malloc(\n        bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)",
            source,
        )
        self.assertIn("esp_ptr_external_ram(pool)", source)
        self.assertIn("abort();", source)
        self.assertNotIn("malloc(bytes)", source)
        self.assertIn('LV_MEM_POOL_INCLUDE=\\"lvgl_psram_pool.h\\"', cmake)
        self.assertIn("LV_MEM_POOL_ALLOC=lvgl_psram_pool_alloc", cmake)
        self.assertIn(
            'target_sources(${lvgl_lib} PRIVATE "lvgl_psram_pool.c")',
            cmake,
        )

    def test_waveshare_startup_reports_memory_domains(self) -> None:
        target = (
            ROOT / "firmware" / "targets" / "esp32_s3_touch_amoled_216"
        )
        app_main = (target / "main" / "app_main.cpp").read_text()
        cmake = (target / "main" / "CMakeLists.txt").read_text()
        header_path = target / "main" / "memory_telemetry.hpp"
        source_path = target / "main" / "memory_telemetry.cpp"

        self.assertTrue(header_path.exists())
        self.assertTrue(source_path.exists())
        source = source_path.read_text()
        header = header_path.read_text()
        self.assertIn(
            "void log_memory_checkpoint(const char* stage, bool lvgl_ready)",
            header,
        )
        self.assertIn("bool verify_lvgl_allocations_external()", header)
        self.assertIn('log_memory_checkpoint("before_lvgl", false)', app_main)
        self.assertIn('log_memory_checkpoint("after_ui", true)', app_main)
        self.assertIn('log_memory_checkpoint("after_ble", true)', app_main)
        self.assertIn(
            'log_memory_checkpoint("connected_60s", true)', app_main
        )
        self.assertIn("CONNECTED_MEMORY_DELAY_MS", app_main)
        self.assertIn("verify_lvgl_allocations_external()", app_main)
        self.assertIn("lv_malloc(64)", source)
        self.assertIn("esp_ptr_external_ram(probe)", source)
        self.assertIn("lv_free(probe)", source)
        self.assertIn("lv_mem_monitor(&monitor)", source)
        for field in (
            "total_size",
            "free_size",
            "free_biggest_size",
            "max_used",
            "used_pct",
            "frag_pct",
        ):
            self.assertIn(f"monitor.{field}", source)
        self.assertNotIn("address=%p", source)
        self.assertIn('"memory_telemetry.cpp"', cmake)


if __name__ == "__main__":
    unittest.main()
