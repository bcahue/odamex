### jwt-cpp (header-only JWT library) ###

if(BUILD_SERVER)
  add_library(jwt-cpp INTERFACE)
  target_include_directories(jwt-cpp INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/jwt-cpp")

  # jwt-cpp is written against the OpenSSL API. We satisfy it with wolfSSL's
  # OpenSSL-compatibility layer (see wolfssl-lib.cmake), which must be included
  # before this file so wolfssl_interface exists. This brings the compat
  # include dirs (so jwt-cpp's <openssl/*.h> resolve) and the static crypto lib.
  target_link_libraries(jwt-cpp INTERFACE wolfssl_interface)
endif()
