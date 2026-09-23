from __future__ import annotations

import json
import re
from collections.abc import Callable, Mapping
from importlib import resources


REPOSITORY_ENV = "QUOTAFRAME_RELEASE_REPO"
_REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")

EmbeddedReader = Callable[[], bytes]


class ReleaseConfigError(ValueError):
    pass


def validate_repository(value: object) -> str:
    if not isinstance(value, str) or _REPOSITORY.fullmatch(value) is None:
        raise ReleaseConfigError("release repository must be owner/name")
    if any(segment in {".", ".."} for segment in value.split("/")):
        raise ReleaseConfigError("release repository must be owner/name")
    return value


def _read_embedded() -> bytes:
    return (
        resources.files("quotaframe_bridge") / "release-channel.json"
    ).read_bytes()


def release_repository(
    environ: Mapping[str, str],
    *,
    embedded: EmbeddedReader = _read_embedded,
) -> str:
    if REPOSITORY_ENV in environ:
        return validate_repository(environ[REPOSITORY_ENV])
    try:
        document = json.loads(embedded())
    except (OSError, TypeError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ReleaseConfigError("embedded release repository is unavailable") from exc
    if not isinstance(document, dict):
        raise ReleaseConfigError("embedded release repository is invalid")
    return validate_repository(document.get("repository"))
