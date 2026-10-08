#!/usr/bin/env bash
set -euo pipefail

STATE_ROOT="${1:?usage: install-runtime.sh <engcoder-state-root>}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMMIT="167742d9dfa01306786e7d3cfd31fcb8a8b185dc"
SHORT_COMMIT="${COMMIT:0:12}"
REPO="https://github.com/GenerelSchwerz/llama.cpp.git"
VCPKG_COMMIT="9e593bb18ea69cc5095e012465dcd675a822ed0d"
VCPKG_REPO="https://github.com/microsoft/vcpkg.git"
RUNTIME_RECIPE="engcoder-qwenflash-2026.10.06-q2pad1"
CACHE_BASE="${XDG_CACHE_HOME:-$HOME/.cache}/engcoder-runtime"
BACKEND_ROOT="$CACHE_BASE/qwen-flash"
SOURCE="$BACKEND_ROOT/llama-$SHORT_COMMIT"
BUILD="$BACKEND_ROOT/build-$SHORT_COMMIT"
VCPKG_ROOT="$CACHE_BASE/vcpkg"
VCPKG_INSTALL_ROOT="$BACKEND_ROOT/vcpkg_installed"
RUNTIME_ROOT="$STATE_ROOT/runtimes/qwen-flash"
RUNTIME_BIN="$RUNTIME_ROOT/bin"

PROGRESS_FILE="$STATE_ROOT/runtime-install-qwen-flash.json"
publish_progress() {
  local percent="$1" phase="$2" message="$3" detail="${4:-}" state="${5:-installing}"
  mkdir -p "$(dirname "$PROGRESS_FILE")"
  local tmp="$PROGRESS_FILE.tmp"
  python3 - "$tmp" "$state" "$percent" "$phase" "$message" "$detail" <<'PYJSON'
import json, sys, datetime
path, state, percent, phase, message, detail = sys.argv[1:]
with open(path, 'w', encoding='utf-8') as f:
    json.dump({
        'state': state, 'percent': max(0, min(100, int(percent))),
        'phase': phase, 'message': message, 'detail': detail,
        'updated_at': datetime.datetime.now(datetime.timezone.utc).isoformat()
    }, f, separators=(',', ':'))
PYJSON
  mv -f "$tmp" "$PROGRESS_FILE"
  echo "[EngCoder runtime] ${percent}% ${phase}: ${message}"
}

publish_progress 2 preflight "Checking build tools and CUDA" "This updates the native llama.cpp/CUDA runtime only; Qwen model weights are unchanged."

publish_progress 5 preflight "Locating Git, CMake and CUDA"
for tool in git cmake nvcc nvidia-smi; do
  command -v "$tool" >/dev/null 2>&1 || { echo "$tool is required to build the Qwen Flash CUDA runtime." >&2; exit 2; }
done

publish_progress 15 dependencies "Preparing pinned vcpkg toolchain" "The first run may clone vcpkg and verify CUDA dependencies."
mkdir -p "$CACHE_BASE" "$BACKEND_ROOT"
if [[ -e "$VCPKG_ROOT" && ! -d "$VCPKG_ROOT/.git" ]]; then rm -rf "$VCPKG_ROOT"; fi
if [[ ! -d "$VCPKG_ROOT/.git" ]]; then
  git clone --filter=blob:none "$VCPKG_REPO" "$VCPKG_ROOT"
fi
git -C "$VCPKG_ROOT" fetch origin "$VCPKG_COMMIT" --depth 1
git -C "$VCPKG_ROOT" checkout --force --detach "$VCPKG_COMMIT"
if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
  "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi
(
  cd "$SCRIPT_DIR"
  "$VCPKG_ROOT/vcpkg" install --triplet x64-linux "--x-install-root=$VCPKG_INSTALL_ROOT"
)
TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
publish_progress 38 dependencies "Pinned dependencies ready" "vcpkg baseline $VCPKG_COMMIT"
echo "vcpkg: $VCPKG_COMMIT"

publish_progress 42 backend-source "Preparing pinned Qwen llama.cpp backend source" "Git clone/fetch progress remains visible in installer output while this phase is active."
if [[ -e "$SOURCE" && ! -d "$SOURCE/.git" ]]; then rm -rf "$SOURCE"; fi
if [[ ! -d "$SOURCE/.git" ]]; then
  git clone --filter=blob:none --no-checkout "$REPO" "$SOURCE"
fi
git -C "$SOURCE" fetch origin "$COMMIT" --depth 1
git -C "$SOURCE" checkout --force --detach "$COMMIT"

PATCH_FILE="$SCRIPT_DIR/patches/0001-qwen38-q2-padded-down.patch"
[[ -f "$PATCH_FILE" ]] || { echo "Qwen3.8 Q2 compatibility patch is missing: $PATCH_FILE" >&2; exit 2; }
git -C "$SOURCE" apply --check "$PATCH_FILE"
git -C "$SOURCE" apply "$PATCH_FILE"
publish_progress 55 compatibility "Qwen compatibility patch applied"
echo "Applied Qwen3.8 Q2 padded-down compatibility patch."

publish_progress 60 configure "Configuring CUDA backend build" "CMake is generating the pinned runtime project."
rm -rf "$BUILD"
GENERATOR=()
if command -v ninja >/dev/null 2>&1; then GENERATOR=(-G Ninja); fi
cmake -S "$SOURCE" -B "$BUILD" "${GENERATOR[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_MANIFEST_MODE=OFF \
  -DVCPKG_INSTALLED_DIR="$VCPKG_INSTALL_ROOT" \
  -DGGML_CUDA=ON \
  -DGGML_NATIVE=ON \
  -DLLAMA_BUILD_COMMON=ON \
  -DLLAMA_BUILD_SERVER=ON \
  -DLLAMA_BUILD_APP=OFF \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_TOOLS=ON \
  -DLLAMA_BUILD_UI=OFF \
  -DLLAMA_BUILD_BORINGSSL=ON
publish_progress 72 build "Compiling Qwen Runtime Backend" "This is normally the longest toolchain phase."
cmake --build "$BUILD" --target llama-server --parallel "$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc)"

BIN=""
for candidate in "$BUILD/bin" "$BUILD/bin/Release" "$BUILD/bin/RelWithDebInfo"; do
  if [[ -x "$candidate/llama-server" ]]; then BIN="$candidate"; break; fi
done
[[ -n "$BIN" ]] || { echo "llama-server was built but not found." >&2; exit 2; }

publish_progress 94 stage "Staging runtime binaries" "The completed backend is being copied into EngCoder runtime storage."
STAGING="$RUNTIME_BIN.staging"
rm -rf "$STAGING"
mkdir -p "$STAGING"
cp -a "$BIN"/. "$STAGING"/
[[ -x "$STAGING/llama-server" ]] || { echo "runtime staging failed" >&2; exit 2; }
rm -rf "$RUNTIME_BIN"
mkdir -p "$RUNTIME_ROOT"
mv "$STAGING" "$RUNTIME_BIN"
cat > "$RUNTIME_ROOT/runtime.json" <<JSON
{
  "runtime": "qwen-flash",
  "backend_commit": "$COMMIT",
  "build_recipe": "$RUNTIME_RECIPE",
  "source": "engcoder-installer",
  "features": ["qwen38-q2-padded-down-v1"]
}
JSON

publish_progress 100 complete "Qwen Runtime Backend ready" "$RUNTIME_BIN" installed
echo "Qwen Flash runtime installed: $RUNTIME_BIN"
echo "Runtime recipe: $RUNTIME_RECIPE"
