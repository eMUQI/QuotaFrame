QuotaFrame - Product Release Assets
==========================================

The primary release path is the automatic GitHub Actions workflow triggered by
a matching v* tag. scripts/assemble_release.py is the cross-platform manual
assembly fallback.

Release notes are not generated. The workflow publishes
docs/release/notes/v<version>.md verbatim as the Release body, and preflight
fails before any build when that file is missing.

The tag workflow assembles these assets and nothing else:

  quotaframe-bridge-windows-v<version>.exe
  quotaframe-bridge-windows-v<version>-setup.exe
  quotaframe-bridge-macos-arm64-v<version>.dmg
  <release-stem>-ota-v<version>.bin    one per maintained target
  <release-stem>-full-v<version>.bin   one per maintained target
  manifest.json
  SHA256SUMS.txt
  LICENSE
  THIRD_PARTY_LICENSES.md

Release stems come from bridge/src/quotaframe_bridge/targets.py, which
scripts/assemble_release.py reads, so a newly maintained target contributes
its own pair without an edit here.

Windows: run quotaframe-bridge-windows-v<version>-setup.exe for a per-user
installation. Upgrades preserve configuration, owned devices and autostart
preferences. The standalone .exe remains available. Missing CLI dependencies
are downloaded from the pinned upstream release and verified before use.

Apple Silicon macOS: open
quotaframe-bridge-macos-arm64-v<version>.dmg, move QuotaFrame
Bridge.app to Applications, and open it. Builds use ad-hoc signing
only. If Gatekeeper blocks a launch, open System Settings > Privacy & Security,
click Open Anyway for the app, then confirm Open. A newly downloaded version
may require approval again. Developer ID signing, notarization and Intel
builds are not complete.

For first installation or serial recovery, select the -full- image matching the
board and write it from address 0x0. The -ota- files are application images;
manifest.json selects only those files for Bridge firmware updates. Never send
a -full- image through OTA. The tag Release does not include esptool, drivers,
GUI flashers, flash scripts, or build directories.

Bridge update checking validates public Release metadata and, after an explicit
user click, opens the Windows installer download in the default browser (or
opens the Release page if the installer asset is unavailable). macOS opens the Release
page. The Bridge never downloads or replaces its own
program automatically. See FIRMWARE_RELEASE.md, the main README, and
bridge/README.md for the complete publishing, installation, and recovery
boundaries.
