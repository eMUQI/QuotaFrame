from __future__ import annotations

import functools
import re
from dataclasses import dataclass


_NUMBER = r"(?:0|[1-9][0-9]*)"
_PRERELEASE_ID = r"(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
_PATTERN = re.compile(
    rf"^({_NUMBER})\.({_NUMBER})\.({_NUMBER})"
    rf"(?:-({_PRERELEASE_ID}(?:\.{_PRERELEASE_ID})*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)
_GIT_DESCRIBE_PATTERN = re.compile(
    rf"^v({_NUMBER})\.({_NUMBER})\.({_NUMBER})-"
    rf"({_NUMBER})-g([0-9a-f]+)(-dirty)?$"
)


class VersionError(ValueError):
    pass


@functools.total_ordering
@dataclass(frozen=True, slots=True, eq=False)
class SemVer:
    major: int
    minor: int
    patch: int
    prerelease: tuple[int | str, ...] = ()
    build: tuple[str, ...] = ()

    @classmethod
    def parse(cls, value: str) -> "SemVer":
        match = _PATTERN.fullmatch(value) if isinstance(value, str) else None
        if match is None:
            raise VersionError("version is not canonical SemVer")
        prerelease = tuple(
            int(item) if item.isdecimal() else item
            for item in (match.group(4) or "").split(".")
            if item
        )
        build = tuple(item for item in (match.group(5) or "").split(".") if item)
        return cls(
            int(match.group(1)),
            int(match.group(2)),
            int(match.group(3)),
            prerelease,
            build,
        )

    @classmethod
    def from_tag(cls, tag: str) -> "SemVer":
        if not isinstance(tag, str) or not tag.startswith("v"):
            raise VersionError("release tag must start with v")
        return cls.parse(tag[1:])

    @classmethod
    def from_device(cls, value: str) -> "SemVer":
        match = (
            _GIT_DESCRIBE_PATTERN.fullmatch(value)
            if isinstance(value, str)
            else None
        )
        if match is not None:
            build = (match.group(4), f"g{match.group(5)}")
            if match.group(6):
                build += ("dirty",)
            return cls(
                int(match.group(1)),
                int(match.group(2)),
                int(match.group(3)),
                build=build,
            )
        if isinstance(value, str) and value.startswith("v"):
            return cls.parse(value[1:])
        return cls.parse(value)

    @property
    def is_prerelease(self) -> bool:
        return bool(self.prerelease)

    def _precedence(self) -> tuple[int, int, int]:
        return self.major, self.minor, self.patch

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, SemVer):
            return NotImplemented
        return (
            self._precedence() == other._precedence()
            and self.prerelease == other.prerelease
        )

    def __hash__(self) -> int:
        return hash((self._precedence(), self.prerelease))

    def __lt__(self, other: object) -> bool:
        if not isinstance(other, SemVer):
            return NotImplemented
        if self._precedence() != other._precedence():
            return self._precedence() < other._precedence()
        if not self.prerelease or not other.prerelease:
            return bool(self.prerelease) and not other.prerelease
        for left, right in zip(self.prerelease, other.prerelease):
            if left == right:
                continue
            if isinstance(left, int) and isinstance(right, str):
                return True
            if isinstance(left, str) and isinstance(right, int):
                return False
            return left < right
        return len(self.prerelease) < len(other.prerelease)

    def __str__(self) -> str:
        value = f"{self.major}.{self.minor}.{self.patch}"
        if self.prerelease:
            value += "-" + ".".join(map(str, self.prerelease))
        if self.build:
            value += "+" + ".".join(self.build)
        return value
