mkdir build | Out-Null
cd build

if (!(Test-Path -Path "SDL2-devel-2.0.12-VC.zip" -PathType leaf)) {
    Start-FileDownload "https://www.libsdl.org/release/SDL2-devel-2.0.12-VC.zip"
}
if (!(Test-Path -Path "SDL2-2.0.12")) {
    7z x "SDL2-devel-2.0.12-VC.zip"
}

if (!(Test-Path -Path "SDL2_mixer-devel-2.0.4-VC.zip" -PathType leaf)) {
    Start-FileDownload "https://www.libsdl.org/projects/SDL_mixer/release/SDL2_mixer-devel-2.0.4-VC.zip"
}
if (!(Test-Path -Path "SDL2_mixer-2.0.4")) {
    7z x "SDL2_mixer-devel-2.0.4-VC.zip"
}

if (!(Test-Path -Path "wxWidgets-3.1.3-headers.7z" -PathType leaf)) {
    Start-FileDownload "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.3/wxWidgets-3.1.3-headers.7z"
}
if (!(Test-Path -Path "wxMSW-3.1.3_vc14x_x64_Dev.7z" -PathType leaf)) {
    Start-FileDownload "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.3/wxMSW-3.1.3_vc14x_x64_Dev.7z"
}
if (!(Test-Path -Path "wxMSW-3.1.3_vc14x_x64_ReleaseDLL.7z" -PathType leaf)) {
    Start-FileDownload "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.3/wxMSW-3.1.3_vc14x_x64_ReleaseDLL.7z"
}

if (!(Test-Path -Path "wxMSW")) {
    mkdir wxMSW | Out-Null
}

if (!(Test-Path -Path "wxMSW\include")) {
    7z x "wxWidgets-3.1.3-headers.7z" -o:wxMSW
}
if (!(Test-Path -Path "wxMSW\build")) {
    7z x "wxMSW-3.1.3_vc14x_x64_Dev.7z" -o:wxMSW
}
if (!(Test-Path -Path "wxMSW\lib\vc14x_x64_dll\wxbase313u_vc14x_x64.dll" -PathType leaf)) {
    7z x "wxMSW-3.1.3_vc14x_x64_ReleaseDLL.7z" -o:wxMSW
}

& "C:\Program Files (x86)\cmake\bin\cmake.exe" .. `
    -G "Visual Studio 14 2015" -T v140_xp `
    -DUSE_MINIUPNP=False `
    -DSDL2_INCLUDE_DIR="./SDL2-2.0.12/include/" -DSDL2_LIBRARY="./SDL2-2.0.12/lib/x86/SDL2.lib" -DSDL2MAIN_LIBRARY="./lib/x86/SDL2main.lib" `
    -DSDL2_MIXER_INCLUDE_DIR="./SDL2_mixer-2.0.4/include/" -DSDL2_MIXER_LIBRARY="./SDL2_mixer-2.0.4/lib/x86/SDL2_mixer.lib" `
    -DwxWidgets_ROOT_DIR="./wxMSW" -DwxWidgets_wxrc_EXECUTABLE="./wxMSW/lib/vc14x_x64_dll/wxrc.exe"

cd ..
