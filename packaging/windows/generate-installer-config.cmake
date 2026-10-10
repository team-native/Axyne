if(NOT DEFINED AXYNE_BINARY_DIR OR NOT DEFINED AXYNE_SOURCE_DIR OR NOT DEFINED AXYNE_OUTPUT)
    message(FATAL_ERROR "Installer metadata arguments are required")
endif()

set(total 0)
set(payload_files
    "${AXYNE_BINARY_DIR}/axyne.exe"
    "${AXYNE_BINARY_DIR}/Scintilla.dll"
    "${AXYNE_BINARY_DIR}/Lexilla.dll"
    "${AXYNE_BINARY_DIR}/msvcp140.dll"
    "${AXYNE_BINARY_DIR}/vcruntime140.dll"
    "${AXYNE_BINARY_DIR}/vcruntime140_1.dll"
    "${AXYNE_BINARY_DIR}/version.json"
    "${AXYNE_BINARY_DIR}/Scintilla-LICENSE.txt"
    "${AXYNE_BINARY_DIR}/Lexilla-LICENSE.txt"
    "${AXYNE_BINARY_DIR}/Axyne-Uninstaller.exe"
    "${AXYNE_SOURCE_DIR}/LICENSE"
    "${AXYNE_SOURCE_DIR}/docs/RELEASE_NOTES.md")
foreach(payload_file IN LISTS payload_files)
    if(EXISTS "${payload_file}")
        file(SIZE "${payload_file}" payload_size)
        math(EXPR total "${total} + ${payload_size}")
    endif()
endforeach()
math(EXPR tenths "(${total} * 10 + 524288) / 1048576")
math(EXPR whole "${tenths} / 10")
math(EXPR fraction "${tenths} % 10")

file(WRITE "${AXYNE_OUTPUT}"
    "#pragma once\n"
    "#define AXYNE_UI_VERSION L\"${AXYNE_VERSION}\"\n"
    "#define AXYNE_UI_INSTALL_SIZE L\"${whole}.${fraction} MB\"\n"
    "#define AXYNE_UI_INSTALL_BYTES ${total}ULL\n"
    "#define AXYNE_UI_REQUIREMENT L\"Windows x64\"\n"
    "#define AXYNE_UI_LICENSE_NAME L\"MIT License\"\n"
    "#define AXYNE_UI_LICENSE_COPYRIGHT L\"Copyright (c) 2026 team.native\"\n")
