#!/usr/bin/env bash
set -euo pipefail

# hex_to_xor
# Author: Mikhail Khoroshavin aka "XopMC"
# Packages the Linux build into a release-ready tarball with docs, manifest, and SHA256.

binary_path=""
output_dir=""
version=""
source_sha=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary-path)
      binary_path="$2"
      shift 2
      ;;
    --output-dir)
      output_dir="$2"
      shift 2
      ;;
    --version)
      version="$2"
      shift 2
      ;;
    --source-sha)
      source_sha="$2"
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

if [[ -z "$binary_path" || -z "$output_dir" || -z "$version" || -z "$source_sha" ]]; then
  echo "Usage: $0 --binary-path PATH --output-dir DIR --version TAG --source-sha SHA" >&2
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$output_dir"
package_root="$output_dir/XorFilter-$version-linux-x64"
archive_path="$output_dir/XorFilter-$version-linux-x64.tar.gz"
sha_path="$archive_path.sha256"

mkdir -p "$package_root"
install -m 0755 "$repo_root/$binary_path" "$package_root/hex_to_xor"
install -m 0644 "$repo_root/README.md" "$package_root/README.md"
install -m 0644 "$repo_root/LICENSE.txt" "$package_root/LICENSE.txt"

cat > "$package_root/MANIFEST.txt" <<EOF
Project: hex_to_xor
Version: $version
Platform: linux-x64
Source commit: $source_sha
Packaged at: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
EOF

tar -czf "$archive_path" -C "$output_dir" "$(basename "$package_root")"
sha256sum "$archive_path" | awk '{ print tolower($1) "  " $2 }' > "$sha_path"
rm -rf "$package_root"

echo "Created $archive_path"
echo "Created $sha_path"
