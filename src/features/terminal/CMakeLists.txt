# Upstream 0.3.3 release includes generated encoding tables, unlike Git tags.
# Pin its complete archive so Windows needs no Perl/source-generation tools.
enable_language(CXX)
FetchContent_Declare(axyne_vterm_source
    URL https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    URL_HASH SHA256=09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0)
FetchContent_GetProperties(axyne_vterm_source)
if(NOT axyne_vterm_source_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(axyne_vterm_source)
    cmake_policy(POP)
endif()
add_library(axyne_terminal STATIC "${CMAKE_CURRENT_LIST_DIR}/terminal.cpp")
target_include_directories(axyne_terminal PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(axyne_terminal PRIVATE axyne_vterm)
set_target_properties(axyne_terminal PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(WIN32)
    target_sources(axyne_terminal PRIVATE "${CMAKE_CURRENT_LIST_DIR}/terminal_windows.cpp")
else()
    find_package(Threads REQUIRED)
    target_sources(axyne_terminal PRIVATE "${CMAKE_CURRENT_LIST_DIR}/terminal_posix.cpp")
    target_link_libraries(axyne_terminal PRIVATE Threads::Threads)
endif()
if(TARGET axyne_ui)
    target_link_libraries(axyne_ui PRIVATE axyne_terminal)
endif()
file(GLOB AXYNE_VTERM_SOURCES CONFIGURE_DEPENDS "${axyne_vterm_source_SOURCE_DIR}/src/*.c")
add_library(axyne_vterm STATIC ${AXYNE_VTERM_SOURCES})
target_include_directories(axyne_vterm SYSTEM PUBLIC "${axyne_vterm_source_SOURCE_DIR}/include")
target_include_directories(axyne_vterm PRIVATE "${axyne_vterm_source_SOURCE_DIR}/src")
if(MSVC)
    target_compile_options(axyne_vterm PRIVATE /utf-8)
endif()
if(APPLE)
    add_custom_command(TARGET axyne POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${axyne_vterm_source_SOURCE_DIR}/LICENSE"
        "$<TARGET_BUNDLE_CONTENT_DIR:axyne>/Resources/libvterm-LICENSE.txt")
    install(FILES "${axyne_vterm_source_SOURCE_DIR}/LICENSE"
        DESTINATION "${AXYNE_MACOS_BUNDLE_NAME}/Contents/Resources"
        RENAME libvterm-LICENSE.txt CONFIGURATIONS Release)
elseif(WIN32)
    add_custom_command(TARGET axyne POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${axyne_vterm_source_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:axyne>/libvterm-LICENSE.txt")
    install(FILES "${axyne_vterm_source_SOURCE_DIR}/LICENSE"
        DESTINATION . RENAME libvterm-LICENSE.txt CONFIGURATIONS Release)
endif()
