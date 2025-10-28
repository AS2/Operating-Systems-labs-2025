#!/bin/bash

set -e # Exit on any error

# Load environment variables
if [ -f .env ]; then
    export $(cat .env | grep -v '^#' | xargs)
fi

# Set defaults if not defined
BUILD_DIR=${BUILD_DIR:-build}
TARGET=${TARGET:-lab1}

echo "=== Building $TARGET with CMake ==="

# Clean previous build
if [ -d "$BUILD_DIR" ]; then
  echo "Cleaning previous build..."
  rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake - flags are defined in CMakeLists.txt
echo "Configuring CMake (flags from CMakeLists.txt)"
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the project
echo "Building..."
cmake --build . --parallel $(nproc)

# Verify the binary was created
if [ -f "bin/$TARGET" ]; then
  echo "=== Build successful ==="
  echo "Binary: bin/$TARGET"
  echo "Size: $(du -h "bin/$TARGET" | cut -f1)"

  # Display build info
  echo "=== Build Information ==="
  echo "C++ standard: C++17"
  echo "Warning flags: -Wall -Werror (from CMakeLists.txt)"

  # Run the program
#  echo "=== Testing executable ==="
#  ./bin/$TARGET
else
  echo "Error: Binary not created at bin/$TARGET"
  exit 1
fi

# Clean up: CMake doesn't create intermediate files in source dir
echo "=== Cleanup complete ==="
echo "No intermediate files in source directory"