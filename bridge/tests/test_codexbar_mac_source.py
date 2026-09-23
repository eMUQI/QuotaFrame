"""macOS CodexBar adapter tests.

The upstream CLI is not installed on every development host, so these tests
drive the adapter through injected runners and a fixture whose shape was taken
from upstream's `ProviderPayload`/`UsageSnapshot`/`RateWindow` sources rather
than from the (simplified) sample in `docs/cli.md`.
"""

from __future__ import annotations

import dataclasses
import json
import tempfile
import unittest
from pathlib import Path

from quotaframe_bridge.domain.models import Provider, SourceState
from quotaframe_bridge.sources.codexbar import (
    WINDOWS_DIALECT,
    CliDialect,
    CodexBarResolutionError,
    CodexBarResolver,
    SourceDataError,
    resolve_codexbar_candidates,
)
from quotaframe_bridge.sources.codexbar_mac import (
    MACOS_DIALECT,
    MACOS_USAGE_TIMEOUT,
    MacCodexBarSource,
    installed_paths,
    parse_codexbar_mac_json,
)

FIXTURE_PATH = Path(__file__).parent / "fixtures" / "codexbar_mac_both.json"
FIXTURE = FIXTURE_PATH.read_text(encoding="utf-8")

USAGE_ARGV = ("usage", "--provider", "both", "--format", "json")
VERSION_STDOUT = "CodexBar 0.17.0\n"


def hermetic_dialect(*installed: Path) -> CliDialect:
    """The macOS dialect with its absolute system paths replaced.

    `installed_paths` points at `/opt/homebrew` and `/Applications`, so any
    test that walks discovery would otherwise pass or fail depending on
    whether the developer happens to have CodexBar installed.
    """

    return dataclasses.replace(
        MACOS_DIALECT,
        installed_paths=lambda environ: installed,
    )


def usage_runner(stdout: str, *, return_code: int = 0):
    """Answer the identity probe, then hand `stdout` to the usage command."""

    async def runner(argv: tuple[str, ...], timeout: float) -> tuple[int, str, str]:
        if argv[1] == "--version":
            return 0, VERSION_STDOUT, ""
        return return_code, stdout, ""

    return runner


class MacParserTests(unittest.TestCase):
    def test_allowlists_both_provider_windows_and_rounds_half_up(self) -> None:
        result = parse_codexbar_mac_json(FIXTURE, attempted_at=1_785_398_400)

        self.assertEqual(result[Provider.CODEX].short.used_percent, 36)
        self.assertEqual(result[Provider.CODEX].week.used_percent, 62)
        self.assertEqual(result[Provider.CLAUDE].short.used_percent, 22)
        self.assertEqual(result[Provider.CLAUDE].week.used_percent, 48)
        self.assertEqual(result[Provider.CODEX].state, SourceState.OK)
        self.assertEqual(result[Provider.CLAUDE].state, SourceState.OK)

    def test_retains_nothing_beyond_the_protocol_allowlist(self) -> None:
        result = parse_codexbar_mac_json(FIXTURE, attempted_at=100)

        self.assertEqual(
            set(result[Provider.CODEX].__dataclass_fields__),
            {"provider", "state", "sampled_at", "short", "week"},
        )

    def test_sample_time_comes_from_the_usage_document(self) -> None:
        result = parse_codexbar_mac_json(FIXTURE, attempted_at=100)

        # 2026-07-30T07:59:50Z
        self.assertEqual(result[Provider.CODEX].sampled_at, 1_785_398_390)

    def test_single_object_document_is_accepted(self) -> None:
        record = json.loads(FIXTURE)[0]

        result = parse_codexbar_mac_json(json.dumps(record), attempted_at=100)

        self.assertEqual(result[Provider.CODEX].state, SourceState.OK)
        self.assertEqual(result[Provider.CLAUDE].state, SourceState.UNAVAILABLE)

    def test_synthetic_placeholder_window_is_not_a_zero_percent_lane(self) -> None:
        """Claude web reports a phantom 0% session when no session exists."""

        records = json.loads(FIXTURE)
        records[1]["usage"]["primary"] = {
            "usedPercent": 0,
            "windowMinutes": 300,
            "resetsAt": "2026-07-30T10:00:00Z",
            "isSyntheticPlaceholder": True,
        }

        result = parse_codexbar_mac_json(json.dumps(records), attempted_at=100)

        self.assertIsNone(result[Provider.CLAUDE].short)
        self.assertEqual(result[Provider.CLAUDE].state, SourceState.PARTIAL)

    def test_over_quota_percent_is_clamped_rather_than_rejected(self) -> None:
        """Upstream deliberately reports raw over-quota values."""

        records = json.loads(FIXTURE)
        records[0]["usage"]["primary"]["usedPercent"] = 137.2

        result = parse_codexbar_mac_json(json.dumps(records), attempted_at=100)

        self.assertEqual(result[Provider.CODEX].short.used_percent, 100)

    def test_missing_window_downgrades_to_partial(self) -> None:
        records = json.loads(FIXTURE)
        records[0]["usage"]["secondary"] = None

        result = parse_codexbar_mac_json(json.dumps(records), attempted_at=100)

        self.assertEqual(result[Provider.CODEX].state, SourceState.PARTIAL)
        self.assertIsNone(result[Provider.CODEX].week)

    def test_window_without_reset_keeps_its_percentage(self) -> None:
        records = json.loads(FIXTURE)
        del records[0]["usage"]["primary"]["resetsAt"]

        result = parse_codexbar_mac_json(json.dumps(records), attempted_at=100)

        self.assertEqual(result[Provider.CODEX].short.used_percent, 36)
        self.assertIsNone(result[Provider.CODEX].short.reset_at)

    def test_provider_error_object_marks_that_provider_unavailable(self) -> None:
        records = json.loads(FIXTURE)
        records[1] = {
            "provider": "claude",
            "source": "web",
            "error": {"message": "cookie import failed for user@example.com"},
        }

        result = parse_codexbar_mac_json(json.dumps(records), attempted_at=100)

        self.assertEqual(result[Provider.CODEX].state, SourceState.OK)
        self.assertEqual(result[Provider.CLAUDE].state, SourceState.UNAVAILABLE)
        self.assertEqual(result[Provider.CLAUDE].sampled_at, 100)

    def test_absent_provider_is_unavailable(self) -> None:
        records = [json.loads(FIXTURE)[0]]

        result = parse_codexbar_mac_json(json.dumps(records), attempted_at=100)

        self.assertEqual(result[Provider.CLAUDE].state, SourceState.UNAVAILABLE)

    def test_unknown_providers_are_ignored(self) -> None:
        records = json.loads(FIXTURE)
        records.append({"provider": "cursor", "usage": {"updatedAt": "nonsense"}})

        result = parse_codexbar_mac_json(json.dumps(records), attempted_at=100)

        self.assertEqual(result[Provider.CODEX].state, SourceState.OK)
        self.assertEqual(set(result), set(Provider))

    def test_duplicate_provider_is_rejected(self) -> None:
        records = json.loads(FIXTURE)
        records.append(json.loads(FIXTURE)[0])

        with self.assertRaises(SourceDataError):
            parse_codexbar_mac_json(json.dumps(records), attempted_at=100)

    def test_snake_case_windows_payload_is_not_silently_accepted(self) -> None:
        """The Windows contract must not parse as if it were the mac one."""

        windows_fixture = (
            Path(__file__).parent / "fixtures" / "codexbar_both.json"
        ).read_text(encoding="utf-8")

        with self.assertRaises(SourceDataError):
            parse_codexbar_mac_json(windows_fixture, attempted_at=100)

    def test_observed_real_document_shape(self) -> None:
        """Reduced from real CodexBar 0.48.0 output on 2026-08-07.

        Three things this pins that the fixture does not: `primary` really can
        be null on a live account with only a weekly lane; a window that is not
        a placeholder omits `isSyntheticPlaceholder` entirely rather than
        setting it false; and a failed provider carries only
        `provider`/`source`/`error`, with the error object holding
        `code`/`kind`/`message`.
        """

        document = json.dumps(
            [
                {
                    "provider": "codex",
                    "source": "oauth",
                    "usage": {
                        "primary": None,
                        "secondary": {
                            "usedPercent": 80,
                            "windowMinutes": 10080,
                            "resetsAt": "2026-08-13T14:57:57Z",
                            "resetDescription": "in 5 days",
                        },
                        "tertiary": None,
                        "updatedAt": "2026-08-07T14:41:10Z",
                    },
                },
                {
                    "provider": "claude",
                    "source": "web",
                    "error": {
                        "code": 2,
                        "kind": "provider",
                        "message": "no session cookie for user@example.com",
                    },
                },
            ]
        )

        result = parse_codexbar_mac_json(document, attempted_at=100)

        self.assertEqual(result[Provider.CODEX].state, SourceState.PARTIAL)
        self.assertIsNone(result[Provider.CODEX].short)
        self.assertEqual(result[Provider.CODEX].week.used_percent, 80)
        self.assertEqual(result[Provider.CLAUDE].state, SourceState.UNAVAILABLE)

    def test_malformed_documents_are_rejected(self) -> None:
        for text in ("{", "null", '"text"', "[1]", '[{"provider":"codex","usage":5}]'):
            with self.subTest(text=text), self.assertRaises(SourceDataError):
                parse_codexbar_mac_json(text, attempted_at=100)

    def test_non_finite_numbers_are_rejected(self) -> None:
        with self.assertRaises(SourceDataError):
            parse_codexbar_mac_json(
                '[{"provider":"codex","usage":{"primary":'
                '{"usedPercent":NaN},"updatedAt":"2026-07-30T07:59:50Z"}}]',
                attempted_at=100,
            )


class MacDiscoveryTests(unittest.TestCase):
    def test_documented_install_locations_are_probed_in_order(self) -> None:
        paths = installed_paths({"HOME": "/Users/panel"})

        self.assertEqual(
            [path.as_posix() for path in paths],
            [
                "/opt/homebrew/bin/codexbar",
                "/usr/local/bin/codexbar",
                "/Applications/CodexBar.app/Contents/Helpers/CodexBarCLI",
                "/Users/panel/Applications/CodexBar.app/Contents/Helpers/CodexBarCLI",
            ],
        )

    def test_configured_path_precedes_the_bundled_helper(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory).resolve()
            configured = home / "tools" / "codexbar"
            configured.parent.mkdir(parents=True)
            configured.touch()
            helper = (
                home
                / "Applications"
                / "CodexBar.app"
                / "Contents"
                / "Helpers"
                / "CodexBarCLI"
            )
            helper.parent.mkdir(parents=True)
            helper.touch()
            config = home / "Library" / "Application Support" / "quotaframe"
            config.mkdir(parents=True)
            (config / "config.toml").write_text(
                f'codexbar_cli = "{configured.as_posix()}"\n',
                encoding="utf-8",
            )

            candidates = resolve_codexbar_candidates(
                None,
                environ={"HOME": str(home)},
                which=lambda _: None,
                dialect=hermetic_dialect(helper),
            )

        self.assertEqual(candidates, (configured, helper))

    def test_path_lookup_uses_the_upstream_binary_names(self) -> None:
        names: list[str] = []

        resolve_codexbar_candidates(
            None,
            environ={},
            which=lambda name: names.append(name) or None,
            dialect=hermetic_dialect(),
        )

        self.assertEqual(names, ["codexbar", "CodexBarCLI"])


class MacCodexBarSourceTests(unittest.IsolatedAsyncioTestCase):
    async def test_collect_executes_the_upstream_json_interface(self) -> None:
        seen: list[tuple[str, ...]] = []

        async def runner(argv: tuple[str, ...], timeout: float):
            seen.append(argv)
            if argv[1] == "--version":
                return 0, VERSION_STDOUT, ""
            return 0, FIXTURE, ""

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar"
            executable.touch()
            source = MacCodexBarSource(executable, runner=runner)

            result = await source.collect(attempted_at=100)

        self.assertEqual(
            seen,
            [
                (str(executable), "--version"),
                (str(executable), *USAGE_ARGV),
            ],
        )
        self.assertEqual(result[Provider.CODEX].state, SourceState.OK)

    async def test_default_timeout_matches_the_upstream_cli_not_the_windows_one(
        self,
    ) -> None:
        """A 15 second limit reports a healthy upstream install as broken.

        Upstream does live web fetches; one real call was measured at 46
        seconds. The resolver cannot tell a timeout from a broken CLI, so the
        default has to come from the dialect.
        """

        timeouts: list[float] = []

        async def runner(argv: tuple[str, ...], timeout: float):
            timeouts.append(timeout)
            if argv[1] == "--version":
                return 0, VERSION_STDOUT, ""
            return 0, FIXTURE, ""

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar"
            executable.touch()

            await MacCodexBarSource(executable, runner=runner).collect(attempted_at=1)

        self.assertEqual(set(timeouts), {MACOS_USAGE_TIMEOUT})
        self.assertGreater(MACOS_USAGE_TIMEOUT, WINDOWS_DIALECT.default_timeout)

    async def test_explicit_timeout_still_wins(self) -> None:
        timeouts: list[float] = []

        async def runner(argv: tuple[str, ...], timeout: float):
            timeouts.append(timeout)
            if argv[1] == "--version":
                return 0, VERSION_STDOUT, ""
            return 0, FIXTURE, ""

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar"
            executable.touch()

            await MacCodexBarSource(
                executable, timeout=5.0, runner=runner
            ).collect(attempted_at=1)

        self.assertEqual(set(timeouts), {5.0})

    async def test_partial_failure_exit_code_still_yields_usable_data(self) -> None:
        """Upstream exits non-zero when any single provider fetch fails."""

        records = json.loads(FIXTURE)
        records[1] = {"provider": "claude", "error": {"message": "auth required"}}

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar"
            executable.touch()
            source = MacCodexBarSource(
                executable,
                runner=usage_runner(json.dumps(records), return_code=1),
            )

            result = await source.collect(attempted_at=100)

        self.assertEqual(result[Provider.CODEX].state, SourceState.OK)
        self.assertEqual(result[Provider.CLAUDE].state, SourceState.UNAVAILABLE)

    async def test_total_failure_exit_code_is_a_resolution_error(self) -> None:
        records = [
            {"provider": "codex", "error": {"message": "auth required"}},
            {"provider": "claude", "error": {"message": "auth required"}},
        ]

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar"
            executable.touch()
            source = MacCodexBarSource(
                executable,
                runner=usage_runner(json.dumps(records), return_code=1),
            )

            with self.assertRaises(CodexBarResolutionError) as raised:
                await source.collect(attempted_at=100)

        self.assertEqual(raised.exception.category, "capability")

    async def test_failures_never_expose_cli_output(self) -> None:
        async def runner(argv: tuple[str, ...], timeout: float):
            if argv[1] == "--version":
                return 0, VERSION_STDOUT, ""
            return 1, "", "token sk-ant-oat-secret for user@example.com"

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar"
            executable.touch()
            source = MacCodexBarSource(executable, runner=runner)

            with self.assertRaises(CodexBarResolutionError) as raised:
                await source.collect(attempted_at=100)

        message = str(raised.exception)
        self.assertNotIn("sk-ant-oat-secret", message)
        self.assertNotIn("user@example.com", message)

    async def test_desktop_bundle_is_rejected_by_the_identity_probe(self) -> None:
        async def runner(argv: tuple[str, ...], timeout: float):
            return 0, "some other tool 1.0.0\n", ""

        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory).resolve() / "codexbar"
            executable.touch()
            source = MacCodexBarSource(executable, runner=runner)

            with self.assertRaises(CodexBarResolutionError) as raised:
                await source.collect(attempted_at=100)

        self.assertEqual(raised.exception.category, "identity")

    async def test_missing_cli_names_the_macos_product(self) -> None:
        resolver = CodexBarResolver(
            None,
            environ={},
            which=lambda _: None,
            runner=usage_runner(FIXTURE),
            dialect=hermetic_dialect(),
        )

        with self.assertRaises(CodexBarResolutionError) as raised:
            await resolver.collect(attempted_at=100)

        self.assertEqual(raised.exception.category, "not_found")
        self.assertIn("CodexBar", str(raised.exception))
        self.assertNotIn("Win-CodexBar", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
