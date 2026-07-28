# The DSP layer: pure C++17, no Qt, no Windows, no I/O on the process path.
#
# This is deliberately buildable and testable on its own. Whatever ends up
# hosting it -- a VST plugin, our own APO, or an offline tool -- links this
# unchanged, so the algorithms can be proven before the hosting question is
# settled.

add_library(dreamdsp_core STATIC
    DspTypes.h
    BiquadFilter.h  BiquadFilter.cpp
    Oversampler.h   Oversampler.cpp
    Compressor.h    Compressor.cpp
    Reverb.h        Reverb.cpp
    Saturation.h    Saturation.cpp
    Stereo.h        Stereo.cpp
    MultibandCompressor.h MultibandCompressor.cpp
    WavFile.h       WavFile.cpp     # offline only, never on an audio callback
)

target_include_directories(dreamdsp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(dreamdsp_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(dreamdsp_core PRIVATE /W4 /fp:fast)
else()
    target_compile_options(dreamdsp_core PRIVATE -Wall -Wextra -ffast-math)
endif()
