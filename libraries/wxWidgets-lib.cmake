### wxWidgets ###

if(BUILD_LAUNCHER)
  if(USE_INTERNAL_WXWIDGETS)
    # The launcher's global-chat view renders in a wxWebView. The official
    # prebuilt wxWidgets binaries are compiled with wxUSE_WEBVIEW_EDGE=0, so they
    # only provide the legacy IE backend (no colour emoji / modern CSS). To get
    # the Edge (Chromium) backend we build wxWidgets from source by default on
    # MSVC. Flip WXWIDGETS_PREBUILT ON to go back to the prebuilt download --
    # e.g. once upstream starts distributing Edge-enabled binaries.
    option(WXWIDGETS_PREBUILT
      "Use the official prebuilt wxWidgets binaries instead of building from source (the prebuilt binaries currently lack the Edge WebView backend)."
      OFF)
    set(WXWIDGETS_SOURCE_DIR "" CACHE PATH
      "Path to a local wxWidgets source tree (with its submodules) to build from. If empty, the source release is downloaded. Only used when WXWIDGETS_PREBUILT is OFF.")
    set(WXWIDGETS_BUILD_CONFIGS "Debug;Release" CACHE STRING
      "wxWidgets configurations to build from source (VS is multi-config). Narrow this while iterating to halve the build time.")
    set(WXWIDGETS_SOURCE_URL
      "https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxWidgets-3.3.2.7z"
      CACHE STRING "URL of the wxWidgets source archive (used when WXWIDGETS_SOURCE_DIR is empty). The release .7z bundles the 3rd-party submodules.")
    set(WXWIDGETS_SOURCE_SHA256 "" CACHE STRING
      "Optional SHA256 of the source archive; set it for reproducible/CI builds.")

    if(WIN32)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets")
      set(wxWidgets_ROOT_DIR "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets" CACHE PATH "")

      if(MSVC_VERSION GREATER_EQUAL 1900)
        # Visual Studio 2015/2017/2019/2022
        if(WXWIDGETS_PREBUILT)
          # --- Prebuilt download (no Edge backend) ---
          if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            file(DOWNLOAD
              "https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxWidgets-3.3.2-headers.7z"
              "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.3.2-headers.7z"
              EXPECTED_HASH SHA256=6d0c866b4a4612f6a667194d1574049639181181f29dd6abceb50c5e5b2baa29)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf
              "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.3.2-headers.7z"
              WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets")
            file(DOWNLOAD
              "https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxMSW-3.3.2_vc14x_x64_Dev.7z"
              "${CMAKE_CURRENT_BINARY_DIR}/wxMSW-3.3.2_vc14x_x64_Dev.7z"
              EXPECTED_HASH SHA256=6ef8686e8c0a9f466a6940dd0a52e09ead25bd3ee38733af6085762632d06c5d)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf
              "${CMAKE_CURRENT_BINARY_DIR}/wxMSW-3.3.2_vc14x_x64_Dev.7z"
              WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets")
            file(DOWNLOAD
              "https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxMSW-3.3.2_vc14x_x64_ReleaseDLL.7z"
              "${CMAKE_CURRENT_BINARY_DIR}/wxMSW-3.3.2_vc14x_x64_ReleaseDLL.7z"
              EXPECTED_HASH SHA256=7e7efa327e0f9dcb95b90069b7aa9fe7a6a764210bfb58f71b06e6c26255437b)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf
              "${CMAKE_CURRENT_BINARY_DIR}/wxMSW-3.3.2_vc14x_x64_ReleaseDLL.7z"
              WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets")

            set(wxWidgets_wxrc_EXECUTABLE
              "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets/lib/vc14x_x64_dll/wxrc.exe"
              CACHE FILEPATH "")
          else()
            file(DOWNLOAD
              "https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.10/wxWidgets-3.2.10-headers.7z"
              "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.2.10-headers.7z"
              EXPECTED_HASH SHA256=f50af8b5415edb42ad223ee127fb48658a1c189e73b6bc678ab350e7396a18bf)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf
              "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.2.10-headers.7z"
              WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets")
            file(DOWNLOAD
              "https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.10/wxMSW-3.2.10_vc14x_Dev.7z"
              "${CMAKE_CURRENT_BINARY_DIR}/wxMSW-3.2.10_vc14x_Dev.7z"
              EXPECTED_HASH SHA256=e89a1a1ce701a4b0194b3ba4635ef03e0106ec5b83e9cc9a957822337fba56a1)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf
              "${CMAKE_CURRENT_BINARY_DIR}/wxMSW-3.2.10_vc14x_Dev.7z"
              WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets")
            file(DOWNLOAD
              "https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.10/wxMSW-3.2.10_vc14x_ReleaseDLL.7z"
              "${CMAKE_CURRENT_BINARY_DIR}/wxMSW-3.2.10_vc14x_ReleaseDLL.7z"
              EXPECTED_HASH SHA256=79fc5c3edbbbb670d3d9213a4530c90a73eed0cfa8635522c76c954fca5b80c5)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf
              "${CMAKE_CURRENT_BINARY_DIR}/wxMSW-3.2.10_vc14x_ReleaseDLL.7z"
              WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets")

            set(wxWidgets_wxrc_EXECUTABLE
              "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets/lib/vc14x_dll/wxrc.exe"
              CACHE FILEPATH "")
          endif()
        else()
          # --- Build from source (gets the Edge WebView backend) ---
          # wx's own CMake emits the "vc" compiler prefix (not the release
          # builds' special "vc14x") and lays Debug+Release artifacts side by
          # side in one lib dir, distinguished by the ud/u in the file names.
          if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_wx_libsubdir "vc_x64_dll")
          else()
            set(_wx_libsubdir "vc_dll")
          endif()

          # Source tree: a local checkout (which must have its submodules) if one
          # was given, otherwise download + extract the source release once.
          if(WXWIDGETS_SOURCE_DIR)
            set(_wx_src "${WXWIDGETS_SOURCE_DIR}")
          else()
            set(_wx_src "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-src")
            if(NOT EXISTS "${_wx_src}/CMakeLists.txt")
              if(WXWIDGETS_SOURCE_SHA256)
                set(_wx_hash EXPECTED_HASH SHA256=${WXWIDGETS_SOURCE_SHA256})
              else()
                set(_wx_hash "")
              endif()
              message(STATUS "Downloading wxWidgets source: ${WXWIDGETS_SOURCE_URL}")
              file(DOWNLOAD "${WXWIDGETS_SOURCE_URL}"
                "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-src.7z" ${_wx_hash} SHOW_PROGRESS)
              file(MAKE_DIRECTORY "${_wx_src}")
              execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf
                "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-src.7z"
                WORKING_DIRECTORY "${_wx_src}")
            endif()
          endif()

          # Generator args to reuse for wx's own configure step.
          set(_wx_gen -G "${CMAKE_GENERATOR}")
          if(CMAKE_GENERATOR_PLATFORM)
            list(APPEND _wx_gen -A "${CMAKE_GENERATOR_PLATFORM}")
          endif()
          if(CMAKE_GENERATOR_TOOLSET)
            list(APPEND _wx_gen -T "${CMAKE_GENERATOR_TOOLSET}")
          endif()
          set(_wx_defs
            -DwxBUILD_SHARED=ON
            -DwxUSE_WEBVIEW=ON -DwxUSE_WEBVIEW_EDGE=ON
            -DwxBUILD_TESTS=OFF -DwxBUILD_SAMPLES=OFF -DwxBUILD_DEMOS=OFF)

          # The configure step also auto-downloads the WebView2 SDK nuget and
          # stages WebView2Loader.dll next to the built DLLs. cmake --build is
          # incremental, so Odamex reconfigures only pay a near-instant no-op
          # rebuild here after the first (full) compile.
          # We *install* wx to a unified prefix and point FindwxWidgets at that.
          # Module-mode FindwxWidgets assumes headers and libs share one root
          # (${ROOT}/include + ${ROOT}/lib/<prefix>) and rederives the lib dir
          # from the root, so a separate out-of-source build tree isn't usable
          # directly -- the install prefix gives it the prebuilt-style layout.
          set(_wx_prefix "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets/install")
          get_property(_wx_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
          if(_wx_multi)
            # Multi-config (e.g. Visual Studio): Debug+Release land side by side
            # in one tree; build + install each requested config into one prefix.
            set(_wx_build "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets/build")
            if(NOT EXISTS "${_wx_build}/CMakeCache.txt")
              message(STATUS "Configuring wxWidgets (Edge WebView) in ${_wx_build}")
              execute_process(COMMAND "${CMAKE_COMMAND}"
                -S "${_wx_src}" -B "${_wx_build}" ${_wx_gen} ${_wx_defs}
                RESULT_VARIABLE _wx_cfg_result)
              if(_wx_cfg_result)
                message(FATAL_ERROR "wxWidgets configure failed (${_wx_cfg_result}).")
              endif()
            endif()
            execute_process(COMMAND "${CMAKE_COMMAND}"
              --build "${_wx_build}" --config Release --target wxrc)
            foreach(_wx_cfg IN LISTS WXWIDGETS_BUILD_CONFIGS)
              message(STATUS "Building wxWidgets (${_wx_cfg}) -- first run compiles all of wx")
              execute_process(COMMAND "${CMAKE_COMMAND}"
                --build "${_wx_build}" --config "${_wx_cfg}" --parallel
                RESULT_VARIABLE _wx_build_result)
              if(_wx_build_result)
                message(FATAL_ERROR "wxWidgets build (${_wx_cfg}) failed (${_wx_build_result}).")
              endif()
              execute_process(COMMAND "${CMAKE_COMMAND}"
                --install "${_wx_build}" --config "${_wx_cfg}" --prefix "${_wx_prefix}"
                RESULT_VARIABLE _wx_install_result)
              if(_wx_install_result)
                message(FATAL_ERROR "wxWidgets install (${_wx_cfg}) failed (${_wx_install_result}).")
              endif()
            endforeach()
          else()
            # Single-config (e.g. Ninja): build + install the one config matching
            # Odamex. wx names its DLLs ud_ (debug) / u_ (release) from this type.
            set(_wx_type "${CMAKE_BUILD_TYPE}")
            if(NOT _wx_type)
              set(_wx_type "Debug")
            endif()
            set(_wx_build "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets/build-${_wx_type}")
            if(NOT EXISTS "${_wx_build}/CMakeCache.txt")
              message(STATUS "Configuring wxWidgets ${_wx_type} (Edge WebView) in ${_wx_build}")
              execute_process(COMMAND "${CMAKE_COMMAND}"
                -S "${_wx_src}" -B "${_wx_build}" ${_wx_gen}
                -DCMAKE_BUILD_TYPE=${_wx_type} ${_wx_defs}
                RESULT_VARIABLE _wx_cfg_result)
              if(_wx_cfg_result)
                message(FATAL_ERROR "wxWidgets configure failed (${_wx_cfg_result}).")
              endif()
            endif()
            message(STATUS "Building wxWidgets ${_wx_type} -- first run compiles all of wx")
            execute_process(COMMAND "${CMAKE_COMMAND}"
              --build "${_wx_build}" --parallel
              RESULT_VARIABLE _wx_build_result)
            if(_wx_build_result)
              message(FATAL_ERROR "wxWidgets build (${_wx_type}) failed (${_wx_build_result}).")
            endif()
            execute_process(COMMAND "${CMAKE_COMMAND}"
              --build "${_wx_build}" --target wxrc)
            execute_process(COMMAND "${CMAKE_COMMAND}"
              --install "${_wx_build}" --prefix "${_wx_prefix}"
              RESULT_VARIABLE _wx_install_result)
            if(_wx_install_result)
              message(FATAL_ERROR "wxWidgets install (${_wx_type}) failed (${_wx_install_result}).")
            endif()
            # Help module-mode FindwxWidgets pick the matching libs/setup.h.
            # wx's install creates BOTH mswu and mswud setup dirs (one empty), so
            # FindwxWidgets otherwise defaults to the release config and tries a
            # release+debug lib pair -- but only this one config's libs exist.
            # Pin the configuration and disable the rel+dbg pairing.
            if(_wx_type STREQUAL "Debug")
              set(wxWidgets_USE_DEBUG ON CACHE BOOL "" FORCE)
              set(wxWidgets_CONFIGURATION "mswud" CACHE STRING "" FORCE)
            else()
              set(wxWidgets_USE_DEBUG OFF CACHE BOOL "" FORCE)
              set(wxWidgets_CONFIGURATION "mswu" CACHE STRING "" FORCE)
            endif()
            set(wxWidgets_USE_REL_AND_DBG OFF CACHE BOOL "" FORCE)
          endif()

          # WebView2Loader.dll is staged next to the built DLLs but isn't part
          # of wx's install rules; mirror it into the install lib dir (where the
          # installed wx DLLs land) so the copy-libs step ships it too.
          if(EXISTS "${_wx_build}/lib/${_wx_libsubdir}/WebView2Loader.dll")
            file(COPY "${_wx_build}/lib/${_wx_libsubdir}/WebView2Loader.dll"
              DESTINATION "${_wx_prefix}/lib/${_wx_libsubdir}")
          endif()

          # Point FindwxWidgets at the install prefix (prebuilt-style layout:
          # ${prefix}/include + ${prefix}/lib/${_wx_libsubdir}/...). It rederives
          # wxWidgets_LIB_DIR from the root, which OdalaunchCopyLibs then reads.
          # wxrc.exe is taken from the build tree, where it's always present.
          set(wxWidgets_ROOT_DIR "${_wx_prefix}" CACHE PATH "" FORCE)
          set(wxWidgets_wxrc_EXECUTABLE
            "${_wx_build}/lib/${_wx_libsubdir}/wxrc.exe" CACHE FILEPATH "" FORCE)
        endif()
      elseif(MINGW)
        # MinGW (prebuilt only; no Edge backend)
        file(DOWNLOAD
          "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.5/wxWidgets-3.1.5-headers.7z"
          "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.1.5-headers.7z"
          EXPECTED_HASH SHA256=5BEF630B59CBE515152EBAABC2B5BB83BBB908B798ACCBF28E4F3D79480EC0E2)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf
          "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.1.5-headers.7z"
          WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets")
        file(DOWNLOAD
          "https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.5/wxMSW-3.1.5_gcc810_x64_Dev.7z"
          "${CMAKE_CURRENT_BINARY_DIR}/wxMSW-3.1.5_gcc810_x64_Dev.7z"
          EXPECTED_HASH SHA256=65ED68EF72C5E9807B64FE664EBA561D4C33F494D71DCDF21D39110C601FD327)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf
          "${CMAKE_CURRENT_BINARY_DIR}/wxMSW-3.1.5_gcc810_x64_Dev.7z"
          WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets")

        # Move the lib directory to where FindwxWidgets.cmake can find it.
        if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets/lib/gcc_dll")
          file(RENAME
            "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets/lib/gcc810_x64_dll"
            "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets/lib/gcc_dll")
        endif()

        set(wxWidgets_wxrc_EXECUTABLE
          "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets/lib/gcc_dll/wxrc.exe"
          CACHE FILEPATH "")
      endif()
    else()
      if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wxWidgets-3.0.5")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/wxWidgets-3.0.5"
          DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
        # Compile wxWidgets and copy resources if exists in /wxWidgets/
        execute_process(COMMAND sh configure --enable-unicode --with-gtk=3
          WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.0.5")
        execute_process(COMMAND make -j3
          WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.0.5")
        file(COPY "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.0.5/utils/wxrc/wxrc"
          DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.0.5/lib/")
        set(wxWidgets_wxrc_EXECUTABLE
          "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.0.5/lib/wxrc"
          CACHE FILEPATH "")
        set(wxWidgets_CONFIG_EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/wxWidgets-3.0.5/wx-config" CACHE PATH "")
        set(wxWidgets_USE_STATIC ON)
      endif()
    endif()
  endif()
endif()
