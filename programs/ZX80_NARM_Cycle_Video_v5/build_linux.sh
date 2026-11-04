#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/source"
"${NARMCC:-narmcc}" zx80_narm.tc -I . -O1 --stack-bytes 12288 -o ../zx80.narm
printf 'Built %s\n' "$(realpath ../zx80.narm)"
