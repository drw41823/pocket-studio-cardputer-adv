#pragma once
#include "SynthEngine.h"

inline bool checkSynth(pocket::SynthEngine& engine) {
    using namespace pocket;
    std::array<int16_t, 128> samples{};
    Controls c; c.notes = 1; c.volume = 180;
    engine.reset(); engine.apply(c);
    int prev = 0, crossings = 0;
    for (unsigned offset = 0; offset < SampleRate * 2; offset += samples.size()) {
        unsigned n = std::min<unsigned>(samples.size(), SampleRate * 2 - offset);
        engine.render(samples.data(), n);
        for (unsigned i = 0; i < n; ++i) {
            if (samples[i] > 0 && prev <= 0) ++crossings;
            prev = samples[i];
        }
    }
    bool ok = crossings >= 521 && crossings <= 526;
    c.notes = 0; engine.apply(c);
    for (int i = 0; i < 70; ++i) engine.render(samples.data(), samples.size());
    for (int16_t s : samples) ok &= std::abs(int(s)) <= 1;
    engine.reset(); c = {}; c.running = true; c.swing = 50; preset(c, 0); engine.apply(c);
    for (unsigned offset = 0; offset < SampleRate * 2; offset += samples.size())
        engine.render(samples.data(), std::min<unsigned>(samples.size(), SampleRate * 2 - offset));
    ok &= engine.stepsRendered() == 16;
    c.running = false; c.panic = 1; engine.apply(c);
    for (int i = 0; i < 20; ++i) engine.render(samples.data(), samples.size());
    for (int16_t s : samples) ok &= std::abs(int(s)) <= 1;
    for (unsigned d = 0; d < DrumCount; ++d) {
        engine.reset(); c = {}; c.volume = 180; c.hits[d] = 1; engine.apply(c);
        int peak = 0;
        for (int block = 0; block < 20; ++block) {
            engine.render(samples.data(), samples.size());
            for (int16_t s : samples) peak = std::max(peak, std::abs(int(s)));
        }
        ok &= peak > 100;
    }
    engine.reset(); return ok;
}
