INCLUDE(BuildPlugin)

if(LMMS_BUILD_WIN32 AND LMMS_HOST_X86_64)
    add_subdirectory(engine_host)
    add_dependencies(lmms softvoice-engine-files)
endif()

BUILD_PLUGIN(softvoice SoftVoice.cpp SoftVoice.h MOCFILES SoftVoice.h EMBEDDED_RESOURCES logo.png)
