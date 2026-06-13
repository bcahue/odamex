### wolfSSL ###
#
# Crypto backend for jwt-cpp. jwt-cpp is written against the OpenSSL API;
# wolfSSL's OpenSSL-compatibility layer (OPENSSL_ALL + OPENSSL_EXTRA) satisfies
# that API, letting the server verify ES256 game tickets without pulling in
# full OpenSSL. Only wolfcrypt (ECC P-256 + SHA-256) plus the compat shims are
# needed (for now).
#

if(BUILD_SERVER)
  if(USE_INTERNAL_WOLFSSL)
    message(STATUS "Compiling internal wolfSSL...")
    set(WOLFSSL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/local")
    if(WIN32)
      set(WOLFSSL_LIBRARY "${WOLFSSL_PREFIX}/lib/wolfssl.lib")
    else()
      set(WOLFSSL_LIBRARY "${WOLFSSL_PREFIX}/lib/libwolfssl.a")
    endif()

    # Configure the wolfSSL sub-build. Flags proven by the integration spike:
    # OPENSSL_ALL/EXTRA give the compat API jwt-cpp calls; KEYGEN + CERTGEN +
    # ECC enable the EC public-key import and PEM round-trip jwt-cpp performs.
  	lib_buildgen(
    LIBRARY wolfssl
  	PARAMS 
      "-DBUILD_SHARED_LIBS=OFF"
      "-DWOLFSSL_OPENSSLALL=yes"
      "-DWOLFSSL_OPENSSLEXTRA=yes"
      "-DWOLFSSL_KEYGEN=yes"
      "-DWOLFSSL_CERTGEN=yes"
      "-DWOLFSSL_ECC=yes"
      "-DWOLFSSL_EXAMPLES=no"
      "-DWOLFSSL_CRYPT_TESTS=no")
    lib_build(LIBRARY wolfssl)

    add_library(wolfssl_interface INTERFACE)
    # Two include dirs: the normal one for <wolfssl/...>, and include/wolfssl so
    # jwt-cpp's #include <openssl/ec.h> resolves to wolfSSL's compat header at
    # include/wolfssl/openssl/ec.h.
    target_include_directories(wolfssl_interface INTERFACE
      "${WOLFSSL_PREFIX}/include"
      "${WOLFSSL_PREFIX}/include/wolfssl")
    target_link_libraries(wolfssl_interface INTERFACE "${WOLFSSL_LIBRARY}")
    set_target_properties(wolfssl_interface PROPERTIES GLOBAL True)
    if(WIN32)
      target_link_libraries(wolfssl_interface INTERFACE ws2_32 advapi32 crypt32)
    endif()
  endif()
	find_package(wolfssl)
endif()
