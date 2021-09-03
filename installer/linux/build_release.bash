#!/usr/bin/env bash

# http://redsymbol.net/articles/unofficial-bash-strict-mode/

set -euo pipefail
IFS=$'\n\t'

cd "$(dirname "$0")"

if [[ $# -ge 1 ]]; then
    if [[ "$1" == "clean" ]]; then
        echo >&2 "==> Cleaning..."
        rm -rf "build"
        rm -rf "odasrv.AppDir"
        exit 0
    else
        echo >&2 "Unknown parameter."; exit 1
    fi
fi

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

# Download AppImage

APPIMAGE_BIN="appimagetool-x86_64.AppImage"

if [[ ! -f "$APPIMAGE_BIN" ]]; then
    curl -LO "https://github.com/AppImage/AppImageKit/releases/download/13/${APPIMAGE_BIN}"
fi

chmod +x "$APPIMAGE_BIN"

# Build Odamex.

if [[ ! -d "build" ]]; then
    echo >&2 "==> Building Odamex..."

    mkdir -p "build"

    cmake -S "../.." -B "build" -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo

    cmake --build "build"
fi

# Assemble the client AppDir.

if [[ ! -d "odamex.AppDir" ]]; then
    echo >&2 "==> Assembling odamex AppDir..."

    mkdir -p "odamex.AppDir/usr/bin"
    mkdir -p "odamex.AppDir/usr/lib"
    mkdir -p "odamex.AppDir/usr/share/applications"
    mkdir -p "odamex.AppDir/usr/share/icons/hicolor/96x96/apps"
    mkdir -p "odamex.AppDir/usr/share/icons/hicolor/128x128/apps"
    mkdir -p "odamex.AppDir/usr/share/icons/hicolor/256x256/apps"
    mkdir -p "odamex.AppDir/usr/share/icons/hicolor/512x512/apps"

    cp -v "build/client/odamex" "odamex.AppDir/usr/bin"
    cp -v "build/wad/odamex.wad" "odamex.AppDir/usr/bin"
    cp -v "odamex.desktop" "odamex.AppDir/usr/share/applications"
    cp -v "../../media/icon_odamex_96.png" \
        "odamex.AppDir/usr/share/icons/hicolor/96x96/apps/odamex.png"
    cp -v "../../media/icon_odamex_128.png" \
        "odamex.AppDir/usr/share/icons/hicolor/128x128/apps/odamex.png"
    cp -v "../../media/icon_odamex_256.png" \
        "odamex.AppDir/usr/share/icons/hicolor/256x256/apps/odamex.png"
    cp -v "../../media/icon_odamex_512.png" \
        "odamex.AppDir/usr/share/icons/hicolor/512x512/apps/odamex.png"

    ln -sv "usr/bin/odamex" "odamex.AppDir/AppRun"
    ln -sv "usr/share/applications/odamex.desktop" "odamex.AppDir/odamex.desktop"
    ln -sv "usr/share/icons/hicolor/512x512/apps/odamex.png" "odamex.AppDir/.DirIcon"
    ln -sv "usr/share/icons/hicolor/512x512/apps/odamex.png" "odamex.AppDir/odamex.png"

    "./$APPIMAGE_BIN" "odamex.AppDir"
fi

# Assemble the server AppDir.

if [[ ! -d "odasrv.AppDir" ]]; then
    echo >&2 "==> Assembling odasrv AppDir..."

    mkdir -p "odasrv.AppDir/usr/bin"
    mkdir -p "odasrv.AppDir/usr/lib"
    mkdir -p "odasrv.AppDir/usr/share/applications"
    mkdir -p "odasrv.AppDir/usr/share/icons/hicolor/96x96/apps"
    mkdir -p "odasrv.AppDir/usr/share/icons/hicolor/128x128/apps"
    mkdir -p "odasrv.AppDir/usr/share/icons/hicolor/256x256/apps"
    mkdir -p "odasrv.AppDir/usr/share/icons/hicolor/512x512/apps"

    cp -v "build/server/odasrv" "odasrv.AppDir/usr/bin"
    cp -v "build/wad/odamex.wad" "odasrv.AppDir/usr/bin"
    cp -v "odasrv.desktop" "odasrv.AppDir/usr/share/applications"
    cp -v "../../media/icon_odasrv_96.png" \
        "odasrv.AppDir/usr/share/icons/hicolor/96x96/apps/odasrv.png"
    cp -v "../../media/icon_odasrv_128.png" \
        "odasrv.AppDir/usr/share/icons/hicolor/128x128/apps/odasrv.png"
    cp -v "../../media/icon_odasrv_256.png" \
        "odasrv.AppDir/usr/share/icons/hicolor/256x256/apps/odasrv.png"
    cp -v "../../media/icon_odasrv_512.png" \
        "odasrv.AppDir/usr/share/icons/hicolor/512x512/apps/odasrv.png"

    ln -sv "usr/bin/odasrv" "odasrv.AppDir/AppRun"
    ln -sv "usr/share/applications/odasrv.desktop" "odasrv.AppDir/odasrv.desktop"
    ln -sv "usr/share/icons/hicolor/512x512/apps/odasrv.png" "odasrv.AppDir/.DirIcon"
    ln -sv "usr/share/icons/hicolor/512x512/apps/odasrv.png" "odasrv.AppDir/odasrv.png"

    "./$APPIMAGE_BIN" "odasrv.AppDir"
fi
