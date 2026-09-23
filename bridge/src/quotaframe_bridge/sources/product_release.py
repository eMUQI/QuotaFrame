"""Validated discovery of public QuotaFrame product releases."""

from __future__ import annotations

import asyncio
import json
import urllib.request
from collections.abc import Callable
from dataclasses import dataclass

from quotaframe_bridge.release_config import validate_repository
from quotaframe_bridge.versioning import SemVer, VersionError


MAX_RELEASE_LIST_BYTES = 256 * 1024
Fetcher = Callable[[str, int], bytes]


class ProductReleaseError(ValueError):
    """The public release list or one of its required assets is invalid."""


@dataclass(frozen=True, slots=True)
class ProductRelease:
    """One validated non-draft release and its public manifest and Windows download URLs."""

    version: SemVer
    tag_name: str
    page_url: str
    manifest_url: str
    windows_installer_url: str | None = None
    windows_portable_url: str | None = None


def _default_fetcher(url: str, max_bytes: int) -> bytes:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "quotaframe-bridge"},
    )
    with urllib.request.urlopen(request, timeout=10) as response:
        content_length = response.headers.get("Content-Length")
        if content_length is not None:
            try:
                if int(content_length) > max_bytes:
                    raise ProductReleaseError("release response is too large")
            except ValueError:
                raise ProductReleaseError(
                    "release response has an invalid content length"
                ) from None
        return response.read(max_bytes + 1)


class ProductReleaseSource:
    """Discover releases while enforcing this project's tag/asset URL contract."""

    def __init__(
        self,
        repository: str,
        *,
        fetcher: Fetcher = _default_fetcher,
    ) -> None:
        self._repository = validate_repository(repository)
        self._fetcher = fetcher

    async def latest(self, current: SemVer) -> ProductRelease | None:
        """Return the newest eligible release at or above `current`.

        Stable installations ignore prereleases; prerelease installations may
        continue on either prerelease or stable versions according to SemVer
        ordering.
        """

        payload = await self._fetch_release_list()
        releases = self._parse_release_list(payload)
        eligible = [item for item in releases if item.version >= current]
        if not current.is_prerelease:
            eligible = [item for item in eligible if not item.version.is_prerelease]
        return max(eligible, key=lambda item: item.version, default=None)

    async def _fetch_release_list(self) -> bytes:
        url = (
            f"https://api.github.com/repos/{self._repository}/"
            "releases?per_page=20"
        )
        try:
            payload = await asyncio.to_thread(
                self._fetcher,
                url,
                MAX_RELEASE_LIST_BYTES,
            )
        except ProductReleaseError:
            raise
        except Exception:
            raise ProductReleaseError("release request failed") from None
        if not isinstance(payload, bytes):
            raise ProductReleaseError("release response is not bytes")
        if len(payload) > MAX_RELEASE_LIST_BYTES:
            raise ProductReleaseError("release response is too large")
        return payload

    def _parse_release_list(self, payload: bytes) -> tuple[ProductRelease, ...]:
        try:
            document = json.loads(payload)
        except (UnicodeDecodeError, json.JSONDecodeError):
            raise ProductReleaseError("release response is not valid JSON") from None
        if not isinstance(document, list):
            raise ProductReleaseError("release response is not a list")

        releases: list[ProductRelease] = []
        for item in document:
            if not isinstance(item, dict) or not isinstance(item.get("draft"), bool):
                raise ProductReleaseError("release entry is invalid")
            if item["draft"]:
                continue
            releases.append(self._parse_release(item))
        return tuple(releases)

    def _parse_release(self, item: dict[object, object]) -> ProductRelease:
        tag_name = item.get("tag_name")
        if not isinstance(tag_name, str):
            raise ProductReleaseError("release tag is invalid")
        try:
            version = SemVer.from_tag(tag_name)
        except VersionError:
            raise ProductReleaseError("release tag is invalid") from None

        prerelease = item.get("prerelease")
        if not isinstance(prerelease, bool) or prerelease != version.is_prerelease:
            raise ProductReleaseError("release channel does not match its tag")

        page_url = (
            f"https://github.com/{self._repository}/releases/tag/{tag_name}"
        )
        if item.get("html_url") != page_url:
            raise ProductReleaseError("release page URL is invalid")

        assets = item.get("assets")
        if not isinstance(assets, list):
            raise ProductReleaseError("release assets are invalid")
        manifests: list[dict[object, object]] = []
        for asset in assets:
            if not isinstance(asset, dict):
                raise ProductReleaseError("release asset is invalid")
            if asset.get("name") == "manifest.json":
                manifests.append(asset)
        if len(manifests) != 1:
            raise ProductReleaseError("release manifest asset is missing or duplicated")

        # The API-provided URLs are validated against paths derived from the
        # configured repository and tag. This prevents release metadata from
        # redirecting update checks to an unrelated host or repository.
        manifest_url = (
            f"https://github.com/{self._repository}/releases/download/"
            f"{tag_name}/manifest.json"
        )
        if manifests[0].get("browser_download_url") != manifest_url:
            raise ProductReleaseError("release manifest URL is invalid")
        windows_urls: dict[str, str | None] = {}
        for kind, suffix in (("installer", "-setup.exe"), ("portable", ".exe")):
            name = f"quotaframe-bridge-windows-v{version}{suffix}"
            matches = [asset for asset in assets if asset.get("name") == name]
            url = f"https://github.com/{self._repository}/releases/download/{tag_name}/{name}"
            if matches and (len(matches) != 1 or matches[0].get("browser_download_url") != url):
                raise ProductReleaseError(f"release Windows {kind} URL is invalid")
            windows_urls[kind] = url if matches else None
        return ProductRelease(
            version, tag_name, page_url, manifest_url,
            windows_urls["installer"], windows_urls["portable"],
        )
