#pragma once

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#  define DREAMDSP_DENORMALS_SSE 1
#  include <xmmintrin.h>
#  include <pmmintrin.h>
#else
#  define DREAMDSP_DENORMALS_SSE 0
#  include <cfloat>
#endif

namespace dreamdsp::dsp {

// Flush-to-zero and denormals-are-zero for the duration of a scope.
//
// Every recursive filter here decays towards zero when the input goes quiet:
// the reverb's 24 delay lines, the DC blockers, and every biquad. Once those
// states reach denormal range each operation costs 50-100x more than a normal
// one -- a CPU spike arriving exactly when there is no signal left to mask a
// dropout.
//
// Restoring the previous mode is not optional. Inside audiodg this code shares
// a thread with other vendors' processing objects, and leaving their arithmetic
// silently altered would be a genuinely nasty thing to do. Hence RAII, with
// every exit path running the destructor.
class DenormalGuard
{
public:
    DenormalGuard() noexcept
        : m_saved(rawMode())
    {
#if DREAMDSP_DENORMALS_SSE
        // 0x8000 = flush-to-zero, 0x0040 = denormals-are-zero.
        _mm_setcsr(m_saved | 0x8040u);
#else
        unsigned tmp = 0;
        _controlfp_s(&tmp, _DN_FLUSH, _MCW_DN);
#endif
    }

    ~DenormalGuard() noexcept
    {
#if DREAMDSP_DENORMALS_SSE
        _mm_setcsr(m_saved);
#else
        unsigned tmp = 0;
        _controlfp_s(&tmp, m_saved, _MCW_DN);
#endif
    }

    DenormalGuard(const DenormalGuard &) = delete;
    DenormalGuard &operator=(const DenormalGuard &) = delete;

    // Exposed so a test can assert the mode really is put back.
    static unsigned rawMode() noexcept
    {
#if DREAMDSP_DENORMALS_SSE
        return _mm_getcsr();
#else
        unsigned cur = 0;
        _controlfp_s(&cur, 0, 0);
        return cur & _MCW_DN;
#endif
    }

private:
    unsigned m_saved;
};

} // namespace dreamdsp::dsp
