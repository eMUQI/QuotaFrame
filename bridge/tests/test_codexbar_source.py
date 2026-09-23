from __future__ import annotations

import asyncio
import functools
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from quotaframe_bridge.domain.models import Provider, SourceState
from quotaframe_bridge.sources import codexbar
from quotaframe_bridge.sources.codexbar import (
    WINDOWS_DIALECT,
    SourceDataError,
    WinCodexBarSource,
    parse_codexbar_json,
    resolve_codexbar_cli,
)


FIXTURE_PATH = Path(__file__).parent / "fixtures" / "codexbar_both.json"
FIXTURE = FIXTURE_PATH.read_text(encoding="utf-8")


class CodexBarParserTests(unittest.TestCase):
    def test_allowlists_both_provider_windows_and_rounds_half_up(self) -> None:
        result = parse_codexbar_json(FIXTURE, attempted_at=1_785_398_400)

        self.assertEqual(result[Provider.CODEX].short.used_percent, 36)
        self.assertEqual(result[Provider.CODEX].week.used_percent, 62)
        self.assertEqual(result[Provider.CLAUDE].short.used_percent, 22)
        self.assertEqual(result[Provider.CLAUDE].state, SourceState.OK)
        self.assertEqual(result[Provider.CLAUDE].sampled_at, 1_785_398_390)

    def test_provider_error_does_not_erase_other_provider(self) -> None:
        records = json.loads(FIXTURE)
        records[0] = {"provider": "codex", "error": "login required"}

        result = parse_codexbar_json(json.dumps(records), attempted_at=100)

        self.assertEqual(result[Provider.CODEX].state, SourceState.UNAVAILABLE)
        self.assertIsNone(result[Provider.CODEX].short)
        self.assertEqual(result[Provider.CODEX].sampled_at, 100)
        self.assertEqual(result[Provider.CLAUDE].state, SourceState.OK)

    def test_missing_secondary_is_partial(self) -> None:
        records = json.loads(FIXTURE)
        records[0]["usage"].pop("secondary")

        result = parse_codexbar_json(json.dumps(records), attempted_at=100)

        self.assertEqual(result[Provider.CODEX].state, SourceState.PARTIAL)
        self.assertIsNotNone(result[Provider.CODEX].short)
        self.assertIsNone(result[Provider.CODEX].week)

    def test_missing_provider_becomes_unavailable(self) -> None:
        records = [json.loads(FIXTURE)[0]]

        result = parse_codexbar_json(json.dumps(records), attempted_at=123)

        self.assertEqual(result[Provider.CLAUDE].state, SourceState.UNAVAILABLE)
        self.assertEqual(result[Provider.CLAUDE].sampled_at, 123)

    def test_non_finite_or_out_of_range_percent_is_rejected(self) -> None:
        for invalid in (101, -1, float("nan")):
            with self.subTest(invalid=invalid):
                records = json.loads(FIXTURE)
                records[0]["usage"]["primary"]["used_percent"] = invalid
                with self.assertRaises(SourceDataError):
                    parse_codexbar_json(json.dumps(records), attempted_at=100)

    def test_malformed_top_level_is_rejected(self) -> None:
        with self.assertRaisesRegex(SourceDataError, "array"):
            parse_codexbar_json('{"provider":"codex"}', attempted_at=100)

    def test_identity_and_cost_fields_are_not_in_domain_model(self) -> None:
        result = parse_codexbar_json(FIXTURE, attempted_at=100)

        domain_fields = set(result[Provider.CODEX].__dataclass_fields__)
        self.assertEqual(
            domain_fields,
            {"provider", "state", "sampled_at", "short", "week"},
        )


class ExecutableResolutionTests(unittest.TestCase):
    def _candidate_resolver(self):
        try:
            resolver = codexbar.resolve_codexbar_candidates
        except AttributeError:
            self.fail("resolve_codexbar_candidates is not implemented")
        return functools.partial(resolver, dialect=WINDOWS_DIALECT)

    def test_config_precedes_default_and_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            configured = root / "configured" / "codexbar-cli.exe"
            default = root / "Programs" / "CodexBar" / "codexbar-cli.exe"
            path_cli = root / "bin" / "codexbar-cli.exe"
            for candidate in (configured, default, path_cli):
                candidate.parent.mkdir(parents=True, exist_ok=True)
                candidate.touch()
            config = root / "quotaframe" / "config.toml"
            config.parent.mkdir()
            config.write_text(
                f'codexbar_cli = "{configured.as_posix()}"\n',
                encoding="utf-8",
            )

            candidates = self._candidate_resolver()(
                None,
                environ={"APPDATA": str(root), "LOCALAPPDATA": str(root)},
                which=lambda name: str(path_cli)
                if name == "codexbar-cli.exe"
                else None,
            )

            self.assertEqual(
                candidates,
                tuple(path.resolve() for path in (configured, default, path_cli)),
            )

    def test_explicit_path_is_the_only_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            explicit = Path(directory).resolve() / "custom-codexbar.exe"
            explicit.touch()

            self.assertEqual(
                self._candidate_resolver()(
                    explicit,
                    environ={"LOCALAPPDATA": directory},
                    which=lambda _: "ignored",
                ),
                (explicit.resolve(),),
            )

    def test_invalid_configuration_is_skipped(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            config = root / "quotaframe" / "config.toml"
            config.parent.mkdir()
            config.write_text("codexbar_cli = [", encoding="utf-8")
            path_cli = root / "codexbar-cli.exe"
            path_cli.touch()

            candidates = self._candidate_resolver()(
                None,
                environ={"APPDATA": str(root)},
                which=lambda _: str(path_cli),
            )

            self.assertEqual(candidates, (path_cli.resolve(),))

    def test_path_checks_source_built_names_and_deduplicates(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path_cli = Path(directory).resolve() / "codexbar.exe"
            path_cli.touch()
            names: list[str] = []

            def which(name: str) -> str | None:
                names.append(name)
                return str(path_cli) if name == "codexbar" else None

            candidates = self._candidate_resolver()(
                None,
                environ={},
                which=which,
            )

            self.assertEqual(
                names,
                ["codexbar-cli.exe", "codexbar-cli", "codexbar.exe", "codexbar"],
            )
            self.assertEqual(candidates, (path_cli.resolve(),))

    def test_explicit_path_has_priority(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "custom-codexbar.exe"
            executable.touch()

            resolved = resolve_codexbar_cli(
                executable,
                environ={"LOCALAPPDATA": directory},
                which=lambda _: "ignored.exe",
                dialect=WINDOWS_DIALECT,
            )

            self.assertEqual(resolved, executable.resolve())

    def test_localappdata_install_is_checked_before_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            installed = (
                Path(directory).resolve()
                / "Programs"
                / "CodexBar"
                / "codexbar-cli.exe"
            )
            installed.parent.mkdir(parents=True)
            installed.touch()

            resolved = resolve_codexbar_cli(
                None,
                environ={"LOCALAPPDATA": directory},
                which=lambda _: r"C:\Tools\codexbar-cli.exe",
                dialect=WINDOWS_DIALECT,
            )

        self.assertEqual(resolved, installed.resolve())

    def test_missing_executable_has_sanitized_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(FileNotFoundError, "codexbar-cli"):
                resolve_codexbar_cli(
                    None,
                    environ={"LOCALAPPDATA": directory},
                    which=lambda _: None,
                    dialect=WINDOWS_DIALECT,
                )


class WinCodexBarSourceTests(unittest.IsolatedAsyncioTestCase):
    async def test_explicit_source_does_not_lookup_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "custom-codexbar.exe"
            executable.touch()

            def which(_: str) -> str | None:
                self.fail("explicit executable must not query PATH")

            async def runner(
                argv: tuple[str, ...], timeout: float
            ) -> tuple[int, str, str]:
                if argv[1] == "--version":
                    return 0, "codexbar 0.46.0\n", ""
                return 0, FIXTURE, ""

            source = WinCodexBarSource(
                executable=executable,
                runner=runner,
                which=which,
            )

            result = await source.collect(attempted_at=100)

            self.assertEqual(result[Provider.CODEX].state, SourceState.OK)

    async def test_collect_executes_current_json_interface(self) -> None:
        seen: list[tuple[str, ...]] = []

        async def runner(
            argv: tuple[str, ...], timeout: float
        ) -> tuple[int, str, str]:
            seen.append(argv)
            self.assertEqual(timeout, 7.5)
            if argv[1] == "--version":
                return 0, "codexbar 0.46.0\n", ""
            return 0, FIXTURE, ""

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar-cli.exe"
            executable.touch()
            source = WinCodexBarSource(
                executable=executable,
                timeout=7.5,
                runner=runner,
            )

            result = await source.collect(attempted_at=1_785_398_400)

        self.assertEqual(
            seen,
            [
                (str(executable), "--version"),
                (str(executable), "usage", "-p", "both", "--json"),
            ],
        )
        self.assertEqual(result[Provider.CODEX].state, SourceState.OK)

    async def test_process_failure_does_not_expose_stderr(self) -> None:
        async def runner(
            argv: tuple[str, ...], timeout: float
        ) -> tuple[int, str, str]:
            if argv[1] == "--version":
                return 0, "codexbar 0.46.0\n", ""
            return 2, "", "secret token and account@example.com"

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar-cli.exe"
            executable.touch()
            source = WinCodexBarSource(
                executable=executable,
                runner=runner,
            )

            with self.assertRaises(codexbar.CodexBarResolutionError) as raised:
                await source.collect(attempted_at=100)

        self.assertEqual(raised.exception.category, "capability")
        self.assertNotIn("secret token", str(raised.exception))
        self.assertNotIn("account@example.com", str(raised.exception))


class CodexBarResolverTests(unittest.IsolatedAsyncioTestCase):
    def _resolver_type(self):
        try:
            resolver = codexbar.CodexBarResolver
        except AttributeError:
            self.fail("CodexBarResolver is not implemented")
        return functools.partial(resolver, dialect=WINDOWS_DIALECT)

    def _resolution_error_type(self):
        try:
            return codexbar.CodexBarResolutionError
        except AttributeError:
            self.fail("CodexBarResolutionError is not implemented")

    async def test_rejects_desktop_version_and_falls_back_to_cli(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            desktop = root / "desktop.exe"
            cli = root / "codexbar.exe"
            desktop.touch()
            cli.touch()
            calls: list[tuple[str, ...]] = []

            def which(name: str) -> str | None:
                if name == "codexbar-cli.exe":
                    return str(desktop)
                if name == "codexbar":
                    return str(cli)
                return None

            async def runner(
                argv: tuple[str, ...], timeout: float
            ) -> tuple[int, str, str]:
                calls.append(argv)
                if argv[0] == str(desktop):
                    return 0, "CodexBar desktop 1.2.3\n", "desktop secret"
                if argv[1] == "--version":
                    return 0, "codexbar 0.46.0\n", ""
                return 0, FIXTURE, ""

            resolver = self._resolver_type()(
                None,
                environ={},
                which=which,
                runner=runner,
            )

            result = await resolver.collect(attempted_at=100)

            self.assertEqual(result[Provider.CODEX].state, SourceState.OK)
            self.assertEqual(
                calls,
                [
                    (str(desktop), "--version"),
                    (str(cli), "--version"),
                    (str(cli), "usage", "-p", "both", "--json"),
                ],
            )

    async def test_capability_failure_falls_back_to_next_identity_valid_candidate(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            first = root / "first.exe"
            second = root / "second.exe"
            first.touch()
            second.touch()
            usage_calls = 0

            def which(name: str) -> str | None:
                if name == "codexbar-cli.exe":
                    return str(first)
                if name == "codexbar-cli":
                    return str(second)
                return None

            async def runner(
                argv: tuple[str, ...], timeout: float
            ) -> tuple[int, str, str]:
                nonlocal usage_calls
                if argv[1] == "--version":
                    return 0, "codexbar 0.46.0\n", ""
                usage_calls += 1
                return (0, "{", "") if usage_calls == 1 else (0, FIXTURE, "")

            resolver = self._resolver_type()(
                None,
                environ={},
                which=which,
                runner=runner,
            )

            result = await resolver.collect(attempted_at=100)

            self.assertEqual(result[Provider.CLAUDE].state, SourceState.OK)
            self.assertEqual(usage_calls, 2)

    async def test_successful_candidate_is_cached_after_identity_validation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar.exe"
            executable.touch()
            calls: list[tuple[str, ...]] = []

            async def runner(
                argv: tuple[str, ...], timeout: float
            ) -> tuple[int, str, str]:
                calls.append(argv)
                if argv[1] == "--version":
                    return 0, "codexbar 0.46.0\n", ""
                return 0, FIXTURE, ""

            resolver = self._resolver_type()(executable, runner=runner)

            await resolver.collect(attempted_at=100)
            await resolver.collect(attempted_at=200)

            self.assertEqual(
                calls,
                [
                    (str(executable), "--version"),
                    (str(executable), "usage", "-p", "both", "--json"),
                    (str(executable), "usage", "-p", "both", "--json"),
                ],
            )

    async def test_cached_failure_restarts_discovery_and_selects_next_candidate(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            first = root / "first.exe"
            second = root / "second.exe"
            first.touch()
            second.touch()
            first_usage_calls = 0

            def which(name: str) -> str | None:
                if name == "codexbar-cli.exe":
                    return str(first)
                if name == "codexbar-cli":
                    return str(second)
                return None

            async def runner(
                argv: tuple[str, ...], timeout: float
            ) -> tuple[int, str, str]:
                nonlocal first_usage_calls
                if argv[1] == "--version":
                    return 0, "codexbar 0.46.0\n", ""
                if argv[0] == str(first):
                    first_usage_calls += 1
                    if first_usage_calls > 1:
                        return 2, "", "stale secret"
                return 0, FIXTURE, ""

            resolver = self._resolver_type()(
                None,
                environ={},
                which=which,
                runner=runner,
            )

            await resolver.collect(attempted_at=100)
            result = await resolver.collect(attempted_at=200)

            self.assertEqual(result[Provider.CODEX].state, SourceState.OK)
            self.assertEqual(first_usage_calls, 3)

    async def test_no_candidates_raises_not_found(self) -> None:
        resolver = self._resolver_type()(None, environ={}, which=lambda _: None)

        with self.assertRaises(self._resolution_error_type()) as raised:
            await resolver.collect(attempted_at=100)

        self.assertEqual(raised.exception.category, "not_found")

    async def test_candidates_without_matching_version_raise_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "not-codexbar.exe"
            executable.touch()

            async def runner(
                argv: tuple[str, ...], timeout: float
            ) -> tuple[int, str, str]:
                return 0, "CodexBar desktop 1.2.3\n", ""

            resolver = self._resolver_type()(
                None,
                environ={},
                which=lambda _: str(executable),
                runner=runner,
            )

            with self.assertRaises(self._resolution_error_type()) as raised:
                await resolver.collect(attempted_at=100)

            self.assertEqual(raised.exception.category, "identity")

    async def test_identity_valid_candidates_without_usage_raise_capability(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar.exe"
            executable.touch()

            async def runner(
                argv: tuple[str, ...], timeout: float
            ) -> tuple[int, str, str]:
                if argv[1] == "--version":
                    return 0, "codexbar 0.46.0\n", ""
                return 0, "{", ""

            resolver = self._resolver_type()(
                None,
                environ={},
                which=lambda _: str(executable),
                runner=runner,
            )

            with self.assertRaises(self._resolution_error_type()) as raised:
                await resolver.collect(attempted_at=100)

            self.assertEqual(raised.exception.category, "capability")

    async def test_resolution_error_does_not_expose_process_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar.exe"
            executable.touch()

            async def runner(
                argv: tuple[str, ...], timeout: float
            ) -> tuple[int, str, str]:
                if argv[1] == "--version":
                    return 0, "codexbar 0.46.0\n", ""
                return 2, '{"account":"account@example.com"}', "secret token"

            resolver = self._resolver_type()(
                None,
                environ={},
                which=lambda _: str(executable),
                runner=runner,
            )

            with self.assertRaises(self._resolution_error_type()) as raised:
                await resolver.collect(attempted_at=100)

            message = str(raised.exception)
            self.assertNotIn("secret token", message)
            self.assertNotIn("account@example.com", message)
            self.assertNotIn("account", message)

class ProcessCreationFlagTests(unittest.IsolatedAsyncioTestCase):
    async def test_cancellation_terminates_and_reaps_child_process(self) -> None:
        started = asyncio.Event()
        child = None
        spawn = asyncio.create_subprocess_exec

        async def capture(*args, **kwargs):
            nonlocal child
            child = await spawn(*args, **kwargs)
            started.set()
            return child

        with patch.object(asyncio, "create_subprocess_exec", side_effect=capture):
            task = asyncio.create_task(codexbar._run_process(
                (sys.executable, "-c", "import time; time.sleep(60)"), 90))
            try:
                await asyncio.wait_for(started.wait(), 5)
                task.cancel()
                with self.assertRaises(asyncio.CancelledError):
                    await asyncio.wait_for(task, 5)
                self.assertIsNotNone(child.returncode)
            finally:
                if child is not None and child.returncode is None:
                    child.kill()
                    await child.communicate()
                if not task.done():
                    task.cancel()
                    await asyncio.gather(task, return_exceptions=True)

    async def test_subprocess_is_spawned_without_a_console_window(self) -> None:
        """A windowed build must never flash a console window."""

        import quotaframe_bridge.sources.codexbar as codexbar

        recorded: dict[str, object] = {}

        async def fake_exec(*argv: str, **kwargs: object) -> object:
            recorded.update(kwargs)

            class _Process:
                returncode = 0

                async def communicate(self) -> tuple[bytes, bytes]:
                    return (b"[]", b"")

            return _Process()

        original = asyncio.create_subprocess_exec
        asyncio.create_subprocess_exec = fake_exec  # type: ignore[assignment]
        try:
            await codexbar._run_process(("codexbar-cli.exe", "--version"), 5.0)
        finally:
            asyncio.create_subprocess_exec = original  # type: ignore[assignment]

        expected = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        self.assertEqual(recorded.get("creationflags"), expected)


if __name__ == "__main__":
    unittest.main()
