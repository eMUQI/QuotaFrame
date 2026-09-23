"""The macOS menu's structure and copy, asserted without a Mac."""

from __future__ import annotations

import unittest

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.targets import TARGETS as MAINTAINED_TARGETS
from quotaframe_bridge.ui.macos.menu_model import (
    AUTOSTART_LABEL,
    REPAIR_LABEL,
    STARTING_LINES,
    MenuAction,
    build_menu,
)


class MenuModelTests(unittest.TestCase):
    def test_first_two_rows_are_disabled_information(self) -> None:
        menu = build_menu(("M5 已连接 · WS 未发现", "Codex 42% · Claude 17%"))

        self.assertEqual(menu[0].label, "M5 已连接 · WS 未发现")
        self.assertEqual(menu[1].label, "Codex 42% · Claude 17%")
        self.assertFalse(menu[0].enabled)
        self.assertFalse(menu[1].enabled)

    def test_missing_snapshot_shows_placeholders_not_blank_rows(self) -> None:
        """Two blank rows read as a broken panel, not a starting one."""

        menu = build_menu(())

        self.assertEqual((menu[0].label, menu[1].label), STARTING_LINES)

    def test_partial_snapshot_keeps_two_rows(self) -> None:
        menu = build_menu(("M5 已连接",))

        self.assertEqual(menu[0].label, "M5 已连接")
        self.assertEqual(menu[1].label, STARTING_LINES[1])

    def test_repair_is_a_dialog_not_an_action(self) -> None:
        """macOS has no unpair API, so the row can only explain."""

        repair = self._find(MenuAction.REPAIR)

        self.assertEqual(repair.label, REPAIR_LABEL)
        self.assertTrue(repair.label.endswith("…"))

    def test_add_device_is_available_after_startup(self) -> None:
        add_device = self._find(MenuAction.ADD_DEVICE)

        self.assertEqual(add_device.label, tr("add_device"))
        self.assertTrue(add_device.enabled)

    def test_repair_has_no_per_device_submenu(self) -> None:
        labels = [item.label for item in build_menu()]

        # Derived from the shared registry so relabelling a target cannot turn
        # these assertions into checks for names nothing would emit anyway.
        for target in MAINTAINED_TARGETS:
            self.assertNotIn(target.label, labels)

    def test_bridge_update_row_keeps_one_shape_for_every_state(self) -> None:
        initial = self._find(MenuAction.BRIDGE_UPDATE)
        checking = next(
            item
            for item in build_menu(
                bridge_update_enabled=False,
                bridge_update_detail=tr("checking"),
            )
            if item.action is MenuAction.BRIDGE_UPDATE
        )
        available = next(
            item
            for item in build_menu(bridge_update_detail=tr("download_version", version='v0.2.0'))
            if item.action is MenuAction.BRIDGE_UPDATE
        )

        self.assertEqual(
            (initial.enabled, initial.label),
            (True, tr("check_bridge_update")),
        )
        self.assertEqual((checking.enabled, checking.label), (False, tr("checking")))
        self.assertEqual(
            (available.enabled, available.label),
            (True, tr("download_version", version='v0.2.0')),
        )

    def test_autostart_uses_the_macos_wording_and_is_a_checkbox(self) -> None:
        item = self._find(MenuAction.TOGGLE_AUTOSTART)

        self.assertEqual(item.label, AUTOSTART_LABEL)
        self.assertTrue(item.is_checkbox)
        self.assertFalse(item.checked)

    def test_autostart_check_reflects_the_stored_value(self) -> None:
        menu = build_menu(autostart_checked=True)

        item = next(i for i in menu if i.action is MenuAction.TOGGLE_AUTOSTART)
        self.assertTrue(item.checked)

    def test_firmware_rows_route_each_device_and_show_progress(self) -> None:
        rows = [item for item in build_menu(firmware_actions=(
            ("addr-m5", "M5", True, "v1.0.0"), ("addr-ws", "WS", False, "42%"),
        )) if item.action is MenuAction.FIRMWARE_UPDATE]
        self.assertEqual([row.device_address for row in rows], ["addr-m5", "addr-ws"])
        self.assertIn("M5", rows[0].label)
        self.assertIn("WS", rows[1].label)
        self.assertEqual([row.enabled for row in rows], [True, False])
        self.assertIn("42%", rows[1].label)

    def test_every_action_appears_exactly_once(self) -> None:
        actions = [item.action for item in build_menu() if item.action is not None]

        self.assertEqual(sorted(a.value for a in actions), sorted(a.value for a in MenuAction if a is not MenuAction.FIRMWARE_UPDATE))

    def test_separators_group_the_rows_as_designed(self) -> None:
        shape = [
            "sep" if item.separator else ("info" if item.action is None else "action")
            for item in build_menu()
        ]

        self.assertEqual(
            shape,
            [
                "info", "info",
                "sep",
                "action", "action", "action",
                "sep",
                "action", "action", "action", "action", "action",
                "sep",
                "action", "action",
                "sep",
                "action",
            ],
        )

    def _find(self, action: MenuAction):
        return next(item for item in build_menu() if item.action is action)


if __name__ == "__main__":
    unittest.main()
