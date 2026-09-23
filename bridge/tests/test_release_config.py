from __future__ import annotations

import unittest

from quotaframe_bridge.release_config import (
    ReleaseConfigError,
    release_repository,
)


class ReleaseConfigTests(unittest.TestCase):
    def test_environment_override_wins(self) -> None:
        self.assertEqual(
            release_repository(
                {"QUOTAFRAME_RELEASE_REPO": "example/releases"},
                embedded=lambda: b'{"repository":"ignored/repo"}',
            ),
            "example/releases",
        )

    def test_embedded_repository_is_used_without_override(self) -> None:
        self.assertEqual(
            release_repository(
                {},
                embedded=lambda: b'{"repository":"eMUQI/QuotaFrame"}',
            ),
            "eMUQI/QuotaFrame",
        )

    def test_invalid_or_missing_repository_is_rejected(self) -> None:
        payloads = (
            b"{}",
            b'{"repository":"https://github.com/x/y"}',
            b'{"repository":"owner/repo/extra"}',
            b'{"repository":"owner /repo"}',
            b'{"repository":"user:token@owner/repo"}',
            b'{"repository":"../.."}',
            b'{"repository":"./repo"}',
            b'{"repository":"owner/."}',
            b"not json",
        )
        for payload in payloads:
            with self.subTest(payload=payload), self.assertRaises(
                ReleaseConfigError
            ):
                release_repository({}, embedded=lambda payload=payload: payload)

    def test_present_but_blank_environment_override_is_rejected(self) -> None:
        with self.assertRaises(ReleaseConfigError):
            release_repository(
                {"QUOTAFRAME_RELEASE_REPO": ""},
                embedded=lambda: b'{"repository":"ignored/repo"}',
            )


if __name__ == "__main__":
    unittest.main()
