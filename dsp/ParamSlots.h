#pragma once

#include <atomic>
#include <cstring>

#include "ParamBlock.h"

namespace dreamdsp::dsp {

// Hand-off of a parameter block from a normal thread to a real-time one.
//
// Single producer (whoever loads parameters), single consumer (the audio
// callback). Deliberately here rather than in the APO: keeping it free of
// Windows means the concurrency protocol can be exercised in the offline
// harness with two std::threads and a few hundred thousand iterations, which is
// the only realistic way to gain confidence in it.
//
// Three slots, not two. The writer round-robins, so the slot a reader is copying
// is two publishes away from being overwritten; the per-slot stamp then makes
// even that case detectable rather than silent.
class ParamSlots
{
public:
    static constexpr int kSlots = 3;

    // Not real-time. Called once before the writer starts.
    //
    // Every slot is filled with the transparent block rather than memset to
    // zero. Both are equally transparent -- enableMask is 0 either way -- but
    // only the former satisfies the "every slot always holds a sanitised block"
    // invariant that the safety argument below rests on.
    void clear() noexcept
    {
        const ParamBlock t = transparentBlock();
        for (int i = 0; i < kSlots; ++i)
            std::memcpy(&m_slot[i], &t, sizeof(ParamBlock));
        for (int i = 0; i < kSlots; ++i)
            m_stamp[i].store(0u, std::memory_order_relaxed);
        m_active.store(0u, std::memory_order_release);
    }

    // Not real-time, single writer. `b` must already be sanitised.
    void publish(const ParamBlock &b) noexcept
    {
        const uint32_t cur = m_active.load(std::memory_order_relaxed);
        const uint32_t next = (cur + 1u) % uint32_t(kSlots);
        const uint32_t s = m_stamp[next].load(std::memory_order_relaxed);

        // Fences, not release stores on the stamps themselves. A release store
        // only stops *earlier* operations moving after it; it does nothing to
        // stop the payload copy below from being hoisted *before* it. That hole
        // is not theoretical -- it produced two torn reads in 155,000 in the
        // harness's two-thread test before the fences went in.
        //
        // The first fence keeps the copy after the odd stamp is visible; the
        // second keeps it before the even one becomes visible.
        m_stamp[next].store(s + 1u, std::memory_order_relaxed);    // odd: in flux
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(&m_slot[next], &b, sizeof(ParamBlock));
        std::atomic_thread_fence(std::memory_order_release);
        m_stamp[next].store(s + 2u, std::memory_order_relaxed);    // even: settled

        m_active.store(next, std::memory_order_release);
    }

    // Real-time safe. At most two attempts: no allocation, no lock, no spin, no
    // syscall, and no loop whose bound depends on another thread making
    // progress. Returns false only if both attempts caught a slot mid-write, in
    // which case the caller keeps its previous copy -- itself always a valid
    // sanitised block.
    bool read(ParamBlock *out) const noexcept
    {
        for (int attempt = 0; attempt < 2; ++attempt) {
            const uint32_t i = m_active.load(std::memory_order_acquire) % uint32_t(kSlots);
            const uint32_t s0 = m_stamp[i].load(std::memory_order_acquire);
            if (s0 & 1u)
                continue;
            std::memcpy(out, &m_slot[i], sizeof(ParamBlock));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (m_stamp[i].load(std::memory_order_relaxed) == s0)
                return true;
        }
        return false;
    }

private:
    // Honest statement of what this rests on: the payload copy is not itself an
    // atomic object, so it is the stamps plus the explicit release/acquire
    // ordering that make an accepted snapshot consistent -- the same guarantee
    // every seqlock rests on, written out rather than left to x86's TSO.
    //
    // Underneath that, and independent of it: every slot always holds a
    // sanitised block, and every field is clamped independently. So even an
    // undetected byte-wise mixture of two slots is a valid, in-range parameter
    // set. No combination can produce a NaN, an infinity, an out-of-range gain
    // or an invalid bool. The worst undetected outcome is a single callback
    // running an old reverb.wet against a new reverb.dry, which heals on the
    // next one.
    ParamBlock m_slot[kSlots];
    std::atomic<uint32_t> m_stamp[kSlots];
    std::atomic<uint32_t> m_active{ 0u };
};

} // namespace dreamdsp::dsp
