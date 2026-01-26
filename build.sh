#!/bin/bash

# **********************************************************************
# * Copyright (C) 2017-2025 MX Authors
# *
# * Authors: Adrian
# *          Dolphin_Oracle
# *          MX Linux <http://mxlinux.org>
# *
# * This file is part of mx-packageinstaller.
# *
# * mx-packageinstaller is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# *
# * mx-packageinstaller is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with mx-packageinstaller.  If not, see <http://www.gnu.org/licenses/>.
# **********************************************************************/

set -e

# Default values
BUILD_DIR="build"
BUILD_TYPE="Release"
USE_CLANG=false
CLEAN=false
ARCH_BUILD=false
BUILD_TESTS=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -c|--clang)
            USE_CLANG=true
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --arch)
            ARCH_BUILD=true
            shift
            ;;
        -t|--tests)
            BUILD_TESTS=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  -d, --debug     Build in Debug mode (default: Release)"
            echo "  -c, --clang     Use clang compiler"
            echo "  -t, --tests     Build with unit tests"
            echo "  --clean         Clean build directory before building"
            echo "  --arch          Build Arch Linux package"
            echo "  -h, --help      Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Build Arch Linux package
if [ "$ARCH_BUILD" = true ]; then
    echo "Building Arch Linux package..."

    if ! command -v makepkg &> /dev/null; then
        echo "Error: makepkg not found. Please install base-devel package."
        exit 1
    fi

    if [ ! -f PKGBUILD ]; then
        echo "Error: PKGBUILD not found; cannot determine version for Arch build."
        exit 1
    fi
    PKGVER_LINE=$(sed -n 's/^pkgver=//p' PKGBUILD | head -n 1)
    PKGREL=$(sed -n 's/^pkgrel=//p' PKGBUILD | head -n 1)
    if [ -z "$PKGVER_LINE" ]; then
        echo "Error: could not parse pkgver from PKGBUILD."
        exit 1
    fi
    if [[ "$PKGVER_LINE" =~ ^\$\{PKGVER:-([^}]+)\}$ ]]; then
        PKGVER="${BASH_REMATCH[1]}"
    else
        PKGVER="$PKGVER_LINE"
    fi
    if [ -n "$PKGREL" ]; then
        ARCH_VERSION="${PKGVER}-${PKGREL}"
    else
        ARCH_VERSION="${PKGVER}"
    fi
    echo "Using version ${ARCH_VERSION} from PKGBUILD"

    ARCH_BUILDDIR=$(mktemp -d -p "$PWD" archpkgbuild.XXXXXX)
    trap 'rm -rf "$ARCH_BUILDDIR"' EXIT

    rm -rf pkg *.pkg.tar.zst

    PKG_DEST_DIR="$PWD/build"
    mkdir -p "$PKG_DEST_DIR"

    PKGDEST="$PKG_DEST_DIR" PKGVER="$PKGVER" PKGREL="$PKGREL" makepkg -f

    echo "Cleaning makepkg artifacts..."
    rm -rf pkg

    echo "Arch Linux package build completed!"
    echo "Package: $(ls *.pkg.tar.zst 2>/dev/null || echo 'not found')"
    echo "Binary available at: build/mx-packageinstaller"
    exit 0
fi

# Clean build directory if requested
if [ "$CLEAN" = true ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    rm -f translations/*.qm version.h
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Set up CMake arguments
CMAKE_ARGS=(
    -S .
    -B "$BUILD_DIR"
    -G Ninja
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
)

if [ "$USE_CLANG" = true ]; then
    CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER=clang++)
    CMAKE_ARGS+=(-DCMAKE_C_COMPILER=clang)
fi

if [ "$BUILD_TESTS" = true ]; then
    CMAKE_ARGS+=(-DBUILD_TESTING=ON)
fi

# Configure CMake with Ninja (skip if already configured)
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Configuring CMake with Ninja generator..."
    cmake "${CMAKE_ARGS[@]}"
else
    echo "CMake already configured, skipping configure step..."
fi

# Build the project (Ninja will automatically detect source changes)
echo "Building project with Ninja..."
cmake --build "$BUILD_DIR" --parallel

echo "Build completed successfully!"
echo "Executable: $BUILD_DIR/mx-packageinstaller"

# Run tests if built with tests
if [ "$BUILD_TESTS" = true ]; then
    echo "Running tests..."
    cd "$BUILD_DIR" && ctest --verbose
    cd - > /dev/null
fi
