#pragma once

#include <atomic>
#include <cstring>
#include "ParamBlock.h"

namespace dreamdsp::dsp {

// One publisher, any number of readers. Atomic payload words avoid the data
// race of a seqlock over plain memory. Sequential consistency orders stamps
// AND payload accesses. Readers commit only a complete, validated snapshot.
class ParamSlots
{
public:
    static constexpr size_t kWords = sizeof(ParamBlock) / sizeof(uint32_t);
    static_assert(sizeof(ParamBlock) % sizeof(uint32_t) == 0, "word-aligned payload");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "RT requires lock-free words");
    static_assert(std::atomic<uint64_t>::is_always_lock_free, "RT requires lock-free stamp");

    // Before the publisher/readers start only.
    void clear() noexcept { publish(transparentBlock()); }
    void publish(const ParamBlock &block) noexcept
    {
        uint32_t words[kWords];
        std::memcpy(words, &block, sizeof block);
        const uint64_t stamp = m_stamp.load();
        m_stamp.store(stamp + 1);
        for (size_t i = 0; i < kWords; ++i) m_words[i].store(words[i]);
        m_stamp.store(stamp + 2);
    }
    bool read(ParamBlock *out) const noexcept
    {
        if (!out) return false;
        uint32_t words[kWords];
        for (int attempt = 0; attempt < 2; ++attempt) {
            const uint64_t before = m_stamp.load();
            if (before & 1u) continue;
            for (size_t i = 0; i < kWords; ++i) words[i] = m_words[i].load();
            if (before == m_stamp.load()) {
                std::memcpy(out, words, sizeof(ParamBlock));
                return true;
            }
        }
        return false; // out stays unchanged, including on the second failure
    }
private:
    std::atomic<uint32_t> m_words[kWords]{};
    std::atomic<uint64_t> m_stamp{0};
};

} // namespace dreamdsp::dsp
