#!/bin/bash
# setup.sh — downloads the stb headers required before building.
# Run this once after cloning the repo.

set -e

STB_DIR="third_party/stb"
mkdir -p "$STB_DIR"

echo "Downloading stb headers..."

curl -fsSL -o "$STB_DIR/stb_image_write.h" \
    "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"

curl -fsSL -o "$STB_DIR/stb_image.h" \
    "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"

echo "Done. stb headers installed to $STB_DIR/"
echo ""
echo "Next steps:"
echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build build --parallel"
