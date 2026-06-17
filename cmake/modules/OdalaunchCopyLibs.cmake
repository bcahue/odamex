function(odalaunch_copy_libs TARGET)
  if(NOT WIN32)
    return()
  endif()

  # Deploy the wxWidgets runtime DLLs next to the launcher. We glob the resolved
  # lib directory rather than hardcoding names so this works regardless of the
  # version (3.3.2 vs 3.3.3...) or the compiler prefix (the prebuilt "vc14x" vs a
  # from-source build's "vc"). FindwxWidgets gives us wxWidgets_LIB_DIR; both the
  # prebuilt layout and a from-source `cmake --install` put the DLLs there next
  # to the import libs.
  if(NOT wxWidgets_LIB_DIR OR NOT EXISTS "${wxWidgets_LIB_DIR}")
    message(WARNING "wxWidgets_LIB_DIR is not set/found; cannot deploy wxWidgets DLLs next to ${TARGET}.")
    return()
  endif()

  set(_wx_dll_dir "${wxWidgets_LIB_DIR}")

  # wx names DLLs ...ud_... (debug) / ...u_... (release).
  get_property(_wx_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(_wx_multi)
    # Multi-config: both debug and release DLLs sit side by side. Glob the debug
    # set, derive each release counterpart, and copy the right one per config.
    file(GLOB _wx_dlls "${_wx_dll_dir}/*ud_*.dll")
    foreach(_wx_dll ${_wx_dlls})
      string(REPLACE "ud_" "u_" _wx_rel "${_wx_dll}")
      add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        $<$<CONFIG:Debug>:${_wx_dll}>
        $<$<CONFIG:Release>:${_wx_rel}>
        $<$<CONFIG:RelWithDebInfo>:${_wx_rel}>
        $<$<CONFIG:MinSizeRel>:${_wx_rel}>
        $<TARGET_FILE_DIR:${TARGET}> VERBATIM)
    endforeach()
  else()
    # Single-config (Ninja): only one config's DLLs exist (debug for a Debug
    # build, release otherwise). Pick the matching set and copy unconditionally.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      file(GLOB _wx_dlls "${_wx_dll_dir}/*ud_*.dll")
    else()
      file(GLOB _wx_dlls "${_wx_dll_dir}/*u_*.dll")
      file(GLOB _wx_debug "${_wx_dll_dir}/*ud_*.dll")
      if(_wx_debug)
        list(REMOVE_ITEM _wx_dlls ${_wx_debug})
      endif()
    endif()
    foreach(_wx_dll ${_wx_dlls})
      add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${_wx_dll}" $<TARGET_FILE_DIR:${TARGET}> VERBATIM)
    endforeach()
  endif()

  # WebView2Loader.dll (present only on Edge-enabled from-source builds) is the
  # same binary for every config, so copy it unconditionally when it exists.
  if(EXISTS "${_wx_dll_dir}/WebView2Loader.dll")
    add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${_wx_dll_dir}/WebView2Loader.dll"
      $<TARGET_FILE_DIR:${TARGET}> VERBATIM)
  endif()
endfunction()
