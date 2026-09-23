#!/usr/bin/env bash

set -euo pipefail

usage() {
    echo "usage: $0 --app <path-to-app> --output <path-to-dmg>" >&2
    exit 2
}

app_path=""
output_path=""
while (($#)); do
    case "$1" in
        --app)
            [[ $# -ge 2 ]] || usage
            app_path="$2"
            shift 2
            ;;
        --output)
            [[ $# -ge 2 ]] || usage
            output_path="$2"
            shift 2
            ;;
        *)
            usage
            ;;
    esac
done

[[ -n "$app_path" ]] || usage
[[ -n "$output_path" ]] || usage
[[ -d "$app_path" ]] || {
    echo "app not found: $app_path" >&2
    exit 1
}
app_basename="$(basename "$app_path")"

staging_dir="$(mktemp -d "${TMPDIR:-/tmp}/quotaframe-dmg.XXXXXX")"
trap 'rm -rf "$staging_dir"' EXIT
cp -R "$app_path" "$staging_dir/$app_basename"
ln -s /Applications "$staging_dir/Applications"

app_name="${app_basename%.app}"
mkdir -p "$(dirname "$output_path")"

hdiutil create \
    -volname "$app_name" \
    -srcfolder "$staging_dir" \
    -ov \
    -format UDZO \
    "$output_path"

echo "Built $output_path"
