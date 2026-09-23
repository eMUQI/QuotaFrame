"""Verify stable release resolution and firmware integrity before site assembly."""
import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch
from urllib.error import HTTPError

from scripts.build_site import fetch_firmware, TARGETS


class SiteBuildTests(unittest.TestCase):
    def test_pinned_release_and_checksum_failures(self):
        payload = b"verified firmware"
        names = [f"{target.release_stem}-full-v1.2.3.bin"
                 for target in TARGETS if target.web_flash is not None]
        checksum = hashlib.sha256(payload).hexdigest()
        sums = "".join(f"{checksum}  {name}\n" for name in names).encode()
        for mode in ("valid", "corrupt", "missing", "prerelease", "unresolved"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                def download(url):
                    self.assertIn("/download/v1.2.3/", url)
                    if url.endswith("SHA256SUMS.txt"):
                        return sums if mode != "missing" else b""
                    return payload if mode != "corrupt" else b"corrupted"

                release_url = "https://github.com/eMUQI/QuotaFrame/releases/tag/v1.2.3"
                if mode == "prerelease":
                    release_url += "-rc.1"
                elif mode == "unresolved":
                    release_url = "https://github.com/eMUQI/QuotaFrame/releases"
                with (
                    patch("scripts.build_site.urlopen") as resolve,
                    patch("scripts.build_site.download", side_effect=download) as fetch,
                ):
                    resolve.return_value.__enter__.return_value.geturl.return_value = release_url
                    if mode == "valid":
                        self.assertEqual(fetch_firmware(Path(directory)), "1.2.3")
                        self.assertEqual(sorted(p.name for p in Path(directory).iterdir()), sorted(names))
                        self.assertEqual(fetch.call_count, 1 + len(names))
                    else:
                        with self.assertRaises(ValueError):
                            fetch_firmware(Path(directory))
                        self.assertEqual(list(Path(directory).iterdir()), [])
                    resolve.assert_called_once()
                    self.assertEqual(resolve.call_args.args[0].full_url,
                                     "https://github.com/eMUQI/QuotaFrame/releases/latest")
                    if mode in ("prerelease", "unresolved"):
                        fetch.assert_not_called()

    def test_release_resolution_retries_transient_failures(self):
        url = "https://github.com/eMUQI/QuotaFrame/releases/latest"
        for error in (TimeoutError(), HTTPError(url, 429, "limited", {}, None),
                      HTTPError(url, 503, "unavailable", {}, None)):
            with self.subTest(error=error), tempfile.TemporaryDirectory() as directory:
                response = MagicMock()
                response.__enter__.return_value.geturl.return_value = (
                    "https://github.com/eMUQI/QuotaFrame/releases/tag/v1.2.3-rc.1"
                )
                with (
                    patch("scripts.build_site.urlopen", side_effect=[error, response]) as resolve,
                    patch("scripts.build_site.time.sleep") as sleep,
                    patch("scripts.build_site.download") as download,
                ):
                    with self.assertRaisesRegex(ValueError, "published stable"):
                        fetch_firmware(Path(directory))
                    self.assertEqual(resolve.call_count, 2)
                    sleep.assert_called_once_with(1)
                    download.assert_not_called()

    def test_explicit_version_skips_latest_and_verifies_checksums(self):
        payload = b"pinned firmware"
        for requested in ("1.2.3", "v1.2.3", "v1.2.3-rc.1"):
            version = requested.removeprefix("v")
            names = [f"{t.release_stem}-full-v{version}.bin"
                     for t in TARGETS if t.web_flash is not None]
            checksum = hashlib.sha256(payload).hexdigest()
            sums = "".join(f"{checksum}  {name}\n" for name in names).encode()
            for corrupt in (False, True):
                with self.subTest(requested=requested, corrupt=corrupt), tempfile.TemporaryDirectory() as directory:
                    def download(url):
                        self.assertIn(f"/download/v{version}/", url)
                        return sums if url.endswith("SHA256SUMS.txt") else (b"bad" if corrupt else payload)
                    with patch("scripts.build_site.fetch") as latest, patch("scripts.build_site.download", side_effect=download):
                        if corrupt:
                            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                                fetch_firmware(Path(directory), requested)
                        else:
                            self.assertEqual(fetch_firmware(Path(directory), requested), version)
                            self.assertEqual(sorted(p.name for p in Path(directory).iterdir()), sorted(names))
                        latest.assert_not_called()

    def test_invalid_explicit_version_does_not_download(self):
        with tempfile.TemporaryDirectory() as directory, patch("scripts.build_site.download") as download:
            with self.assertRaises(ValueError):
                fetch_firmware(Path(directory), "../../other")
            download.assert_not_called()
