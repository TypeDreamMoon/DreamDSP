# Console exerciser for dreamdsp_core. Console subsystem on purpose: this is
# meant to be run from a terminal and read.

add_executable(dspharness main.cpp)
target_link_libraries(dspharness PRIVATE dreamdsp_core)
set_target_properties(dspharness PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME dsp_regression COMMAND dspharness --test)

# No AppController, registry, installed APO, or system parameter-file access.
add_executable(dsp_preset_tests
    preset_tests.cpp
    ../src/core/DreamPreset.cpp
    ../src/core/Biquad.cpp
    ../src/app/EffectsModel.cpp
    ../src/app/EffectsModel.h)
target_include_directories(dsp_preset_tests PRIVATE ../src)
target_link_libraries(dsp_preset_tests PRIVATE dreamdsp_core Qt6::Core Qt6::Qml)
target_compile_definitions(dsp_preset_tests PRIVATE
    DREAMDSP_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/testdata")
set_target_properties(dsp_preset_tests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME preset_regression COMMAND dsp_preset_tests)
