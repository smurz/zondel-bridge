#!/usr/bin/env bash
# Extracts a single version's section from CHANGELOG.md
# Usage: extract-changelog.sh v0.1.0   (or  0.1.0  — either works)
# License: GPL-2.0-or-later. Copyright (c) 2026 Zondel.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <version-or-tag>" >&2
  exit 2
fi

version="${1#v}"   # strip optional leading v
changelog="${CHANGELOG_PATH:-CHANGELOG.md}"

if [[ ! -f "$changelog" ]]; then
  echo "$changelog not found" >&2
  exit 1
fi

awk -v v="$version" '
  $0 ~ "^## \\[" v "\\]" { capture=1; print; next }
  capture && $0 ~ "^## \\[" { capture=0 }
  capture { print }
' "$changelog"
