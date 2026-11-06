#include "SynthChecks.h"
#include <cstdio>
#include <vector>
#include <fstream>
#include <cstdlib>

using namespace pocket;
void require(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
std::vector<int16_t> audio(SynthEngine& e, Controls c, unsigned seconds = 1) {
    e.reset(); e.apply(c);
    std::vector<int16_t> data(SampleRate * seconds);
    e.render(data.data(), data.size()); return data;
}
double rms(const std::vector<int16_t>& data, unsigned start = 0) {
    double sum = 0;
    for (unsigned i = start; i < data.size(); ++i) sum += double(data[i]) * data[i];
    return std::sqrt(sum / (data.size() - start));
}
int main() {
    SynthEngine e; e.init();
    require(checkSynth(e), "boot DSP checks");
    require(noteForKey('d') == 13 && noteForKey('4') == 1, "C-sharp mappings");
    for (int i = 0; i < 12; ++i) {
        require(noteForKey(LowKeys[i]) == i && noteForKey(HighKeys[i]) == i + 12, "two piano octaves");
    }
    require(noteForKey('3') == -1 && noteForKey('6') == -1 && noteForKey('s') == -1 && noteForKey('k') == -1, "unused gaps");
    Controls c; c.volume = 180; c.notes = 1;
    double minRms = 1e9, maxRms = 0;
    for (unsigned w = 0; w < 4; ++w) {
        c.wave = w;
        auto data = audio(e, c);
        double energy = rms(data, SampleRate / 2);
        std::printf("Wave %u RMS %.1f\n", w, energy);
        require(energy > 1500, "each waveform is audible PCM");
        minRms = std::min(minRms, energy); maxRms = std::max(maxRms, energy);
        std::ofstream("artifacts/wave-" + std::to_string(w) + ".pcm", std::ios::binary).write(reinterpret_cast<char*>(data.data()), data.size() * 2);
    }
    // Perceptual speaker trims intentionally differ in electrical RMS.
    require(maxRms / minRms < 2.5, "speaker trims retain bounded electrical gain spread");
    for (unsigned wave = 0; wave < 4; ++wave) {
        Controls chord; chord.volume = 200; chord.wave = wave;
        chord.notes = (1u << 0) | (1u << 4) | (1u << 7) | (1u << 12);
        auto data = audio(e, chord);
        for (auto sample : data) require(std::abs(int(sample)) <= 32112, "compensated four-note chord PCM bounded");
    }
    c = {}; c.volume = 180; c.hits[5] = 1; c.muted = 1 << 5;
    require(rms(audio(e, c)) == 0, "muted drum is silent");
    c.muted = 0; c.solo = 1 << 0;
    require(rms(audio(e, c)) == 0, "solo excludes other drums");
    c.solo = 1 << 5;
    require(rms(audio(e, c)) > 100, "solo includes selected drum");
    auto crossings = [](const std::vector<int16_t>& data) {
        unsigned count = 0;
        for (unsigned i = 1; i < 3000; ++i) if (data[i - 1] < 0 && data[i] >= 0) ++count;
        return count;
    };
    unsigned base = crossings(audio(e, c)); c.tunes[5] = 76;
    unsigned raised = crossings(audio(e, c));
    require(raised >= base * 2 - 2 && raised <= base * 2 + 2, "12 semitones doubles drum pitch");
    for (unsigned d = 0; d < DrumCount; ++d) {
        c = {}; c.volume = 180; c.hits[d] = 1;
        auto data = audio(e, c);
        int peak = 0; for (auto s : data) peak = std::max(peak, std::abs(int(s)));
        std::printf("Drum %u peak %d RMS %.1f\n", d, peak, rms(data));
        require(peak > 400 && rms(data) > 25, "all eight drum pads have energy");
        if (d == 5 || d == 6) {
            double previous = 0, filtered = 0, sum = 0;
            const double alpha = std::exp(-2 * double(Pi) * 350 / SampleRate);
            for (auto sample : data) {
                filtered = alpha * (filtered + sample - previous);
                previous = sample; sum += filtered * filtered;
            }
            double body = std::sqrt(sum / data.size());
            std::printf("Tom %u 350Hz high-pass RMS %.1f\n", d, body);
            require(body > 280, "tom has energy above its low fundamental");
        }
        std::ofstream("artifacts/drum-" + std::to_string(d) + ".pcm", std::ios::binary).write(reinterpret_cast<char*>(data.data()), data.size() * 2);
    }
    // Timing must not depend on the PCM block boundaries.
    e.reset(); c = {}; c.running = true; c.swing = 50; e.apply(c);
    std::array<int16_t, 1> one{};
    std::vector<unsigned> changes; int last = -1;
    for (unsigned i = 0; i < 12001; ++i) {
        e.render(one.data(), 1);
        if (e.currentStep() != last) { changes.push_back(i); last = e.currentStep(); }
    }
    require(changes.size() == 5 && changes[0] == 0 && changes[1] == 4500 && changes[2] == 6000
            && changes[3] == 10500 && changes[4] == 12000, "swing long/short timing");
    e.reset(); c = {}; c.running = true; c.length = 64; e.apply(c);
    std::vector<int16_t> longer(SampleRate * 8); e.render(longer.data(), longer.size());
    require(e.currentStep() == 63 && e.stepsRendered() == 64, "64-step length");
    e.render(one.data(), 1); require(e.currentStep() == 0, "64-step wrap");
    e.reset(); c = {}; c.running = true; e.apply(c);
    std::vector<int16_t> n(1000); e.render(n.data(), n.size());
    require(e.quantizedStep() == 0, "quantize first half");
    e.render(n.data(), n.size()); require(e.quantizedStep() == 1, "quantize second half");
    c = {}; c.length = 64; c.swing = 35; c.bpm = 137; c.steps[7] = uint64_t(1) << 63;
    c.muted = 2; c.solo = 8; c.tunes[0] = 40;
    auto encoded = encodeSong(c); Controls loaded;
    require(decodeSong(encoded.data(), encoded.size(), loaded) && loaded.steps == c.steps
            && loaded.length == 64 && loaded.bpm == 137 && loaded.swing == 35
            && loaded.muted == 2 && loaded.solo == 8 && loaded.tunes[0] == 40, "save/load roundtrip");
    Controls untouched = loaded;
    encoded[42] ^= 1;
    require(!decodeSong(encoded.data(), encoded.size(), loaded) && loaded.steps == untouched.steps, "CRC rejection without mutation");
    require(!decodeSong(encoded.data(), 50, loaded), "truncated file rejection");
    for (unsigned wave = 0; wave < 4; ++wave) {
        c = {}; c.wave = wave; c.notes = 0xffffff; c.volume = 200; c.running = true; c.bpm = 240;
        for (auto& bits : c.steps) bits = ~uint64_t(0);
        auto data = audio(e, c);
        for (auto s : data) require(std::abs(int(s)) <= 32113, "dense mix bounded PCM");
    }
    std::puts("PASS: DSP, mappings, 8 drums, swing, quantization, 64 steps, persistence and dense mix");
}
