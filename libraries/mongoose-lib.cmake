### Mongoose (single-file embedded HTTP/networking library) ###

# Used by the launcher (odalaunch) for the OIDC loopback callback listener.
# Vendored as mongoose.c + mongoose.h (v7.21, GPL-2.0-only or commercial --
# compatible with Odamex's GPLv2+). Default config: OS sockets + HTTP, plain
# HTTP only, which is all the 127.0.0.1 loopback callback needs. Compiled as a
# plain static lib (not via odamex_target_settings) so the project's strict
# warning/conformance flags aren't applied to third-party C.
if(BUILD_LAUNCHER)
  add_library(mongoose STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/mongoose/mongoose.c")
  target_include_directories(mongoose PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/mongoose")
  if(WIN32)
    target_link_libraries(mongoose PUBLIC ws2_32)
  endif()
endif()
