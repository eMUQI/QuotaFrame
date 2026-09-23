from __future__ import annotations

import hashlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from quotaframe_bridge.sources import codexbar_dependency as dependency
from quotaframe_bridge.sources.codexbar import CodexBarResolutionError, WinCodexBarSource


class DependencyTests(unittest.TestCase):
    def test_download_verifies_before_replacing_and_cleans_partial_files(self):
        payload = b"verified CLI bytes"
        for downloaded, accepted in ((payload, True), (b"x" * len(payload), False),
                                     (payload[:-1], False), (payload + b"overflow", False)):
            with self.subTest(downloaded=downloaded), tempfile.TemporaryDirectory() as directory:
                env = {"LOCALAPPDATA": directory}
                target = dependency.cli_path(env)
                target.parent.mkdir(parents=True)
                target.write_bytes(b"existing damaged file")
                response = io.BytesIO(downloaded)
                response.headers = {}
                response.geturl = lambda: dependency.URL
                opener = Mock()
                opener.open.return_value = response
                with (
                    patch.object(dependency, "SIZE", len(payload)),
                    patch.object(dependency, "SHA256", hashlib.sha256(payload).hexdigest()),
                    patch.object(dependency.urllib.request, "build_opener", return_value=opener),
                ):
                    if accepted:
                        self.assertEqual(dependency.install_cli(env), target)
                        self.assertEqual(dependency.install_cli(env), target)
                        self.assertEqual(target.read_bytes(), payload)
                    else:
                        with self.assertRaises(dependency.DependencyDownloadError):
                            dependency.install_cli(env)
                        self.assertEqual(target.read_bytes(), b"existing damaged file")
                    self.assertEqual(opener.open.call_count, 1)
                self.assertEqual(list(target.parent.glob("*.part")), [])

    def test_network_error_is_sanitized_and_https_downgrade_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(dependency.urllib.request, "build_opener", side_effect=OSError("secret")):
                with self.assertRaises(dependency.DependencyDownloadError) as caught:
                    dependency.install_cli({"LOCALAPPDATA": directory})
            self.assertNotIn("secret", str(caught.exception))
        with self.assertRaises(dependency.DependencyDownloadError):
            dependency._HttpsRedirect().redirect_request(None, None, 302, "", {}, "http://example.com/cli.exe")


class DependencySourceTests(unittest.IsolatedAsyncioTestCase):
    async def test_missing_cli_downloads_once_and_is_then_collected(self):
        payload = b"test cli"
        with tempfile.TemporaryDirectory() as directory:
            env = {"LOCALAPPDATA": directory}
            target = dependency.cli_path(env)
            states = []
            async def runner(argv, timeout):
                return (0, "codexbar 1.2.12", "") if argv[1] == "--version" else (0, "[]", "")
            def install(_env):
                target.parent.mkdir(parents=True)
                target.write_bytes(payload)
                return target
            with (
                patch.object(dependency, "SIZE", len(payload)),
                patch.object(dependency, "SHA256", hashlib.sha256(payload).hexdigest()),
                patch("quotaframe_bridge.sources.codexbar.install_cli", side_effect=install) as download,
            ):
                source = WinCodexBarSource(environ=env, which=lambda _: None,
                                          runner=runner, dependency_status=states.append)
                await source.collect(100)
                await source.collect(200)
                download.assert_called_once_with(env)
                self.assertEqual(states, ["downloading", "ready"])

    async def test_explicit_configuration_and_usage_failures_never_download(self):
        for mode in ("explicit", "configured", "capability"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                env = {"LOCALAPPDATA": directory, "APPDATA": directory}
                if mode == "configured":
                    config = root / "quotaframe" / "config.toml"
                    config.parent.mkdir()
                    config.write_text("codexbar_cli = 'missing.exe'", encoding="utf-8")
                executable = root / "cli.exe"
                if mode == "capability":
                    executable.touch()
                async def runner(argv, timeout):
                    return (0, "codexbar 1.2.12", "") if argv[1] == "--version" else (1, "", "secret")
                with patch("quotaframe_bridge.sources.codexbar.install_cli") as download:
                    source = WinCodexBarSource(
                        executable if mode == "explicit" else None,
                        environ=env, runner=runner,
                        which=lambda _: str(executable) if mode == "capability" else None,
                    )
                    with self.assertRaises(CodexBarResolutionError):
                        await source.collect(100)
                    download.assert_not_called()

    async def test_download_failure_has_bounded_retries(self):
        with tempfile.TemporaryDirectory() as directory:
            states = []
            source = WinCodexBarSource(environ={"LOCALAPPDATA": directory},
                                      which=lambda _: None, dependency_status=states.append)
            with patch("quotaframe_bridge.sources.codexbar.install_cli",
                       side_effect=dependency.DependencyDownloadError("failed")) as download:
                with self.assertRaises(dependency.DependencyDownloadError):
                    await source.collect(100)
                with self.assertRaises(CodexBarResolutionError):
                    await source.collect(200)
                download.assert_called_once()
                self.assertEqual(states, ["downloading", "failed"])


if __name__ == "__main__":
    unittest.main()
