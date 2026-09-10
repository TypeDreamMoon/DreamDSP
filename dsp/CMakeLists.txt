# The DSP layer: pure C++17, no Qt, no Windows, no I/O on the process path.
#
# This is deliberately buildable and testable on its own. Whatever ends up
# hosting it -- an APO, a plugin, or the offline renderer -- links this
# unchanged, so the algorithms are proven before the hosting question is
# settled.
#
# Built twice, differing only in C runtime. The application links Qt and
# therefore needs the dynamic CRT; the APO is loaded into audiodg.exe, where a
# missing VC redist would mean a silent load failure inside a system process,
# so it takes the static one.

set(DREAMDSP_CORE_SOURCES
    DspTypes.h
    Denormals.h
    Fft.h                 Fft.cpp
    Convolver.h           Convolver.cpp
    ImpulseBlob.h         ImpulseBlob.cpp
    ImpulseAnalysis.h     ImpulseAnalysis.cpp
    Resampler.h           Resampler.cpp
    ParamBlock.h          ParamBlock.cpp
    ParamSlots.h
    StatusBlock.h
    EffectChain.h         EffectChain.cpp
    Equalizer.h           Equalizer.cpp
    GraphicEq.h           GraphicEq.cpp
    Limiter.h             Limiter.cpp
    Clipper.h             Clipper.cpp
    BassBoost.h           BassBoost.cpp
    Loudness.h            Loudness.cpp
    LoudnessMeter.h       LoudnessMeter.cpp
    AutoGain.h            AutoGain.cpp
    Transient.h           Transient.cpp
    Routing.h             Routing.cpp
    BiquadFilter.h        BiquadFilter.cpp
    Oversampler.h         Oversampler.cpp
    Compressor.h          Compressor.cpp
    Reverb.h              Reverb.cpp
    Saturation.h          Saturation.cpp
    Stereo.h              Stereo.cpp
    MultibandCompressor.h MultibandCompressor.cpp
    WavFile.h             WavFile.cpp     # offline only, never on a callback
)

function(dreamdsp_add_core name runtime)
    add_library(${name} STATIC ${DREAMDSP_CORE_SOURCES})
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(${name} PUBLIC cxx_std_17)
    set_target_properties(${name} PROPERTIES MSVC_RUNTIME_LIBRARY "${runtime}")
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /fp:fast)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -ffast-math)
    endif()
endfunction()

dreamdsp_add_core(dreamdsp_core     "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
dreamdsp_add_core(dreamdsp_core_mt  "MultiThreaded$<$<CONFIG:Debug>:Debug>")
