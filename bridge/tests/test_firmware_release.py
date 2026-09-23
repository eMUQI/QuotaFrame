from __future__ import annotations

import hashlib
import json
import unittest
import os
from unittest.mock import patch

from quotaframe_bridge.sources.firmware_release import (
    FirmwareReleaseError,
    FirmwareReleaseSource,
)


class FirmwareReleaseTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self) -> None:
        env = patch.dict(os.environ, {"QUOTAFRAME_RELEASE_REPO": "example/panel"})
        env.start()
        self.addCleanup(env.stop)
        self.image = b"known firmware bytes"
        self.image_url = (
            "https://github.com/example/panel/releases/download/v0.5.0/"
            "m5sticks3-0.5.0.bin"
        )
        self.manifest_url = (
            "https://github.com/example/panel/releases/download/v0.5.0/manifest.json"
        )

    def manifest(self, **overrides: object) -> bytes:
        item = {
            "target": "m5sticks3",
            "firmware_project": "quotaframe",
            "kind": "app",
            "version": "0.5.0",
            "size": len(self.image),
            "sha256": hashlib.sha256(self.image).hexdigest(),
            "url": self.image_url,
        }
        item.update(overrides)
        return json.dumps({"schema_version": 1, "releases": [item]}).encode()

    async def test_fetch_select_and_download_verify_exact_image(self) -> None:
        fixtures = {
            self.manifest_url: self.manifest(),
            self.image_url: self.image,
        }
        source = FirmwareReleaseSource(
            fetcher=lambda url, _limit: fixtures[url]
        )

        manifest = await source.fetch_manifest(self.manifest_url)
        selected = manifest.for_target("m5sticks3")
        downloaded = await source.download_image(selected)

        self.assertEqual(downloaded, self.image)
        self.assertEqual(selected.version, "0.5.0")

    async def test_rejects_bad_schema_duplicate_target_and_non_github_url(self) -> None:
        cases = [
            {"schema": "2", "releases": []},
            {
                "schema_version": 1,
                "releases": json.loads(self.manifest())["releases"] * 2,
            },
            json.loads(self.manifest(url="http://github.com/example/bad.bin")),
            json.loads(self.manifest(url="https://example.com/bad.bin")),
        ]
        for payload in cases:
            with self.subTest(payload=payload):
                source = FirmwareReleaseSource(
                    fetcher=lambda _url, _limit,
                    value=json.dumps(payload).encode(): value
                )
                with self.assertRaises(FirmwareReleaseError):
                    await source.fetch_manifest(self.manifest_url)

    async def test_rejects_declared_size_digest_and_missing_target(self) -> None:
        fixtures = {self.manifest_url: self.manifest(), self.image_url: self.image}
        source = FirmwareReleaseSource(
            fetcher=lambda url, _limit: fixtures[url]
        )
        manifest = await source.fetch_manifest(self.manifest_url)

        with self.assertRaises(FirmwareReleaseError):
            manifest.for_target("waveshare_amoled_216")

        for override in (
            {"size": len(self.image) + 1},
            {"sha256": "00" * 32},
        ):
            bad_source = FirmwareReleaseSource(
                fetcher=lambda url, _limit, data=self.manifest(**override):
                    data if url == self.manifest_url else self.image
            )
            bad_manifest = await bad_source.fetch_manifest(self.manifest_url)
            with self.assertRaises(FirmwareReleaseError):
                await bad_source.download_image(bad_manifest.for_target("m5sticks3"))

    async def test_rejects_oversized_manifest_image_and_fetch_result(self) -> None:
        oversized_manifest = b"x" * (64 * 1024 + 1)
        source = FirmwareReleaseSource(
            fetcher=lambda _url, _limit: oversized_manifest
        )
        with self.assertRaises(FirmwareReleaseError):
            await source.fetch_manifest(self.manifest_url)

        source = FirmwareReleaseSource(
            fetcher=lambda _url, limit: b"x" * (limit + 1)
        )
        manifest = await FirmwareReleaseSource(
            fetcher=lambda _url, _limit: self.manifest()
        ).fetch_manifest(self.manifest_url)
        with self.assertRaises(FirmwareReleaseError):
            await source.download_image(manifest.for_target("m5sticks3"))

        too_large = json.loads(self.manifest(size=4 * 1024 * 1024 + 1))
        source = FirmwareReleaseSource(
            fetcher=lambda _url, _limit: json.dumps(too_large).encode()
        )
        with self.assertRaises(FirmwareReleaseError):
            await source.fetch_manifest(self.manifest_url)

    async def test_rejects_noncanonical_semver(self) -> None:
        for version in ("01.2.3", "1.02.3", "1.2.03", "1.2.3-alpha..1", "1.2.3-01"):
            with self.subTest(version=version):
                source = FirmwareReleaseSource(
                    fetcher=lambda _url, _limit, value=version:
                    self.manifest(version=value)
                )
                with self.assertRaises(FirmwareReleaseError):
                    await source.fetch_manifest(self.manifest_url)


if __name__ == "__main__":
    unittest.main()
