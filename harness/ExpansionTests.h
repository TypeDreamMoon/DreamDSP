#pragma once
#include "FdnReverb.h"
#include "Modulation.h"
#include "ParamSlots.h"
#include <array>
#include <atomic>
#include <thread>
#include <vector>

namespace dreamdsp::tests {
template<class Check>
void runExpansionTests(Check check)
{
    using namespace dsp;
    std::printf("\n[parameter migration and broadcast]\n");
    ParamBlock seed = transparentBlock();
    seed.eq.preampDb = -4.0f;
    seed.generation = 471;
    seed.enableMask = kEnTube | kEnLimiter;
    seed.flags = kPfBypass;
    for (uint32_t version : {6u, 7u}) {
        std::array<unsigned char, 1388> raw{};
        std::memcpy(raw.data(), &seed, 1368);
        uint32_t size = uint32_t(raw.size());
        std::memcpy(raw.data() + 4, &version, 4);
        std::memcpy(raw.data() + 8, &size, 4);
        for (int i = 0; i < 20; ++i) raw[1368 + i] = uint8_t(19 - i);
        ParamBlock got;
        const bool ok = decodeParamBlock(raw.data(), raw.size(), &got);
        check(ok && isSane(got) && got.eq.preampDb == -4.0f && got.generation == 471
              && got.enableMask == seed.enableMask, "v6/v7 keeps settings, new stages disabled");
        check(got.flags == (version == 7 ? kPfBypass : 0u), "legacy reserved header is not master bypass");
        int prev = 20;
        bool ordered = true;
        for (uint8_t id : got.order) if (id < 20) { ordered &= int(id) < prev; prev = id; }
        check(ordered, "migration preserves user-relative stage order");
    }
    {
        auto oldSettings = seed;
        oldSettings.enableMask |= kEnBinaural | kEnFdn;
        oldSettings.binaural = {20, -98, 1, -.9f, 35, 1u};
        oldSettings.fdn = {2.5f, 4300, 27, .3f, .8f, -2};
        std::array<unsigned char, 1524> raw{};
        std::memcpy(raw.data(), &oldSettings, 1384);
        std::memcpy(raw.data() + 1384, &oldSettings.neural, 1524 - 1384);
        const uint32_t version = 8, size = 1524;
        std::memcpy(raw.data() + 4, &version, 4);
        std::memcpy(raw.data() + 8, &size, 4);
        ParamBlock got;
        check(decodeParamBlock(raw.data(), raw.size(), &got) && isSane(got)
              && std::memcmp(&got.binaural, &oldSettings.binaural, 16) == 0
              && got.binaural.model == 0 && got.binaural.elevationDeg == 0
              && std::memcmp(&got.neural, &oldSettings.neural, 1524 - 1384) == 0,
              "v8 migration retains old horizontal sound and moves the remaining fields without loss");
    }
    ParamBlock unchanged = seed;
    std::array<unsigned char, 25> junk{};
    check(!decodeParamBlock(junk.data(), junk.size(), &unchanged)
          && std::memcmp(&unchanged, &seed, sizeof seed) == 0, "invalid payload leaves last snapshot intact");

    ParamSlots slots;
    slots.clear();
    std::atomic<bool> go{false}, done{false};
    std::atomic<int> bad{0}, accepted{0};
    auto make = [](uint32_t g) {
        auto p = transparentBlock(); p.generation = g;
        p.binaural.yawDeg = float(int(g % 361) - 180);
        p.neural.driveDb = float(int(g % 31) - 12);
        return sanitise(p);
    };
    std::array<std::thread, 4> readers;
    for (auto &reader : readers) reader = std::thread([&] {
        while (!go.load()) std::this_thread::yield();
        auto got = transparentBlock();
        do {
            const auto before = got;
            if (slots.read(&got)) {
                if (got.generation) {
                    const auto expected = make(got.generation);
                    if (std::memcmp(&got, &expected, sizeof got)) ++bad;
                    ++accepted;
                }
            } else if (std::memcmp(&got, &before, sizeof got)) ++bad;
        } while (!done.load());
    });
    go.store(true);
    for (uint32_t i = 1; i <= 20000; ++i) slots.publish(make(i));
    done.store(true);
    for (auto &reader : readers) reader.join();
    check(bad.load() == 0 && accepted.load() > 0, "four APO readers: no mixed payload or failed-read mutation");

    std::printf("\n[modulation and feedback delay network]\n");
    for (auto kind : {Modulation::Kind::Chorus, Modulation::Kind::Flanger, Modulation::Kind::Phaser}) {
        Modulation whole(kind), split(kind);
        whole.prepare(48000, 2); split.prepare(48000, 2);
        Modulation::Params p;
        p.feedback = 0.8f; p.mix = 0.7f;
        whole.setParams(p); split.setParams(p); whole.reset(); split.reset();
        constexpr int n = 4096;
        std::array<std::vector<float>,2> a{std::vector<float>(n),std::vector<float>(n)}, b = a;
        for (int i=0; i<n; ++i) a[0][i]=b[0][i]=float(0.1*std::sin(i*0.17));
        float *ap[]={a[0].data(),a[1].data()}; whole.process({ap,2,n});
        for(int i=0;i<n;i+=37) { float *bp[]={b[0].data()+i,b[1].data()+i}; split.process({bp,2,std::min(37,n-i)}); }
        bool same=true, isolated=true, finite=true;
        for(int i=0;i<n;++i) { same &= a[0][i]==b[0][i]; isolated &= a[1][i]==0.0f; finite &= std::isfinite(a[0][i]) && std::abs(a[0][i])<=4.0f; }
        check(same && isolated && finite,"modulation: block invariant, isolated channels, stable feedback");
    }
    FdnReverb reverb;
    reverb.prepare(48000,2);
    FdnReverb::Params p; p.preDelayMs=0; p.decaySeconds=0.5f; p.mix=1; p.dampingHz=18000;
    reverb.setParams(p); reverb.reset();
    std::array<float,128> l{},r{}; float *ptrs[]={l.data(),r.data()};
    double early=0,late=0; bool finite=true;
    for(int block=0;block<1125;++block) {
        l.fill(0);r.fill(0); if(block==0) l[0]=r[0]=1;
        reverb.process({ptrs,2,128});
        for(int i=0;i<128;++i) { const double e=l[i]*l[i]+r[i]*r[i]; if(block<188)early+=e; if(block>937)late+=e; finite &= std::isfinite(e) && std::abs(l[i])<=4 && std::abs(r[i])<=4; }
    }
    check(finite && early>1e-8 && late<early*1e-5,"FDN produces a finite tail and decays after silent input");

    // Exercise public DSP APIs directly: the wire sanitiser must not be the
    // only barrier before a NaN delay is converted into a vector index.
    const uint32_t badBits[] = {0x7FC12345u, 0x7F800000u, 0xFF800000u};
    bool safeModulation = true, safeReverb = true;
    for (uint32_t bits : badBits) {
        float invalid;
        std::memcpy(&invalid, &bits, sizeof invalid);
        for (auto kind : {Modulation::Kind::Chorus, Modulation::Kind::Flanger, Modulation::Kind::Phaser}) {
            Modulation effect(kind);
            effect.prepare(48000, 2);
            Modulation::Params malformed;
            malformed.rateHz = malformed.depth = malformed.delayMs = invalid;
            malformed.feedback = malformed.mix = malformed.stereoPhase = invalid;
            effect.setParams(malformed);
            effect.reset();
            l.fill(0.125f); r.fill(-0.25f);
            effect.process({ptrs, 2, 128});
            for (int i = 0; i < 128; ++i)
                safeModulation &= l[i] == 0.125f && r[i] == -0.25f;
        }
        FdnReverb::Params malformed;
        malformed.decaySeconds = malformed.dampingHz = malformed.preDelayMs = invalid;
        malformed.mix = malformed.width = malformed.outputDb = invalid;
        reverb.setParams(malformed);
        reverb.reset();
        l.fill(0.125f); r.fill(-0.25f);
        reverb.process({ptrs, 2, 128});
        for (int i = 0; i < 128; ++i)
            safeReverb &= l[i] == 0.125f && r[i] == -0.25f;
    }
    check(safeModulation, "modulation direct NaN/Inf parameters cannot poison delay indexes or smoothers");
    check(safeReverb, "FDN direct NaN/Inf parameters cannot poison predelay indexes or feedback");

    const uint64_t rateBits[] = {0x7FF8123456789ABCull, 0x7FF0000000000000ull,
                                 0xFFF0000000000000ull, 0x7FEFFFFFFFFFFFFFull};
    bool safeRates = true;
    for (uint64_t bits : rateBits) {
        double invalid;
        std::memcpy(&invalid, &bits, sizeof invalid);
        Modulation invalidMod(Modulation::Kind::Chorus), referenceMod(Modulation::Kind::Chorus);
        FdnReverb invalidFdn, referenceFdn;
        invalidMod.prepare(invalid, 2); referenceMod.prepare(48000, 2);
        invalidFdn.prepare(invalid, 2); referenceFdn.prepare(48000, 2);
        std::array<float, 128> rl{}, rr{};
        float *referencePtrs[] = {rl.data(), rr.data()};
        for (int block = 0; block < 40; ++block) {
            l.fill(0); r.fill(0); rl.fill(0); rr.fill(0);
            if (block == 0) l[0] = r[0] = rl[0] = rr[0] = 0.5f;
            invalidMod.process({ptrs, 2, 128});
            referenceMod.process({referencePtrs, 2, 128});
            invalidFdn.process({ptrs, 2, 128});
            referenceFdn.process({referencePtrs, 2, 128});
            safeRates &= l == rl && r == rr;
        }
    }
    check(safeRates, "modulation/FDN invalid rates safely fall back to 48 kHz before sizing buffers");

    FdnReverb::Params gainParams;
    gainParams.mix = 0.0f;
    gainParams.outputDb = -6.0f;
    reverb.setParams(gainParams); reverb.reset();
    l.fill(0.125f); r.fill(-0.25f);
    reverb.process({ptrs, 2, 128});
    check(std::abs(l[0] - 0.125f * dbToLin(-6.0f)) < 1e-7f
          && std::abs(r[0] + 0.25f * dbToLin(-6.0f)) < 1e-7f,
          "FDN output trim applies continuously at zero wet mix");
    gainParams.outputDb = 0.0f;
    reverb.setParams(gainParams); reverb.reset();
    l.fill(0.125f); r.fill(-0.25f);
    reverb.process({ptrs, 2, 128});
    check(l[0] == 0.125f && r[0] == -0.25f,
          "FDN zero wet mix with unity output is bit-exact dry");
}
} // namespace dreamdsp::tests
