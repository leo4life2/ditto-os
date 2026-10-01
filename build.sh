#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
scout_idf="${IDF_PATH:-$HOME/esp/esp-idf}"
if [[ ! -f "$scout_idf/export.sh" ]]; then
  echo "ESP-IDF 5.5.5 not found. Set IDF_PATH; see README.md." >&2
  exit 1
fi
source "$scout_idf/export.sh"
python tools/build_provenance.py "$@"
