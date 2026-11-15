if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(softvoice_engine_host LANGUAGES CXX)
    set(BIN_DIR ".")
endif()

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SoftVoice synthesis requires a Windows x64 toolchain.")
endif()

add_executable(softvoice-host softvoice-host.cpp)
target_compile_features(softvoice-host PRIVATE cxx_std_17)
target_link_libraries(softvoice-host PRIVATE user32 kernel32)

if(MINGW)
    target_link_options(softvoice-host PRIVATE -municode -static)
endif()

set(engine_dlls
    "${CMAKE_CURRENT_SOURCE_DIR}/svctl64.dll"
    "${CMAKE_CURRENT_SOURCE_DIR}/sveng64.dll"
)

# LMMS uses the build root for runtime output, including configuration subfolders.
set_target_properties(softvoice-host PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_target(softvoice-engine-files ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${engine_dlls} "$<TARGET_FILE_DIR:softvoice-host>"
    DEPENDS softvoice-host ${engine_dlls}
    VERBATIM
)

install(TARGETS softvoice-host RUNTIME DESTINATION "${BIN_DIR}")
install(FILES ${engine_dlls} DESTINATION "${BIN_DIR}")
