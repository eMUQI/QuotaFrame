"""Authoritative definitions for maintained hardware targets."""

from __future__ import annotations

from dataclasses import dataclass
from types import MappingProxyType
from typing import Mapping


@dataclass(frozen=True, slots=True)
class WebFlashDefinition:
    """User-facing metadata needed to assemble the browser flasher."""

    name: str
    description: str
    chip_family: str
    asset: str


@dataclass(frozen=True, slots=True)
class TargetDefinition:
    """Product-level metadata shared by Bridge and repository tooling."""

    id: str
    label: str
    firmware_project: str
    idf_version: str
    ota_image: str
    full_image: str
    release_stem: str
    image_chip_id: int
    ota_partition_bytes: int
    test_apps: tuple[str, ...] = ()
    web_flash: WebFlashDefinition | None = None

    @property
    def ota_transfer_bytes(self) -> int:
        """Allow a full app partition plus the OTA sink's 512-byte manifest buffer."""
        return self.ota_partition_bytes + 512


TARGETS: tuple[TargetDefinition, ...] = (
    TargetDefinition(
        id="m5sticks3",
        label="M5StickS3",
        firmware_project="firmware/targets/m5sticks3",
        idf_version="6.1",
        ota_image="m5_usage_panel.bin",
        full_image="m5_usage_panel_full.bin",
        release_stem="m5sticks3",
        image_chip_id=9,
        ota_partition_bytes=3 * 1024 * 1024,
        test_apps=("firmware/targets/m5sticks3/test_apps/logic",),
        web_flash=WebFlashDefinition(
            name="M5StickS3",
            description="135 × 240 显示屏 · 正面按键翻页",
            chip_family="ESP32-S3",
            asset="assets/devices/m5sticks3.jpg",
        ),
    ),
    TargetDefinition(
        id="waveshare_amoled_216",
        label="ESP32-S3-Touch-AMOLED-2.16",
        firmware_project="firmware/targets/esp32_s3_touch_amoled_216",
        idf_version="6.1",
        ota_image="ws_usage_panel.bin",
        full_image="ws_usage_panel_full.bin",
        release_stem="waveshare-esp32-s3-touch-amoled-216",
        image_chip_id=9,
        ota_partition_bytes=4 * 1024 * 1024,
        test_apps=("firmware/targets/esp32_s3_touch_amoled_216/test_apps/logic",),
        web_flash=WebFlashDefinition(
            name="ESP32-S3-Touch-AMOLED-2.16",
            description="480 × 480 圆角方形 AMOLED · 触控与滑动操作",
            chip_family="ESP32-S3",
            asset="assets/devices/waveshare_amoled_216.jpg",
        ),
    ),
    TargetDefinition(
        id="waveshare_epaper_397",
        label="ESP32-S3-ePaper-3.97",
        firmware_project="firmware/targets/waveshare_epaper_397",
        idf_version="6.1",
        ota_image="ws_epaper_397.bin",
        full_image="ws_epaper_397_full.bin",
        release_stem="waveshare-esp32-s3-epaper-397",
        image_chip_id=9,
        ota_partition_bytes=4 * 1024 * 1024,
        web_flash=WebFlashDefinition(
            name="ESP32-S3-ePaper-3.97",
            description="800 × 480 四灰阶墨水屏 · 三向拨轮操作",
            chip_family="ESP32-S3",
            asset="assets/devices/waveshare_epaper_397.jpg",
        ),
    ),
    TargetDefinition(
        id="esp_mosaico",
        label="ESP-Mosaico",
        firmware_project="firmware/targets/esp_mosaico",
        idf_version="6.1",
        ota_image="mosaico_usage_panel.bin",
        full_image="mosaico_usage_panel_full.bin",
        release_stem="espressif-esp-mosaico",
        image_chip_id=32,
        ota_partition_bytes=4 * 1024 * 1024,
        test_apps=("firmware/targets/esp_mosaico/test_apps/logic",),
        web_flash=WebFlashDefinition(
            name="ESP-Mosaico",
            description="480 × 480 AMOLED · 触控与 AI 按键翻页",
            chip_family="ESP32-S31",
            asset="assets/devices/esp_mosaico.png",
        ),
    ),
)

IDF_IMAGES: Mapping[str, str] = MappingProxyType(
    {
        "6.1": (
            "espressif/idf:v6.1@sha256:"
            "81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c"
        ),
    }
)

TARGETS_BY_ID: Mapping[str, TargetDefinition] = MappingProxyType(
    {target.id: target for target in TARGETS}
)

if len(TARGETS_BY_ID) != len(TARGETS):
    raise RuntimeError("maintained target IDs must be unique")

for target in TARGETS:
    if target.idf_version not in IDF_IMAGES:
        raise RuntimeError(
            f"maintained target {target.id!r} uses unknown ESP-IDF "
            f"version {target.idf_version!r}"
        )
