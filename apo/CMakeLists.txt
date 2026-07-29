# The audio processing object.
#
# Loaded into audiodg.exe, so: no Qt, no exceptions on the process path, and
# the static runtime -- a missing VC redist would mean the DLL fails to load
# inside a system process, where nothing can report it.
#
# /MANIFEST:NO matters too. An APO carrying an embedded manifest fails to load
# once the audio graph is running protected content, and the failure is silent.

add_library(DreamDspApo SHARED
    DreamApo.h    DreamApo.cpp
    ParamChannel.h ParamChannel.cpp
    DllMain.cpp
    DreamDspApo.def
)

target_link_libraries(DreamDspApo PRIVATE dreamdsp_core_mt ole32 uuid)

target_compile_definitions(DreamDspApo PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
)

set_target_properties(DreamDspApo PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

if(MSVC)
    target_link_options(DreamDspApo PRIVATE /MANIFEST:NO)
endif()
