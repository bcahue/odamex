mkdir build | Out-Null
cd build
Start-FileDownload "https://www.libsdl.org/release/SDL2-devel-2.0.5-VC.zip"
7z x "SDL2-devel-2.0.5-VC.zip"
Start-FileDownload "https://www.libsdl.org/projects/SDL_mixer/release/SDL2_mixer-devel-2.0.1-VC.zip"
7z x "SDL2_mixer-devel-2.0.1-VC.zip"
Start-FileDownload "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.3/wxWidgets-3.1.3.zip"
7z x "wxWidgets-3.1.3.zip"
& "C:\Program Files (x86)\cmake\bin\cmake.exe" .\wxWidgets-3.1.3 -G "MinGW Makefiles" -DwxBUILD_COMPATIBILITY="2.8" -DCMAKE_BUILD_TYPE=Release -DwxBUILD_SHARED=False
& "C:\Program Files (x86)\cmake\bin\cmake.exe" --build .\wxWidgets-3.1.3
& "C:\Program Files (x86)\cmake\bin\cmake.exe" .. -G "MinGW Makefiles" -DUSE_MINIUPNP=False -DSDL2_INCLUDE_DIR="./SDL2-2.0.5/include/" -DSDL2_LIBRARY="./SDL2-2.0.5/lib/x86/SDL2.lib" -DSDL2MAIN_LIBRARY="./lib/x86/SDL2main.lib" -DSDL2_MIXER_INCLUDE_DIR="./SDL2_mixer-2.0.1/include/" -DSDL2_MIXER_LIBRARY="./SDL2_mixer-2.0.1/lib/x86/SDL2_mixer.lib" -DUSE_STATIC_STDLIB=True -DwxWidgets_wxrc_EXECUTABLE="./wxWidgets-3.1.3/lib/gcc_lib/wxrc.exe"
& "C:\Program Files (x86)\cmake\bin\cmake.exe" --build .\odalaunch
rm .\CMakeCache.txt
& "C:\Program Files (x86)\cmake\bin\cmake.exe" .. -G "Visual Studio 14 2015" -DUSE_MINIUPNP=False -DSDL2_INCLUDE_DIR="./SDL2-2.0.5/include/" -DSDL2_LIBRARY="./SDL2-2.0.5/lib/x86/SDL2.lib" -DSDL2MAIN_LIBRARY="./lib/x86/SDL2main.lib" -DSDL2_MIXER_INCLUDE_DIR="./SDL2_mixer-2.0.1/include/" -DSDL2_MIXER_LIBRARY="./SDL2_mixer-2.0.1/lib/x86/SDL2_mixer.lib"
cd ..