from __future__ import annotations

import json
import unittest
from unittest.mock import patch

from quotaframe_bridge.sources.product_release import (
    MAX_RELEASE_LIST_BYTES,
    ProductReleaseError,
    ProductReleaseSource,
)
from quotaframe_bridge.versioning import SemVer


REPOSITORY = "eMUQI/QuotaFrame"
API_URL = f"https://api.github.com/repos/{REPOSITORY}/releases?per_page=20"


def release_document(
    tag: str,
    *,
    draft: bool = False,
    prerelease: bool | None = None,
    page_url: str | None = None,
    assets: list[dict[str, object]] | None = None,
) -> dict[str, object]:
    version = SemVer.from_tag(tag)
    expected_page = f"https://github.com/{REPOSITORY}/releases/tag/{tag}"
    expected_manifest = (
        f"https://github.com/{REPOSITORY}/releases/download/{tag}/manifest.json"
    )
    return {
        "draft": draft,
        "prerelease": version.is_prerelease if prerelease is None else prerelease,
        "tag_name": tag,
        "html_url": expected_page if page_url is None else page_url,
        "assets": (
            [
                {
                    "name": "manifest.json",
                    "browser_download_url": expected_manifest,
                }
            ]
            if assets is None
            else assets
        ),
    }


class ProductReleaseSourceTests(unittest.IsolatedAsyncioTestCase):
    async def test_windows_assets_are_optional_and_validated(self):
        tag = "v0.5.0"
        for kind, suffix in (("installer", "-setup.exe"), ("portable", ".exe")):
            name = f"quotaframe-bridge-windows-v0.5.0{suffix}"
            url = f"https://github.com/{REPOSITORY}/releases/download/{tag}/{name}"
            for variant in ("valid", "missing", "wrong_host", "wrong_tag", "wrong_asset", "duplicate"):
                with self.subTest(kind=kind, variant=variant):
                    entry = release_document(tag)
                    asset = {"name": name, "browser_download_url": url}
                    if variant == "wrong_host":
                        asset["browser_download_url"] = "https://example.com/setup.exe"
                    elif variant == "wrong_tag":
                        asset["browser_download_url"] = url.replace("/v0.5.0/", "/v0.4.0/")
                    elif variant == "wrong_asset":
                        asset["browser_download_url"] = url + ".zip"
                    if variant != "missing":
                        entry["assets"].append(asset)
                    if variant == "duplicate":
                        entry["assets"].append(asset)
                    source = ProductReleaseSource(
                        REPOSITORY, fetcher=lambda *_: json.dumps([entry]).encode()
                    )
                    if variant not in ("valid", "missing"):
                        with self.assertRaises(ProductReleaseError):
                            await source.latest(SemVer.parse("0.4.0"))
                    else:
                        release = await source.latest(SemVer.parse("0.4.0"))
                        self.assertEqual(getattr(release, f"windows_{kind}_url"),
                                         None if variant == "missing" else url)

    async def test_unordered_results_ignore_drafts_and_choose_latest(self) -> None:
        document = [
            release_document("v0.0.1-alpha.2"),
            release_document("v0.0.1-alpha.4", draft=True),
            release_document("v0.0.1-alpha.3"),
        ]
        calls: list[tuple[str, int]] = []

        def fetcher(url: str, limit: int) -> bytes:
            calls.append((url, limit))
            return json.dumps(document).encode()

        release = await ProductReleaseSource(REPOSITORY, fetcher=fetcher).latest(
            SemVer.parse("0.0.1-alpha.1")
        )

        self.assertIsNotNone(release)
        assert release is not None
        self.assertEqual(str(release.version), "0.0.1-alpha.3")
        self.assertEqual(release.tag_name, "v0.0.1-alpha.3")
        self.assertEqual(
            release.page_url,
            "https://github.com/eMUQI/QuotaFrame/releases/tag/v0.0.1-alpha.3",
        )
        self.assertEqual(
            release.manifest_url,
            "https://github.com/eMUQI/QuotaFrame/releases/download/"
            "v0.0.1-alpha.3/manifest.json",
        )
        self.assertEqual(calls, [(API_URL, MAX_RELEASE_LIST_BYTES)])

    async def test_stable_channel_rejects_newer_prereleases(self) -> None:
        document = [release_document("v1.1.0-alpha.1")]
        release = await ProductReleaseSource(
            REPOSITORY,
            fetcher=lambda _url, _limit: json.dumps(document).encode(),
        ).latest(SemVer.parse("1.0.0"))
        self.assertIsNone(release)

    async def test_current_release_remains_available_for_firmware_selection(self) -> None:
        document = [release_document("v1.0.0")]

        release = await ProductReleaseSource(
            REPOSITORY,
            fetcher=lambda _url, _limit: json.dumps(document).encode(),
        ).latest(SemVer.parse("1.0.0"))

        self.assertIsNotNone(release)
        assert release is not None
        self.assertEqual(release.tag_name, "v1.0.0")

    async def test_prerelease_channel_accepts_prereleases_and_stable_releases(self) -> None:
        for candidate in ("v1.0.0-beta.2", "v1.0.0"):
            with self.subTest(candidate=candidate):
                document = [release_document(candidate)]
                release = await ProductReleaseSource(
                    REPOSITORY,
                    fetcher=lambda _url, _limit, document=document: json.dumps(
                        document
                    ).encode(),
                ).latest(SemVer.parse("1.0.0-beta.1"))
                self.assertIsNotNone(release)
                assert release is not None
                self.assertEqual(release.tag_name, candidate)

    async def test_tag_and_prerelease_flag_must_agree(self) -> None:
        for document in (
            [release_document("v1.0.0", prerelease=True)],
            [release_document("v1.0.0-rc.1", prerelease=False)],
        ):
            with self.subTest(document=document), self.assertRaises(
                ProductReleaseError
            ):
                await ProductReleaseSource(
                    REPOSITORY,
                    fetcher=lambda _url, _limit, document=document: json.dumps(
                        document
                    ).encode(),
                ).latest(SemVer.parse("0.9.0"))

    async def test_manifest_asset_is_required_exactly_once(self) -> None:
        tag = "v1.0.0"
        manifest = {
            "name": "manifest.json",
            "browser_download_url": (
                f"https://github.com/{REPOSITORY}/releases/download/{tag}/manifest.json"
            ),
        }
        for assets in ([], [manifest, manifest]):
            document = [release_document(tag, assets=assets)]
            with self.subTest(assets=assets), self.assertRaises(ProductReleaseError):
                await ProductReleaseSource(
                    REPOSITORY,
                    fetcher=lambda _url, _limit, document=document: json.dumps(
                        document
                    ).encode(),
                ).latest(SemVer.parse("0.9.0"))

    async def test_release_and_manifest_urls_must_match_repository_and_tag(self) -> None:
        tag = "v1.0.0"
        wrong_asset = [
            {
                "name": "manifest.json",
                "browser_download_url": (
                    "https://github.com/other/repo/releases/download/"
                    f"{tag}/manifest.json"
                ),
            }
        ]
        documents = (
            [
                release_document(
                    tag,
                    page_url=f"https://github.com/other/repo/releases/tag/{tag}",
                )
            ],
            [release_document(tag, assets=wrong_asset)],
        )
        for document in documents:
            with self.subTest(document=document), self.assertRaises(
                ProductReleaseError
            ):
                await ProductReleaseSource(
                    REPOSITORY,
                    fetcher=lambda _url, _limit, document=document: json.dumps(
                        document
                    ).encode(),
                ).latest(SemVer.parse("0.9.0"))

    async def test_invalid_payloads_and_network_failures_are_sanitized(self) -> None:
        failures = (
            lambda _url, _limit: b"not json",
            lambda _url, limit: b"x" * (limit + 1),
            lambda _url, _limit: (_ for _ in ()).throw(OSError("secret host")),
        )
        for fetcher in failures:
            with self.subTest(fetcher=fetcher), self.assertRaisesRegex(
                ProductReleaseError, "release"
            ) as raised:
                await ProductReleaseSource(REPOSITORY, fetcher=fetcher).latest(
                    SemVer.parse("1.0.0")
                )
            self.assertNotIn("secret host", str(raised.exception))

    async def test_default_fetcher_sets_timeout_user_agent_and_read_bound(self) -> None:
        payload = json.dumps([release_document("v1.0.1")]).encode()
        captured: dict[str, object] = {}

        class Response:
            headers: dict[str, str] = {}

            def __enter__(self):
                return self

            def __exit__(self, *_args: object) -> None:
                return None

            def read(self, size: int) -> bytes:
                captured["read_size"] = size
                return payload

        def urlopen(request, *, timeout: int):
            captured["url"] = request.full_url
            captured["user_agent"] = request.get_header("User-agent")
            captured["timeout"] = timeout
            return Response()

        with patch(
            "quotaframe_bridge.sources.product_release.urllib.request.urlopen",
            side_effect=urlopen,
        ):
            release = await ProductReleaseSource(REPOSITORY).latest(
                SemVer.parse("1.0.0")
            )

        self.assertIsNotNone(release)
        self.assertEqual(captured["url"], API_URL)
        self.assertEqual(captured["user_agent"], "quotaframe-bridge")
        self.assertEqual(captured["timeout"], 10)
        self.assertEqual(captured["read_size"], MAX_RELEASE_LIST_BYTES + 1)


if __name__ == "__main__":
    unittest.main()
