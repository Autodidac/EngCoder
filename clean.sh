#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
rm -rf "$ROOT/out/build"
echo "Cleaned generated EngCoder build output."
