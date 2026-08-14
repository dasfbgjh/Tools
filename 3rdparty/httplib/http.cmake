include_directories(${CMAKE_CURRENT_LIST_DIR})
link_libraries(ws2_32)
add_compile_definitions(-D_WIN32_WINNT=0x0A00)

# OpenSSL support for HTTPS
set(OPENSSL_MINGW_PATH "C:/msys64/mingw64")
if(EXISTS "${OPENSSL_MINGW_PATH}/include/openssl/ssl.h" AND
   EXISTS "${OPENSSL_MINGW_PATH}/lib/libssl.a" AND
   EXISTS "${OPENSSL_MINGW_PATH}/lib/libcrypto.a")
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_MINGW_PATH}/include")
    add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)
    include_directories(${OPENSSL_INCLUDE_DIR})
    link_libraries(
        "${OPENSSL_MINGW_PATH}/lib/libssl.a"
        "${OPENSSL_MINGW_PATH}/lib/libcrypto.a"
        crypt32
    )
    message(STATUS "OpenSSL support enabled (MinGW)")
else()
    message(STATUS "OpenSSL not found, HTTPS support disabled")
endif()