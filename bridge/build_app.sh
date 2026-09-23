#!/usr/bin/env bash

set -euo pipefail

usage() {
    echo "usage: $0 --release-repository owner/name [--python path]" >&2
    exit 2
}

release_repository=""
python_command="python3"
while (($#)); do
    case "$1" in
        --release-repository)
            (($# >= 2)) || usage
            release_repository="$2"
            shift 2
            ;;
        --python)
            (($# >= 2)) || usage
            python_command="$2"
            shift 2
            ;;
        *)
            usage
            ;;
    esac
done

[[ -n "$release_repository" ]] || usage
if [[ ! "$release_repository" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
    echo "release repository must be owner/name" >&2
    exit 2
fi
IFS=/ read -r repository_owner repository_name <<<"$release_repository"
if [[ "$repository_owner" == "." || "$repository_owner" == ".." || \
      "$repository_name" == "." || "$repository_name" == ".." ]]; then
    echo "release repository must be owner/name" >&2
    exit 2
fi

test "$(uname -m)" = arm64 || {
    echo "Apple Silicon (arm64) is required" >&2
    exit 1
}

required_python_version="3.13.14"
build_python_version="$($python_command -c 'import platform; print(platform.python_version())')"
if [[ "$build_python_version" != "$required_python_version" ]]; then
    echo "release packaging requires Python $required_python_version because release-lock-macos.txt pins CPython 3.13 wheels; got $build_python_version from $python_command. Pass --python <path> to Python $required_python_version." >&2
    exit 1
fi

root="$(cd "$(dirname "$0")" && pwd)"
repository_root="$(cd "$root/.." && pwd)"
venv_dir="$root/.build-venv"
lock_file="$root/release-lock-macos.txt"
dist_dir="$root/dist"
work_dir="$root/build"
src_dir="$root/src"
asset_dir="$repository_root/assets/tray"
entry_script="$src_dir/quotaframe_bridge/ui/macos/app.py"
icon_source="$asset_dir/app-icon-256.png"
iconset="$work_dir/QuotaFrame Bridge.iconset"
icon_file="$work_dir/QuotaFrame Bridge.icns"
release_channel="$work_dir/release-channel.json"
app_path="$dist_dir/QuotaFrame Bridge.app"
inner_executable="$app_path/Contents/MacOS/QuotaFrame Bridge"

[[ -f "$lock_file" ]] || {
    echo "release hash lock not found at $lock_file" >&2
    exit 1
}

rm -rf "$venv_dir" "$dist_dir" "$work_dir"
"$python_command" -m venv "$venv_dir"
venv_python="$venv_dir/bin/python"
# Install external packages from the platform wheel/hash lock first. This fixes
# both dependency versions and distribution bytes instead of trusting a later
# package-index resolution of the same versions.
"$venv_python" -m pip install --quiet --only-binary=:all: --no-deps \
    --require-hashes --requirement "$lock_file"
# The local project is the checked-out release source. Do not let installing it
# resolve dependencies again after the external set has passed hash checks.
"$venv_python" -m pip install --quiet --no-deps --no-build-isolation "$root[build]"
"$venv_python" -m pip check

version="$($venv_python -c \
    'from quotaframe_bridge import __version__; print(__version__)')"

mkdir -p "$iconset" "$dist_dir" "$work_dir"
for icon_spec in \
    "16 icon_16x16.png" \
    "32 icon_16x16@2x.png" \
    "32 icon_32x32.png" \
    "64 icon_32x32@2x.png" \
    "128 icon_128x128.png" \
    "256 icon_128x128@2x.png" \
    "256 icon_256x256.png" \
    "512 icon_256x256@2x.png" \
    "512 icon_512x512.png" \
    "1024 icon_512x512@2x.png"; do
    read -r pixels filename <<<"$icon_spec"
    sips -z "$pixels" "$pixels" "$icon_source" \
        --out "$iconset/$filename" >/dev/null
done
iconutil -c icns "$iconset" -o "$icon_file"

printf '{"repository":"%s"}\n' "$release_repository" >"$release_channel"

"$venv_python" -m PyInstaller \
    --onedir --windowed --name "QuotaFrame Bridge" --clean --noconfirm \
    --distpath "$dist_dir" --workpath "$work_dir" --specpath "$work_dir" \
    --icon "$icon_file" \
    --hidden-import UserNotifications \
    --add-data "$asset_dir:tray" \
    --add-data "$release_channel:quotaframe_bridge" \
    -p "$src_dir" "$entry_script"

info_plist="$app_path/Contents/Info.plist"
plist_buddy=/usr/libexec/PlistBuddy
"$plist_buddy" -c "Set :CFBundleIdentifier com.quotaframe.bridge" "$info_plist"
"$plist_buddy" -c "Set :CFBundleShortVersionString $version" "$info_plist"
"$plist_buddy" -c "Delete :CFBundleVersion" "$info_plist" >/dev/null 2>&1 || true
"$plist_buddy" -c "Add :CFBundleVersion string $version" "$info_plist"
"$plist_buddy" -c "Delete :LSUIElement" "$info_plist" >/dev/null 2>&1 || true
"$plist_buddy" -c "Add :LSUIElement bool true" "$info_plist"
"$plist_buddy" -c "Delete :NSBluetoothAlwaysUsageDescription" \
    "$info_plist" >/dev/null 2>&1 || true
"$plist_buddy" -c \
    "Add :NSBluetoothAlwaysUsageDescription string Connect to QuotaFrame panels over Bluetooth." \
    "$info_plist"

codesign --force --deep --sign - "$app_path"
codesign --verify --deep --strict "$app_path"
architectures="$(lipo -archs "$inner_executable")"
case " $architectures " in
    *" arm64 "*) ;;
    *)
        echo "frozen executable is not arm64: $architectures" >&2
        exit 1
        ;;
esac
"$inner_executable" --version

echo "Built $app_path"
