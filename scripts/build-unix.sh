#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NATIVE_ROOT="$ROOT/projects/native"
WEB_ROOT="$ROOT/projects/web"
cd "$ROOT"

CONFIG="Release"
RUN_TESTS=1
RUN_APP=1
USE_EXTERNAL_VCPKG=0
KEEP_DEPENDENCIES=0
CLIENT="gui"

usage() {
  cat <<'USAGE'
Usage: ./auto.sh [options]

Options:
  --config Debug|Release       Build configuration (default: Release)
  --client gui|cli|web|server|none  Launch mode (default: gui; GUI/CLI embed runtime)
  --no-tests                  Skip CTest
  --no-run                    Build/test only; do not launch EngCoder
  --use-external-vcpkg        Use VCPKG_ROOT instead of the pinned local checkout
  --keep-dependencies          Keep the project-local vcpkg checkout after a successful build
  -h, --help                  Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --config) CONFIG="${2:?--config requires Debug or Release}"; shift 2 ;;
    Debug|Release) CONFIG="$1"; shift ;;
    --client) CLIENT="${2:?--client requires gui, cli, web, server, or none}"; shift 2 ;;
    --no-tests) RUN_TESTS=0; shift ;;
    --no-run) RUN_APP=0; shift ;;
    --use-external-vcpkg) USE_EXTERNAL_VCPKG=1; shift ;;
    --keep-dependencies) KEEP_DEPENDENCIES=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$CONFIG" in Debug|Release) ;; *) echo "configuration must be Debug or Release" >&2; exit 2 ;; esac
case "$CLIENT" in gui|cli|web|server|none) ;; *) echo "client must be gui, cli, web, server, or none" >&2; exit 2 ;; esac

VCPKG_TAG="2026.07.29"
VCPKG_BASELINE="9e593bb18ea69cc5095e012465dcd675a822ed0d"

require_command() { command -v "$1" >/dev/null 2>&1 || { echo "$1 is required but was not found on PATH." >&2; exit 1; }; }
require_command cmake
require_command ctest
require_command git
require_command python3
require_command ninja

if [[ "$(uname -s)" == "Linux" ]]; then
  require_command pkg-config
  if command -v dpkg-query >/dev/null 2>&1; then
    required_packages=(
      libx11-dev libxft-dev libxext-dev
      libwayland-dev libxkbcommon-dev libegl1-mesa-dev libibus-1.0-dev
    )
    missing_packages=()
    for package in "${required_packages[@]}"; do
      if ! dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'ok installed'; then
        missing_packages+=("$package")
      fi
    done
    if [[ ${#missing_packages[@]} -gt 0 ]]; then
      echo "EngCoder native GUI requires SDL3 Linux development dependencies." >&2
      echo "Install the missing packages, then rerun auto.sh:" >&2
      echo "  sudo apt install ${missing_packages[*]}" >&2
      exit 1
    fi
  fi
fi

CMAKE_VERSION="$(cmake --version | head -1 | awk '{print $3}')"
python3 - "$CMAKE_VERSION" <<'PYVER'
import sys
parts=tuple(int(x) for x in sys.argv[1].split('.')[:2])
if parts < (3,28): raise SystemExit(f"CMake 3.28+ is required; found {sys.argv[1]}")
PYVER

validate_vcpkg() {
  local path="$1"
  [[ -d "$path/.git" ]] || return 1
  [[ -f "$path/versions/baseline.json" ]] || return 1
  git -C "$path" cat-file -e "${VCPKG_BASELINE}:versions/baseline.json" 2>/dev/null
}

if [[ "$USE_EXTERNAL_VCPKG" == "1" || "${ENGCODER_USE_EXTERNAL_VCPKG:-0}" == "1" ]]; then
  : "${VCPKG_ROOT:?external vcpkg requested, but VCPKG_ROOT is not set}"
  validate_vcpkg "$VCPKG_ROOT" || { echo "External VCPKG_ROOT cannot resolve pinned baseline." >&2; exit 1; }
else
  VCPKG_ROOT="$ROOT/.deps/vcpkg"
  if [[ -e "$VCPKG_ROOT" ]] && ! validate_vcpkg "$VCPKG_ROOT"; then
    echo "Repairing invalid project-local vcpkg checkout: $VCPKG_ROOT"
    rm -rf "$VCPKG_ROOT"
  fi
  if [[ ! -d "$VCPKG_ROOT/.git" ]]; then
    mkdir -p "$ROOT/.deps" "$VCPKG_ROOT"
    git -C "$VCPKG_ROOT" init --quiet
    git -C "$VCPKG_ROOT" remote add origin https://github.com/microsoft/vcpkg.git
  fi
  if ! validate_vcpkg "$VCPKG_ROOT"; then
    git -C "$VCPKG_ROOT" fetch --force --depth 1 origin "$VCPKG_BASELINE"
    git -C "$VCPKG_ROOT" checkout --quiet --detach "$VCPKG_BASELINE"
  fi
  validate_vcpkg "$VCPKG_ROOT" || { echo "Pinned vcpkg checkout is invalid." >&2; exit 1; }
  "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi
export VCPKG_ROOT

case "$(uname -s)" in
  Linux) PRESET="linux" ;;
  Darwin) PRESET="macos" ;;
  *) echo "Unsupported platform: $(uname -s)" >&2; exit 1 ;;
esac

CONFIG_LOWER="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
BUILD_PRESET="${PRESET}-${CONFIG_LOWER}"
BUILD_DIR="$ROOT/out/build/$PRESET"
BIN_DIR="$BUILD_DIR/$CONFIG"

printf 'CMake:          %s (%s)\n' "$(command -v cmake)" "$CMAKE_VERSION"
printf 'Native project: %s\n' "$NATIVE_ROOT"
printf 'Web project:    %s\n' "$WEB_ROOT"
printf 'Generator:      %s (Ninja Multi-Config)\n' "$PRESET"
printf 'vcpkg:          %s\n' "$VCPKG_ROOT"
printf 'vcpkg tag:      %s\n' "$VCPKG_TAG"
printf 'Configuration:  %s\n' "$CONFIG"
printf 'Client:         %s\n' "$CLIENT"

(
  cd "$NATIVE_ROOT"
  cmake --preset "$PRESET"
  echo "Cleaning generated targets..."
  cmake --build --preset "$BUILD_PRESET" --target clean --parallel
  build_targets=(engcoder_server engcoder_gui engcoder_cli)
  if [[ "$RUN_TESTS" == "1" ]]; then build_targets+=(engcoder_tests); fi
  printf 'Build targets:   %s\n' "${build_targets[*]}"
  for target in "${build_targets[@]}"; do
    case "$target" in
      engcoder_server) artifact="$BIN_DIR/EngCoderServer" ;;
      engcoder_gui) artifact="$BIN_DIR/EngCoder" ;;
      engcoder_cli) artifact="$BIN_DIR/EngCoderCLI" ;;
      *) artifact="$BIN_DIR/$target" ;;
    esac
    rm -f "$artifact"
    echo "Building $target..."
    cmake --build --preset "$BUILD_PRESET" --target "$target" --parallel
    [[ -f "$artifact" ]] || { echo "Build $target returned success but did not produce $artifact" >&2; exit 1; }
    echo "Built $target: $artifact"
  done
  if [[ "$RUN_TESTS" == "1" ]]; then ctest --preset "$BUILD_PRESET"; fi
)

echo "Build completed successfully."
echo "Build tree: $BUILD_DIR"

required_artifacts=("$BIN_DIR/EngCoderServer" "$BIN_DIR/EngCoder" "$BIN_DIR/EngCoderCLI")
if [[ "$RUN_TESTS" == "1" ]]; then required_artifacts+=("$BIN_DIR/engcoder_tests"); fi
echo "Verified build artifacts:"
for artifact in "${required_artifacts[@]}"; do
  [[ -f "$artifact" ]] || { echo "Expected build artifact is missing: $artifact" >&2; exit 1; }
  if stat --version >/dev/null 2>&1; then
    stat -c '  %n  %s bytes  %y' "$artifact"
  else
    stat -f '  %N  %z bytes  %Sm' "$artifact"
  fi
done

if [[ "$USE_EXTERNAL_VCPKG" != "1" && "${ENGCODER_USE_EXTERNAL_VCPKG:-0}" != "1" && "$KEEP_DEPENDENCIES" != "1" ]]; then
  echo "Removing transient project-local vcpkg checkout/cache..."
  rm -rf "$ROOT/.deps/vcpkg"
  rmdir "$ROOT/.deps" 2>/dev/null || true
  unset VCPKG_ROOT
  echo "Transient dependency checkout removed."
  echo "Keeping $BUILD_DIR/vcpkg_installed because the generated build tree still uses it."
fi

if [[ "$RUN_APP" != "1" ]]; then
  echo "Run skipped (--no-run)."
  exit 0
fi

SERVER="$BIN_DIR/EngCoderServer"
CLI="$BIN_DIR/EngCoderCLI"
GUI="$BIN_DIR/EngCoder"
for required in "$SERVER" "$CLI" "$GUI"; do
  [[ -x "$required" ]] || { echo "Built application not found: $required" >&2; exit 1; }
done

RUN_DIR="$ROOT/.engcoder/run"
PID_FILE="$RUN_DIR/engcoder_server.pid"
PATH_FILE="$RUN_DIR/engcoder_server.path"
STDOUT_LOG="$RUN_DIR/engcoder_server.stdout.log"
STDERR_LOG="$RUN_DIR/engcoder_server.stderr.log"
GUI_STDOUT_LOG="$RUN_DIR/engcoder_gui.stdout.log"
GUI_STDERR_LOG="$RUN_DIR/engcoder_gui.stderr.log"
mkdir -p "$RUN_DIR"

# Native GUI/CLI now own their runtime in-process. Stop a prior standalone
# server recorded by auto.sh so stale daemon state never survives a mode switch.
if [[ -f "$PID_FILE" && -f "$PATH_FILE" ]]; then
  previous_pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  previous_path="$(cat "$PATH_FILE" 2>/dev/null || true)"
  if [[ "$previous_pid" =~ ^[0-9]+$ ]] && kill -0 "$previous_pid" 2>/dev/null; then
    if [[ "$previous_path" == "$SERVER" || "$previous_path" == *EngCoder-*/*EngCoderServer ]]; then
      echo "Stopping previous standalone EngCoder server (PID $previous_pid)..."
      kill "$previous_pid" 2>/dev/null || true
      for _ in {1..30}; do kill -0 "$previous_pid" 2>/dev/null || break; sleep 0.1; done
      kill -9 "$previous_pid" 2>/dev/null || true
    fi
  fi
  rm -f "$PID_FILE" "$PATH_FILE"
fi

export ENGCODER_WORKSPACE="$ROOT"
export ENGCODER_WEB_ROOT="$WEB_ROOT"

if [[ "$CLIENT" == "cli" || "$CLIENT" == "web" || "$CLIENT" == "server" ]]; then
  echo "Running model-backend doctor..."
  set +e
  "$CLI" --doctor
  DOCTOR_STATUS=$?
  set -e
  if [[ "$DOCTOR_STATUS" -ne 0 ]]; then
    echo "Backend doctor exited with code $DOCTOR_STATUS. Client launch will continue." >&2
  fi
elif [[ "$CLIENT" == "gui" ]]; then
  echo "GUI mode: backend authentication can be entered inside EngCoder; skipping pre-launch doctor."
fi

start_standalone_server() {
  if [[ -n "${ENGCODER_SERVER_PORT:-}" ]]; then
    PORT="$ENGCODER_SERVER_PORT"
  else
    PORT="14321"
    export ENGCODER_SERVER_PORT="$PORT"
  fi
  RUNTIME_URL="http://127.0.0.1:$PORT"
  UI_URL="$RUNTIME_URL/ui/"
  export ENGCODER_AGENT_URL="$RUNTIME_URL"

  echo "Launching standalone EngCoder server..."
  "$SERVER" --workspace "$ROOT" --web-root "$WEB_ROOT" --port "$PORT" >"$STDOUT_LOG" 2>"$STDERR_LOG" &
  SERVER_PID=$!
  printf '%s' "$SERVER_PID" > "$PID_FILE"
  printf '%s' "$SERVER" > "$PATH_FILE"

  if ! python3 - "$RUNTIME_URL/health" <<'PYWAIT'
import sys,time,urllib.request
url=sys.argv[1]
deadline=time.time()+12
while time.time()<deadline:
    try:
        with urllib.request.urlopen(url, timeout=1) as r:
            if r.status == 200: raise SystemExit(0)
    except Exception: pass
    time.sleep(.15)
raise SystemExit(1)
PYWAIT
  then
    echo "EngCoder did not become healthy. See $STDERR_LOG" >&2
    exit 1
  fi
  echo "Standalone server ready (PID $SERVER_PID): $RUNTIME_URL"
}

case "$CLIENT" in
  gui)
    if [[ "$(uname -s)" == "Linux" && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
      echo "No graphical Linux session detected; falling back to embedded CLI."
      exec "$CLI"
    fi
    echo "Launching native EngCoder GUI with embedded runtime..."
    : >"$GUI_STDOUT_LOG"
    : >"$GUI_STDERR_LOG"
    "$GUI" >"$GUI_STDOUT_LOG" 2>"$GUI_STDERR_LOG" &
    GUI_PID=$!
    sleep 0.6
    if ! kill -0 "$GUI_PID" 2>/dev/null; then
      wait "$GUI_PID" || GUI_STATUS=$?
      GUI_STATUS="${GUI_STATUS:-1}"
      echo "EngCoder exited immediately with code $GUI_STATUS." >&2
      [[ -s "$GUI_STDERR_LOG" ]] && cat "$GUI_STDERR_LOG" >&2
      exit "$GUI_STATUS"
    fi
    echo "EngCoder GUI started (PID $GUI_PID). No standalone server is running."
    echo "Use Start server / Open Web UI inside the GUI when HTTP/web access is wanted."
    ;;
  cli)
    echo "Starting interactive EngCoder CLI with embedded runtime..."
    exec "$CLI"
    ;;
  web)
    start_standalone_server
    echo "Opening EngCoder web client: $UI_URL"
    if [[ "$(uname -s)" == "Darwin" ]]; then open "$UI_URL"; else xdg-open "$UI_URL" >/dev/null 2>&1 || echo "Open $UI_URL manually."; fi
    ;;
  server)
    start_standalone_server
    echo "Standalone runtime: $RUNTIME_URL"
    echo "Web UI: $UI_URL"
    ;;
  none)
    echo "Run skipped: build artifacts only; no server or client launched."
    ;;
esac
