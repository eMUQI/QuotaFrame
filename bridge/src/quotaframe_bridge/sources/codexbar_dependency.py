"""Install the hash-pinned Windows CLI without installing the CodexBar desktop app."""

from __future__ import annotations

import hashlib
import os
import tempfile
import time
import urllib.request
from collections.abc import Mapping
from pathlib import Path
from urllib.parse import urlsplit

VERSION = "1.2.12"
URL = f"https://github.com/nesszer/Win-CodexBar/releases/download/v{VERSION}/codexbar.exe"
SIZE = 12_583_424
SHA256 = "a9d5d603705d168f7b587066d469deff9f2acb6780f022c88e55c4fc41ed7166"
DOWNLOAD_TIMEOUT = 120.0


class DependencyDownloadError(RuntimeError):
    """The CLI could not be downloaded and verified; no downloaded code was run."""


class _HttpsRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urlsplit(newurl).scheme != "https":
            raise DependencyDownloadError("CLI download requires HTTPS")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def cli_path(environ: Mapping[str, str]) -> Path | None:
    base = environ.get("LOCALAPPDATA")
    if not base:
        return None
    return Path(base) / "quotaframe" / "dependencies" / "codexbar" / VERSION / "codexbar-cli.exe"


def verified_cli(environ: Mapping[str, str]) -> Path | None:
    path = cli_path(environ)
    if path is None:
        return None
    try:
        if path.stat().st_size != SIZE:
            return None
        with path.open("rb") as stream:
            return path if hashlib.file_digest(stream, "sha256").hexdigest() == SHA256 else None
    except OSError:
        return None


def install_cli(environ: Mapping[str, str]) -> Path:
    """Download to a sibling temporary file and publish only verified bytes."""
    existing = verified_cli(environ)
    if existing is not None:
        return existing
    destination = cli_path(environ)
    if destination is None:
        raise DependencyDownloadError("LOCALAPPDATA is required for the managed CLI")
    temporary: Path | None = None
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        request = urllib.request.Request(URL, headers={"User-Agent": "quotaframe-bridge"})
        opener = urllib.request.build_opener(_HttpsRedirect())
        deadline = time.monotonic() + DOWNLOAD_TIMEOUT
        with opener.open(request, timeout=30) as response:
            if urlsplit(response.geturl()).scheme != "https":
                raise DependencyDownloadError("CLI download requires HTTPS")
            length = response.headers.get("Content-Length")
            if length is not None and int(length) != SIZE:
                raise DependencyDownloadError("CLI download size does not match")
            digest = hashlib.sha256()
            received = 0
            with tempfile.NamedTemporaryFile(dir=destination.parent, suffix=".part", delete=False) as stream:
                temporary = Path(stream.name)
                while chunk := response.read1(min(64 * 1024, SIZE + 1 - received)):
                    received += len(chunk)
                    if received > SIZE or time.monotonic() > deadline:
                        raise DependencyDownloadError("CLI download exceeded its size or time limit")
                    digest.update(chunk)
                    stream.write(chunk)
        if received != SIZE or digest.hexdigest() != SHA256:
            raise DependencyDownloadError("CLI download checksum does not match")
        os.replace(temporary, destination)
        return destination
    except DependencyDownloadError:
        raise
    except Exception:
        raise DependencyDownloadError("CLI download failed; check the network and retry") from None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
