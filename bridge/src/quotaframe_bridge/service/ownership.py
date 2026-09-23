"""Atomic, strictly validated records of explicitly adopted devices."""

from __future__ import annotations

import json
import logging
import os
import sys
import time
from collections.abc import Iterable, Iterator, Mapping
from dataclasses import dataclass, replace
from pathlib import Path

from quotaframe_bridge.paths import data_directory
from quotaframe_bridge.protocol.validation import validate_name, validate_identifier, unique_object

LOGGER = logging.getLogger(__name__)

DEVICES_FILENAME = "devices.json"
SCHEMA_VERSION = 1


@dataclass(frozen=True, slots=True)
class OwnedDevice:
    """Persisted metadata; live status remains authoritative for OTA."""

    target: str
    address: str
    added_at: int = 0
    name: str = ""
    firmware_project: str = ""


def normalize_address(address: object) -> str | None:
    """Return a comparable form of a platform BLE address, or None if unusable.

    Both platforms hand out case-insensitive hex text — colon-separated MAC
    digits or a UUID — so folding case here is what lets an address saved by
    one scan match the same device in the next one.
    """

    if not isinstance(address, str):
        return None
    if any(ord(c) < 32 or 0x7F <= ord(c) <= 0x9F for c in address):
        return None
    trimmed = address.strip()
    return trimmed.upper() if trimmed else None


def default_devices_path(
    environ: Mapping[str, str] | None = None,
    *,
    platform: str = sys.platform,
) -> Path:
    """Return the ownership file path beside the Bridge's other local state."""

    source = os.environ if environ is None else environ
    return data_directory(source, platform=platform) / DEVICES_FILENAME


def display_name(device: OwnedDevice, owned: Iterable[OwnedDevice]) -> str:
    """Disambiguate names with progressively longer address suffixes."""
    label = device.name
    peers = tuple(other for other in owned if other.name == device.name)
    if len(peers) < 2:
        return label

    compact = "".join(
        character for character in device.address if character.isalnum()
    )
    peer_addresses = tuple(
        "".join(character for character in other.address if character.isalnum())
        for other in peers
        if other.address != device.address
    )
    for width in range(4, len(compact) + 1):
        suffix = compact[-width:]
        if all(other[-width:] != suffix for other in peer_addresses):
            return f"{label} ·{suffix}"
    return f"{label} ·{device.address}"


class OwnershipStore:
    """Read and write the user's adopted-device list.

    Every mutation rewrites the whole file through a temporary sibling and
    `os.replace`, so a Bridge killed mid-write leaves either the old list or
    the new one, never a half-written file that would read as "owns nothing"
    and discard the ownership history.
    """

    def __init__(self, path: Path | None = None) -> None:
        self._path = default_devices_path() if path is None else path
        self._devices: tuple[OwnedDevice, ...] = ()
        self._loaded = False
        self.read_error = False

    @property
    def path(self) -> Path:
        return self._path

    def __iter__(self) -> Iterator[OwnedDevice]:
        return iter(self.devices)

    def __len__(self) -> int:
        return len(self.devices)

    @property
    def devices(self) -> tuple[OwnedDevice, ...]:
        """The adopted devices, loading the file on first access."""

        if not self._loaded:
            self._devices = self._read()
            self._loaded = True
        return self._devices

    def addresses(self) -> frozenset[str]:
        """Normalized addresses of every adopted device."""

        return frozenset(device.address for device in self.devices)

    def owns(self, address: object) -> bool:
        normalized = normalize_address(address)
        return normalized is not None and normalized in self.addresses()

    def add(self, target: str, address: str, *, name: str,
            firmware_project: str = "", now: int | None = None) -> OwnedDevice | None:
        """Save a validated device before attaching its long-lived session."""
        entry = {"address": address, "name": name,
                 "added_at": int(time.time()) if now is None else now}
        if target:
            entry["target"] = target
        if firmware_project:
            entry["firmware_project"] = firmware_project
        device = next(self._parse_entries([entry]))
        if device.address in self.addresses():
            return None
        self._write(self.devices + (device,))
        return device

    def update_metadata(self, address: str, status) -> OwnedDevice | None:
        """Persist changed metadata without rebuilding the device session."""
        previous = next((d for d in self.devices if d.address == address), None)
        if previous is None:
            return None
        updated = replace(previous, name=status.name, target=status.target,
                          firmware_project=status.firmware_project)
        if updated != previous:
            self._write(tuple(updated if d is previous else d for d in self.devices))
        return updated

    def remove(self, address: str) -> bool:
        """Forget one device, returning whether anything was removed."""

        normalized = normalize_address(address)
        if normalized is None:
            return False
        remaining = tuple(
            device for device in self.devices if device.address != normalized
        )
        if len(remaining) == len(self.devices):
            return False
        self._write(remaining)
        LOGGER.info("device removed: address=%s", normalized)
        return True

    def reload(self) -> tuple[OwnedDevice, ...]:
        """Re-read the file, discarding anything cached."""

        self._loaded = False
        return self.devices

    def _read(self) -> tuple[OwnedDevice, ...]:
        self.read_error = False
        try:
            raw = self._path.read_text(encoding="utf-8")
        except FileNotFoundError:
            return ()
        except (OSError, UnicodeError) as exc:
            return self._unreadable(type(exc).__name__)
        try:
            document = json.loads(raw, object_pairs_hook=unique_object)
            if (not isinstance(document, dict) or set(document) != {"schema_version", "devices"}
                    or type(document["schema_version"]) is not int or document["schema_version"] != 1
                    or not isinstance(document["devices"], list)):
                raise ValueError("invalid document")
            return tuple(self._parse_entries(document["devices"]))
        except (ValueError, TypeError, RecursionError):
            return self._unreadable("invalid device document")

    def _unreadable(self, reason: str) -> tuple[OwnedDevice, ...]:
        self.read_error = True
        LOGGER.error(
            "Device records unreadable (%s): %s. Device adoption and writes are disabled. "
            "Exit Bridge, repair the file or explicitly rename it to reset, then restart.",
            reason, self._path,
        )
        return ()

    @staticmethod
    def _parse_entries(entries: list[object]) -> Iterator[OwnedDevice]:
        """One malformed record invalidates the entire document."""
        seen: set[str] = set()
        for entry in entries:
            if (not isinstance(entry, dict)
                    or not {"address", "name", "added_at"} <= entry.keys()
                    or entry.keys() - {"address", "name", "added_at", "target", "firmware_project"}):
                raise ValueError("invalid device fields")
            address = normalize_address(entry["address"])
            if address is None or address in seen:
                raise ValueError("invalid or duplicate address")
            seen.add(address)
            name = validate_name(entry["name"])
            added_at = entry["added_at"]
            if type(added_at) is not int or added_at < 0:
                raise ValueError("invalid added_at")
            target = validate_identifier(entry["target"], "target") if "target" in entry else ""
            project = (validate_identifier(entry["firmware_project"], "firmware_project", 64)
                       if "firmware_project" in entry else "")
            yield OwnedDevice(target, address, added_at, name, project)

    def _write(self, devices: tuple[OwnedDevice, ...]) -> None:
        if self.read_error:
            raise RuntimeError("device records are unreadable; repair or explicitly reset the file")
        document = {
            "schema_version": SCHEMA_VERSION,
            "devices": [
                {
                    **({"target": device.target} if device.target else {}),
                    **({"firmware_project": device.firmware_project} if device.firmware_project else {}),
                    "name": device.name,
                    "address": device.address,
                    "added_at": device.added_at,
                }
                for device in devices
            ],
        }
        self._path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self._path.with_name(f"{self._path.name}.tmp")
        temporary.write_text(
            json.dumps(document, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, self._path)
        self._devices = devices
        self._loaded = True
