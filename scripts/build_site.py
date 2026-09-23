#!/usr/bin/env python3
"""Build the website with a specified Release or the latest stable firmware."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.assemble_web_flasher import assemble, TARGETS, SemVer

REPOSITORY = "eMUQI/QuotaFrame"


def fetch(url: str) -> tuple[str, bytes]:
    for attempt in range(3):
        try:
            request = Request(url, headers={"User-Agent": "QuotaFrame-site-build"})
            with urlopen(request, timeout=60) as response:
                return response.geturl(), response.read()
        except (URLError, TimeoutError) as error:
            if isinstance(error, HTTPError) and error.code < 500 and error.code != 429:
                raise
            if attempt == 2:
                raise
            time.sleep(2 ** attempt)
    raise AssertionError("download attempts exhausted")


def download(url: str) -> bytes:
    return fetch(url)[1]


def fetch_firmware(destination: Path, release_version: str | None = None) -> str:
    if release_version is not None:
        version = SemVer.from_tag(release_version) if release_version.startswith("v") else SemVer.parse(release_version)
    else:
        release_url, _ = fetch(f"https://github.com/{REPOSITORY}/releases/latest")
        prefix = f"https://github.com/{REPOSITORY}/releases/tag/"
        if not release_url.startswith(prefix):
            raise ValueError(f"latest Release did not resolve to a release tag: {release_url}")
        version = SemVer.from_tag(release_url.removeprefix(prefix))
        if version.is_prerelease:
            raise ValueError("latest Release must be a published stable version")
    base = f"https://github.com/{REPOSITORY}/releases/download/v{version}"
    checksums = {}
    for line in download(f"{base}/SHA256SUMS.txt").decode("utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]{64}) [ *](.+)", line)
        if not match or match[2] in checksums:
            raise ValueError("invalid or duplicate SHA256SUMS entry")
        checksums[match[2]] = match[1].lower()
    for target in TARGETS:
        if target.web_flash is None:
            continue
        filename = f"{target.release_stem}-full-v{version}.bin"
        if filename not in checksums:
            raise ValueError(f"missing firmware checksum: {filename}")
        payload = download(f"{base}/{filename}")
        if hashlib.sha256(payload).hexdigest() != checksums[filename]:
            raise ValueError(f"firmware checksum mismatch: {filename}")
        (destination / filename).write_bytes(payload)
    return str(version)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="Release version or v-prefixed tag; defaults to latest stable")
    arguments = parser.parse_args()
    npm = shutil.which("npm.cmd" if sys.platform == "win32" else "npm")
    if npm is None:
        raise RuntimeError("npm is required to build the website")
    with tempfile.TemporaryDirectory(prefix="quotaframe-release-") as directory:
        assets = Path(directory)
        version = fetch_firmware(assets, arguments.version)
        print(f"Building current website with firmware v{version}", flush=True)
        subprocess.run([npm, "ci", "--ignore-scripts"], cwd=ROOT / "web", check=True)
        subprocess.run([npm, "run", "build"], cwd=ROOT / "web", check=True)
        assemble(argparse.Namespace(
            version=version, release_assets=assets, web_root=ROOT / "web",
            vendor_root=ROOT / "web/node_modules/esp-web-tools",
            output=ROOT / "pages-site",
        ))


if __name__ == "__main__":
    main()
