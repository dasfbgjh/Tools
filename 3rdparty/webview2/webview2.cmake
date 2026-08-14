include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/../webview/core/include
    ${CMAKE_CURRENT_LIST_DIR}/include
)

link_libraries(advapi32 ole32 shell32 shlwapi user32 version)