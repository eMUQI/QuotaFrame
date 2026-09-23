"""Validated GitHub Release manifests and firmware downloads."""

from __future__ import annotations

import asyncio
import hashlib
import json
import re
import os
import urllib.request
from collections.abc import Callable
from dataclasses import dataclass
from urllib.parse import urlsplit

from quotaframe_bridge.protocol.validation import unique_object, validate_identifier
from quotaframe_bridge.release_config import release_repository
from quotaframe_bridge.versioning import SemVer, VersionError

_SHA256 = re.compile(r"^[0-9a-f]{64}$")
MAX_MANIFEST_BYTES = 64 * 1024
MAX_IMAGE_BYTES = 4 * 1024 * 1024


class FirmwareReleaseError(ValueError):
    """A release URL, manifest, image size, or digest is invalid."""


@dataclass(frozen=True, slots=True)
class FirmwareImage:
    """One validated target image declared by a public release manifest."""

    target: str
    version: str
    size: int
    sha256: str
    url: str
    firmware_project: str = "quotaframe"


@dataclass(frozen=True, slots=True)
class FirmwareManifest:
    """Validated firmware images keyed by firmware project and target."""

    images: tuple[FirmwareImage, ...]

    def for_target(self, target: str, firmware_project: str = "quotaframe") -> FirmwareImage:
        """Return the image matching both project and target, or raise FirmwareReleaseError."""

        for image in self.images:
            if image.target == target and image.firmware_project == firmware_project:
                return image
        raise FirmwareReleaseError("release does not contain the connected target")


def _validate_github_url(url: object) -> str:
    if (not isinstance(url, str) or url != url.strip()
            or any(ord(c) < 32 or ord(c) == 127 for c in url)):
        raise FirmwareReleaseError("release URL is invalid")
    try:
        parsed = urlsplit(url)
        port = parsed.port
    except ValueError:
        raise FirmwareReleaseError("release URL is invalid") from None
    if (
        parsed.scheme != "https"
        or parsed.hostname != "github.com"
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
        or port not in {None, 443}
        or not parsed.path.startswith(f"/{release_repository(os.environ)}/releases/")
        or any(part in {".", ".."} for part in parsed.path.split("/"))
        or "%" in parsed.path
        or not parsed.path
    ):
        raise FirmwareReleaseError("release URL must be an HTTPS github.com URL")
    return url


class _ReleaseRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        destination = urlsplit(newurl)
        if (destination.scheme != "https" or destination.username or destination.password
                or destination.port not in {None, 443}
                or destination.hostname not in {"github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"}):
            raise FirmwareReleaseError("release redirect is not allowed")
        if destination.hostname == "github.com":
            _validate_github_url(newurl)
        return super().redirect_request(request, fp, code, message, headers, newurl)


def catalog_url() -> str:
    """Independent stable Release asset, available once a schema-1 catalog is published."""
    return f"https://github.com/{release_repository(os.environ)}/releases/latest/download/manifest.json"


def _default_fetcher(url: str, max_bytes: int) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "quotaframe-bridge"})
    with urllib.request.build_opener(_ReleaseRedirect()).open(request, timeout=30) as response:
        content_length = response.headers.get("Content-Length")
        if content_length is not None:
            try:
                if int(content_length) > max_bytes:
                    raise FirmwareReleaseError("release download is too large")
            except ValueError:
                raise FirmwareReleaseError(
                    "release download has an invalid content length"
                ) from None
        return response.read(max_bytes + 1)


class FirmwareReleaseSource:
    """Fetch and validate allowlisted public firmware artifacts from github.com.

    Caller- and manifest-provided URLs must begin as HTTPS github.com URLs.
    urllib may follow GitHub's normal download redirects, so downloaded images
    are additionally bounded by declared size and accepted only after an exact
    SHA-256 match.
    """

    def __init__(
        self,
        *,
        fetcher: Callable[[str, int], bytes] = _default_fetcher,
    ) -> None:
        self._fetcher = fetcher

    async def _fetch(self, url: str, max_bytes: int) -> bytes:
        validated = _validate_github_url(url)
        try:
            payload = await asyncio.to_thread(
                self._fetcher, validated, max_bytes
            )
        except FirmwareReleaseError:
            raise
        except Exception:
            raise FirmwareReleaseError("release download failed") from None
        if not isinstance(payload, bytes):
            raise FirmwareReleaseError("release download returned invalid bytes")
        if len(payload) > max_bytes:
            raise FirmwareReleaseError("release download is too large")
        return payload

    async def fetch_manifest(self, url: str) -> FirmwareManifest:
        """Download and strictly validate a schema-1 manifest keyed by project and target."""

        raw = await self._fetch(url, MAX_MANIFEST_BYTES)
        try:
            document = json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object)
        except (UnicodeError, ValueError, RecursionError):
            raise FirmwareReleaseError("release manifest is not valid JSON") from None
        if not isinstance(document, dict) or type(document.get("schema_version")) is not int or document["schema_version"] != 1:
            raise FirmwareReleaseError("release manifest schema is unsupported")
        items = document.get("releases")
        if not isinstance(items, list):
            raise FirmwareReleaseError("release manifest images are missing")

        images: list[FirmwareImage] = []
        seen: set[tuple[str, str]] = set()
        for item in items:
            if not isinstance(item, dict):
                raise FirmwareReleaseError("release image entry is invalid")
            target = item.get("target")
            project = item.get("firmware_project")
            try:
                validate_identifier(project, "firmware_project", 64)
                validate_identifier(target, "target")
            except ValueError as exc:
                raise FirmwareReleaseError(str(exc)) from None
            if item.get("kind") != "app":
                raise FirmwareReleaseError("release image must be an application")
            version = item.get("version")
            size = item.get("size")
            digest = item.get("sha256")
            if (project, target) in seen:
                raise FirmwareReleaseError("release target is invalid or duplicated")
            if not isinstance(version, str) or not 1 <= len(version) <= 31:
                raise FirmwareReleaseError("release version is invalid")
            try:
                SemVer.parse(version)
            except VersionError:
                raise FirmwareReleaseError("release version is invalid") from None
            if (
                isinstance(size, bool)
                or not isinstance(size, int)
                or size <= 0
                or size > MAX_IMAGE_BYTES
            ):
                raise FirmwareReleaseError("release image size is invalid")
            if not isinstance(digest, str) or _SHA256.fullmatch(digest) is None:
                raise FirmwareReleaseError("release image digest is invalid")
            image_url = _validate_github_url(item.get("url"))
            seen.add((project, target))
            images.append(FirmwareImage(target, version, size, digest, image_url, project))
        return FirmwareManifest(tuple(images))

    async def download_image(self, image: FirmwareImage) -> bytes:
        """Download an image and require exact manifest size and SHA-256 match."""

        if image.size <= 0 or image.size > MAX_IMAGE_BYTES:
            raise FirmwareReleaseError("release image size is invalid")
        payload = await self._fetch(image.url, image.size)
        if len(payload) != image.size:
            raise FirmwareReleaseError("firmware image size does not match manifest")
        if hashlib.sha256(payload).hexdigest() != image.sha256:
            raise FirmwareReleaseError("firmware image digest does not match manifest")
        return payload
