INCLUDE(BuildPlugin)

BUILD_PLUGIN(softvoice SoftVoice.cpp SoftVoice.h MOCFILES SoftVoice.h EMBEDDED_RESOURCES logo.png)

if(WIN32)
    set(softvoice_renderer_dir "${CMAKE_CURRENT_SOURCE_DIR}/renderer")
    set(softvoice_renderer_files
        "${softvoice_renderer_dir}/sv_render.exe"
        "${softvoice_renderer_dir}/svwebspeak-host.exe"
        "${softvoice_renderer_dir}/SVctl32.DLL"
        "${softvoice_renderer_dir}/SVENG32.DLL"
        "${softvoice_renderer_dir}/Svspan32.dll"
    )
    set(softvoice_runtime_dir "$<TARGET_FILE_DIR:lmms>/softvoice-renderer")
    add_custom_target(softvoice-renderer-files ALL
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${softvoice_runtime_dir}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${softvoice_renderer_files} "${softvoice_runtime_dir}"
        DEPENDS ${softvoice_renderer_files}
        VERBATIM
    )
    add_dependencies(softvoice-renderer-files lmms)

    install(FILES ${softvoice_renderer_files} DESTINATION "softvoice-renderer")
endif()
