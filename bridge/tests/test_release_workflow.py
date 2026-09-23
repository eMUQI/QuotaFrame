from __future__ import annotations

import unittest
from pathlib import Path

from quotaframe_bridge import __version__


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"
CI_WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"
LICENSES = ROOT / "THIRD_PARTY_LICENSES.md"
WINDOWS_CONSTRAINTS = ROOT / "bridge" / "release-constraints-windows.txt"
MACOS_CONSTRAINTS = ROOT / "bridge" / "release-constraints-macos.txt"


def _read_constraints(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for raw_line in path.read_text("utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        name, version = line.split("==", 1)
        result[name.lower()] = version
    return result


def _license_sections() -> dict[str, str]:
    sections: dict[str, str] = {}
    for chunk in LICENSES.read_text("utf-8").split("\n## ")[1:]:
        heading, _, body = chunk.partition("\n")
        sections[heading.strip().strip("`").lower()] = body
    return sections


def _notice_heading(package: str) -> str:
    return package.lower().replace(".", "-")


class ReleaseWorkflowInvariantTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW.read_text("utf-8")
        cls.ci_workflow = CI_WORKFLOW.read_text("utf-8")

    def test_uses_immutable_release_toolchain(self) -> None:
        self.assertIn(
            "container:\n      image: ${{ matrix.idf_image }}",
            self.workflow,
        )
        self.assertNotIn("container: espressif/idf:", self.workflow)
        self.assertIn("python-version: \"3.13.14\"", self.workflow)
        for mutable_ref in (
            "actions/checkout@v5",
            "actions/setup-python@v5",
            "actions/setup-python@v6",
            "actions/upload-artifact@v7",
            "actions/download-artifact@v8",
        ):
            self.assertNotIn(mutable_ref, self.workflow)
        for immutable_ref in (
            "actions/checkout@fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09",
            "actions/setup-python@ece7cb06caefa5fff74198d8649806c4678c61a1",
            "actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a",
            "actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c",
        ):
            self.assertIn(immutable_ref, self.workflow)

    def test_every_release_constraint_has_a_versioned_license_notice(self) -> None:
        sections = _license_sections()
        constraints = {
            **_read_constraints(WINDOWS_CONSTRAINTS),
            **_read_constraints(MACOS_CONSTRAINTS),
        }
        for package, version in constraints.items():
            heading = _notice_heading(package)
            self.assertIn(heading, sections, package)
            self.assertIn(f"`{version}`", sections[heading], package)

    def test_publish_token_is_confined_to_one_job(self) -> None:
        publish = self.workflow.split("\n  publish:", 1)[1].split(
            "\n  refresh-cloudflare:", 1
        )[0]
        self.assertEqual(publish.count("GH_TOKEN: ${{ github.token }}"), 3)
        self.assertNotIn("GH_TOKEN:", publish.split("steps:", 1)[0])
        self.assertIn("permissions:\n  contents: read", self.workflow)
        self.assertIn("permissions:\n      contents: write", publish)
        self.assertEqual(self.workflow.count("contents: write"), 1)
        self.assertNotIn("secrets.RELEASE_TOKEN", self.workflow)

    def test_ci_runs_bridge_for_files_the_bridge_suite_validates(self) -> None:
        # The Bridge suite reads these files, so they cannot be classified as
        # documentation and skipped.
        self.assertIn(
            "if grep -Eq '^(LICENSE|THIRD_PARTY_LICENSES",
            self.ci_workflow,
        )
        self.assertIn("docs/release/notes/", self.ci_workflow)

    def test_ci_actions_are_immutable(self) -> None:
        web_job = self.ci_workflow.split("\n  web:", 1)[1].split(
            "\n  firmware_build:", 1
        )[0]
        self.assertNotIn("actions/checkout@v5", web_job)
        self.assertIn(
            "actions/checkout@fbc6f3992d24b796d5a048ff273f7fcc4a7b6c09",
            web_job,
        )

    def test_release_repository_is_the_source_repository(self) -> None:
        self.assertIn(
            'release_repository = validate_repository(os.environ["GITHUB_REPOSITORY"])',
            self.workflow,
        )
        self.assertNotIn("vars.RELEASE_REPO", self.workflow)
        self.assertIn('--verify-tag --draft', self.workflow)

    def test_downstream_jobs_use_the_validated_release_repository(self) -> None:
        self.assertIn(
            "release_repository: ${{ steps.version.outputs.release_repository }}",
            self.workflow,
        )
        self.assertGreaterEqual(
            self.workflow.count("${{ needs.preflight.outputs.release_repository }}"),
            3,
        )
        downstream = self.workflow.split("\n  firmware:", 1)[1]
        self.assertNotIn('RELEASE_REPO: ${{ vars.RELEASE_REPO }}', downstream)

    def test_windows_job_builds_and_publishes_installer_alongside_portable(self):
        self.assertIn("./bridge/build_installer.ps1 -Python python", self.workflow)
        self.assertIn("bridge/dist/quotaframe-bridge.exe", self.workflow)
        self.assertIn("bridge/dist/quotaframe-bridge-windows-*-setup.exe", self.workflow)
        self.assertIn('--windows-setup downloads/windows/quotaframe-bridge-windows-v"$version"-setup.exe', self.workflow)

    def test_macos_job_builds_and_publishes_versioned_dmg(self) -> None:
        self.assertIn("./bridge/build_dmg.sh", self.workflow)
        self.assertIn(
            "bridge/dist/quotaframe-bridge-macos-arm64-*.dmg",
            self.workflow,
        )
        self.assertIn(
            '--macos-dmg downloads/macos/quotaframe-bridge-macos-arm64-v"$version".dmg',
            self.workflow,
        )

    def test_release_notes_come_from_the_versioned_file_preflight_checks(self):
        self.assertIn('notes = Path("docs/release/notes") / f"v{version}.md"',
                      self.workflow)
        self.assertIn("missing release notes", self.workflow)
        self.assertIn('notes="docs/release/notes/v${VERSION}.md"', self.workflow)
        self.assertEqual(self.workflow.count('--notes-file "$notes"'), 2)
        notes = ROOT / "docs" / "release" / "notes" / f"v{__version__}.md"
        self.assertTrue(notes.is_file(), notes)

    def test_publish_verifies_draft_before_making_release_public(self) -> None:
        for operation in (
            "--draft",
            "gh release download",
            "sha256sum --check SHA256SUMS.txt",
            "gh release edit",
            "--draft=false",
            "curl --fail --silent --show-error --location",
            "cmp --silent",
        ):
            self.assertIn(operation, self.workflow)

    def test_publish_rerun_resumes_only_after_verifying_public_assets(self) -> None:
        publish = self.workflow.split("\n  publish:", 1)[1].split(
            "\n  refresh-cloudflare:", 1
        )[0]
        self.assertIn("id: release", publish)
        self.assertIn("already_published=true", publish)
        self.assertIn("steps.release.outputs.already_published", publish)
        self.assertNotIn("refusing to replace published release", publish)

    def test_tag_version_uses_shared_parser_and_release_is_serialized(self) -> None:
        self.assertIn("SemVer.from_tag", self.workflow)
        self.assertIn("__version__", self.workflow)
        self.assertIn("concurrency: release-${{ github.ref }}", self.workflow)
        self.assertIn("workflow_dispatch:", self.workflow)
        self.assertIn('os.environ["RELEASE_TAG"] or f"v{__version__}"', self.workflow)
        self.assertIn("name: quotaframe-v${{ needs.preflight.outputs.version }}-candidate", self.workflow)
        for step in (
            "Prepare Release assets",
            "Re-download and verify Release assets",
            "Publish and verify anonymous firmware access",
        ):
            self.assertIn(f"name: {step}\n        if: github.event_name == 'push'", self.workflow)

    def test_website_refresh_comes_after_the_public_stable_release(self) -> None:
        refresh = self.workflow.split("\n  refresh-cloudflare:", 1)[1]
        self.assertIn("needs:\n      - preflight\n      - publish", refresh)
        self.assertIn("if: github.event_name == 'push' && needs.preflight.outputs.prerelease == 'false'", refresh)
        self.assertIn("permissions: {}", refresh)
        self.assertIn("secrets.CLOUDFLARE_PAGES_DEPLOY_HOOK", refresh)
        self.assertNotIn("RELEASE_TOKEN", refresh)


if __name__ == "__main__":
    unittest.main()
