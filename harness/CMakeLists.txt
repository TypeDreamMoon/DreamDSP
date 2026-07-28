# Console exerciser for dreamdsp_core. Console subsystem on purpose: this is
# meant to be run from a terminal and read.

add_executable(dspharness main.cpp)
target_link_libraries(dspharness PRIVATE dreamdsp_core)
set_target_properties(dspharness PROPERTIES WIN32_EXECUTABLE FALSE)
