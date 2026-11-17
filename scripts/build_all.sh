#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JUCE_DIR="${JUCE_DIR:-$ROOT/JUCE}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

command -v cmake >/dev/null 2>&1 || { echo "ERROR: install CMake 3.22+ first." >&2; exit 2; }
command -v git >/dev/null 2>&1 || { echo "ERROR: Git is required to fetch JUCE." >&2; exit 2; }
cmake --version | head -n1 | grep -Eq 'version 3\.(2[2-9]|[3-9][0-9])|version [4-9]' || { echo "ERROR: CMake 3.22+ is required." >&2; exit 2; }
if [[ ! -f "$JUCE_DIR/CMakeLists.txt" ]]; then
  echo "Fetching official JUCE 8.0.4 into: $JUCE_DIR"
  git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
fi
cmake -S "$ROOT" -B "$ROOT/build-protocol" -DOCTIVIER_PROTOCOL_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build-protocol" --parallel 2
ctest --test-dir "$ROOT/build-protocol" -C Release --output-on-failure
cmake -S "$ROOT" -B "$BUILD_DIR" "-DJUCE_DIR=$JUCE_DIR" -DCMAKE_BUILD_TYPE=Release -DOCTIVIER_BUILD_TESTS=ON
cmake --build "$BUILD_DIR" --config Release --parallel 2
ctest --test-dir "$BUILD_DIR" -C Release --output-on-failure
