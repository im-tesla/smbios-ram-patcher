#!/bin/bash
set -e

# Always run from the project root
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Check for gnu-efi dependencies
if [ ! -d "/usr/include/efi" ] || [ ! -f "/usr/lib/crt0-efi-x86_64.o" ]; then
    echo "[ERROR] Missing gnu-efi build dependencies."
    echo "        Please install them via:"
    echo "        sudo pacman -S gnu-efi base-devel cmake gcc"
    exit 1
fi

if command -v cmake >/dev/null 2>&1; then
    echo "[*] Building with CMake..."
    rm -rf ./build
    mkdir -p ./build
    cd ./build
    cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 ..
    make
else
    echo "[*] cmake not found, building directly with Makefile..."
    make clean
    make
fi