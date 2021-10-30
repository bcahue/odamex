#!/usr/bin/env bash

# http://redsymbol.net/articles/unofficial-bash-strict-mode/

set -euo pipefail
IFS=$'\n\t'

export VERSION="10.0.0"

APPIMAGE_BIN="appimagetool-x86_64.AppImage"
LINUXDEPLOY_BIN="linuxdeploy-x86_64.AppImage"

cd "$(dirname "$0")"

function precheck()
{
    hash c++ 2>/dev/null || {
        echo >&2 "A compiler is required to generate the installer."; exit 1
    }
    hash cmake 2>/dev/null || {
        echo >&2 "CMake is required to generate the installer."; exit 1
    }
    hash ninja 2>/dev/null || {
        echo >&2 "Ninja is required to generate the installer."; exit 1
    }
    hash deutex 2>/dev/null || {
        echo >&2 "Deutex is required to generate the installer."; exit 1
    }
}

function download() 
{
    if [[ ! -f "$APPIMAGE_BIN" ]]; then
        curl -LO "https://github.com/AppImage/AppImageKit/releases/download/13/${APPIMAGE_BIN}"
    fi

    chmod +x "$APPIMAGE_BIN"

    if [[ ! -f "$LINUXDEPLOY_BIN" ]]; then
        curl -LO "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/${LINUXDEPLOY_BIN}"
    fi

    chmod +x "$LINUXDEPLOY_BIN"
}

function build()
{
    if [[ ! -d "build" ]]; then
        echo >&2 "==> Building Odamex..."

        mkdir -p "build"

        cmake -S "../.." -B "build" -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_LAUNCHER=0 -DBUILD_OR_FAIL=1

        cmake --build "build"
    fi
}

function odamex_image()
{
    echo >&2 "==> Assembling odamex image..."

    if [[ ! -d "odamex.AppDir" ]]; then
        mkdir -p "odamex.AppDir/usr/bin"

        cp -v "build/wad/odamex.wad" "odamex.AppDir/usr/bin"

        "./${LINUXDEPLOY_BIN}" \
            --executable="build/client/odamex" \
            --desktop-file="odamex.desktop" \
            --icon-file="../../media/icon_odamex_96.png" \
            --icon-file="../../media/icon_odamex_128.png" \
            --icon-file="../../media/icon_odamex_256.png" \
            --icon-file="../../media/icon_odamex_512.png" \
            --icon-filename="odamex" \
            --output=appimage \
            --appdir="odamex.AppDir"
    fi
}

function odasrv_image()
{
    echo >&2 "==> Assembling odasrv image..."

    if [[ ! -d "odasrv.AppDir" ]]; then
        mkdir -p "odasrv.AppDir/usr/bin"

        cp -v "build/wad/odamex.wad" "odasrv.AppDir/usr/bin"

        "./${LINUXDEPLOY_BIN}" \
            --executable="build/server/odasrv" \
            --desktop-file="odasrv.desktop" \
            --icon-file="../../media/icon_odasrv_96.png" \
            --icon-file="../../media/icon_odasrv_128.png" \
            --icon-file="../../media/icon_odasrv_256.png" \
            --icon-file="../../media/icon_odasrv_512.png" \
            --icon-filename="odasrv" \
            --output=appimage \
            --appdir="odasrv.AppDir"
    fi
}

# Check for "clean" parameter
if [[ $# -ge 1 ]]; then
    if [[ "$1" == "clean" ]]; then
        echo >&2 "==> Cleaning..."
        rm -rf "build"
        rm -rf "odamex.AppDir"
        rm -rf "odasrv.AppDir"
        exit 0
    else
        echo >&2 "Unknown parameter."; exit 1
    fi
fi

precheck
download
build
odamex_image
odasrv_image
