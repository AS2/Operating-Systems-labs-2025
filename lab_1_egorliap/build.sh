#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="$SCRIPT_DIR/build"
BIN_NAME="lab1_daemon"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

g++ -std=c++17 -Wall -Werror \
  "$SCRIPT_DIR/main.cpp" \
  "$SCRIPT_DIR/daemon.cpp" \
  "$SCRIPT_DIR/tasks.cpp" \
  -o "$BUILD_DIR/$BIN_NAME"

echo "Built $BUILD_DIR/$BIN_NAME"


