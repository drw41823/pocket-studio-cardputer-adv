#pragma once
#include "MusicState.h"
#include <algorithm>
#include <cmath>

namespace pocket {
class SynthEngine {
public:
    void setPolyphony(unsigned limit) { voiceLimit = std::max(1u, std::min(4u, limit)); }
    void init() {
        // All engines use the same immutable table, initialized before the audio task.
        auto& sine=sineTable();static bool initialized=false;
        if(!initialized){for(unsigned i=0;i<sine.size();++i)sine[i]=std::sin(2*Pi*i/sine.size());initialized=true;}
        reset();
    }
    void reset() {
        voices = {}; drums = {}; current = {}; previous = {};
        ticks = 0; step = -1; totalSteps = 0; age = 0;
        pendingStart = false; gain = 0; lastX = lastY = 0;
        for (auto& p : pitch) p = 1;
    }
#if defined(ARDUINO_ARCH_ESP32)
    IRAM_ATTR __attribute__((optimize("O3","reciprocal-math")))
#endif
    void apply(Controls c) {
        c.bpm = std::max<uint16_t>(40, std::min<uint16_t>(240, c.bpm));
        c.octave = std::max<uint8_t>(2, std::min<uint8_t>(6, c.octave));
        c.wave %= 4; c.notes &= 0xffffff; c.swing = std::min<uint8_t>(50, c.swing);
        if (c.length != 16 && c.length != 32 && c.length != 64) c.length = 16;
        if (c.panic != previous.panic) { voices = {}; drums = {}; previous.notes = 0; previous.midiNotes = {}; }
        auto held = [](const Controls& state, unsigned midi) {
            int key = int(midi) - (state.octave + 1) * 12;
            return (state.midiNotes[midi / 32] & (1u << (midi % 32)))
                || (key >= 0 && key < 24 && (state.notes & (1u << key)));
        };
        for (auto& v : voices) if (v.key >= 0 && !held(c, v.key)) release(v);
        if(c.notes||c.midiNotes[0]||c.midiNotes[1]||c.midiNotes[2]||c.midiNotes[3])for (int key = 0; key < 128; ++key) {
            bool retrigger = c.retriggerNotes[key / 32] & (1u << (key % 32));
            if (!held(c, key) || (held(previous, key) && !retrigger)) continue;
            if (retrigger) for (auto& voice : voices) if (voice.key == key) voice.key = -1;
            Voice* chosen = &voices[0];
            for (unsigned i = 0; i < voiceLimit; ++i) {
                auto& v = voices[i];
                if (v.key < 0) { chosen = &v; break; }
                if (v.stage == 3 && chosen->stage != 3) chosen = &v;
                else if (v.stage == chosen->stage && v.age < chosen->age) chosen = &v;
            }
            *chosen = {}; chosen->key = key; chosen->age = ++age;
            chosen->increment = 440.f * std::pow(2.f, (key - 69) / 12.f) / SampleRate;
        }
        if (c.running && !previous.running) { pendingStart = true; step = -1; ticks = 0; }
        if (!c.running && previous.running) { step = -1; pendingStart = false; ticks = 0; }
        if (step >= c.length) step %= c.length;
        current = c;
        for (unsigned d = 0; d < DrumCount; ++d) {
            if (c.tunes[d] != previous.tunes[d]) pitch[d] = std::pow(2.f, (int(c.tunes[d]) - 64) / 12.f);
            if (c.hits[d] != previous.hits[d]) trigger(d);
        }
        previous = c;
    }
#if defined(ARDUINO_ARCH_ESP32)
    IRAM_ATTR __attribute__((optimize("O3","reciprocal-math")))
#endif
    void render(int16_t* out, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            if (current.running) {
                if (pendingStart) { pendingStart = false; advance(); }
                while (ticks >= stepDuration()) { ticks -= stepDuration(); advance(); }
                ticks += current.bpm * 400u;
            }
            float mixed = 0;
            for (auto& v : voices) {
                if (v.key < 0) continue;
                if (v.stage == 0) {
                    v.env += 1.f / (SampleRate * .008f);
                    if (v.env >= 1) { v.env = 1; v.stage = 1; }
                } else if (v.stage == 1) {
                    v.env -= .3f / (SampleRate * .09f);
                    if (v.env <= .7f) { v.env = .7f; v.stage = 2; }
                } else if (v.stage == 3) {
                    v.env -= v.releaseDelta;
                    if (v.env <= 0) { v.key = -1; continue; }
                }
                // Equal electrical RMS alone leaves pure sine weak on the small speaker.
                // Empirical starting trims; keep SINE pure rather than adding harmonics.
                constexpr float trims[] = {1.8f, 1.15f, .85f, 1.f};
                mixed += oscillator(v.phase, v.increment) * trims[current.wave] * v.env * .16f;
                v.phase += v.increment; if (v.phase >= 1) v.phase -= 1;
            }
            for (unsigned d = 0; d < DrumCount; ++d) {
                float sample = drumSample(d);
                if (!(current.muted & (1 << d)) && (!current.solo || (current.solo & (1 << d))))
                    mixed += sample * current.levels[d] / 255.f;
            }
            gain += (current.volume / 255.f - gain) * .002f;
            float x = mixed * gain;
            float y = x - lastX + .995f * lastY;
            lastX = x; lastY = y;
            // Gentle saturation, then a hard PCM bound for dense patterns.
            if (y > .75f) y = .75f + (y - .75f) / (1 + 4 * (y - .75f));
            if (y < -.75f) y = -.75f + (y + .75f) / (1 - 4 * (y + .75f));
            y = std::max(-.98f, std::min(.98f, y));
            out[i] = static_cast<int16_t>(y * 32767);
        }
    }
    int currentStep() const { return step; }
    uint32_t stepsRendered() const { return totalSteps; }
    uint8_t quantizedStep() const {
        if (step < 0) return 0;
        return (ticks >= stepDuration() / 2 ? step + 1 : step) % current.length;
    }
    bool beatEnabled(unsigned drum, unsigned pos) const { return (current.steps[drum] >> pos) & 1; }
private:
    struct Voice {
        int key = -1;
        unsigned age = 0;
        float phase = 0, increment = 0, env = 0, releaseDelta = 0;
        uint8_t stage = 0;
    };
    struct Drum {
        bool active = false;
        unsigned age = 0;
        float phase = 0, env = 0, noisePrev = 0, pitchDrop = 0;
    };
    std::array<Voice, 4> voices{};
    std::array<Drum, DrumCount> drums{};
    std::array<float, DrumCount> pitch{};
    static std::array<float,2048>& sineTable(){static std::array<float,2048> table{};return table;}
    Controls previous{}, current{};
    uint32_t ticks = 0, totalSteps = 0, age = 0, random = 0x12345678;
    int step = -1;
    bool pendingStart = false;
    unsigned voiceLimit = 4;
    float gain = 0, lastX = 0, lastY = 0;
    uint32_t stepDuration() const {
        int factor = 100 + ((step & 1) ? -int(current.swing) : int(current.swing));
        return SampleRate * 60u * factor;
    }
    void advance() {
        step = (step + 1) % current.length; ++totalSteps;
        for (unsigned d = 0; d < DrumCount; ++d) if ((current.steps[d] >> step) & 1) trigger(d);
    }
    void trigger(unsigned d) {
        drums[d] = {}; drums[d].active = true; drums[d].env = 1;
        drums[d].pitchDrop = d == 0 ? 120 : 70;
        if (d == 3) drums[4].active = false; // Closed hat chokes open hat.
    }
    static void release(Voice& v) {
        if (v.key < 0 || v.stage == 3) return;
        v.stage = 3; v.releaseDelta = std::max(v.env / (SampleRate * .18f), .000001f);
    }
    float noise() {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        return static_cast<int32_t>(random) / 2147483648.f;
    }
    __attribute__((always_inline)) float sinPhase(float p) const {
        const auto& sine=sineTable();
        p -= int(p);if(p<0)p+=1;
        return sine[static_cast<unsigned>(p * sine.size()) % sine.size()];
    }
    __attribute__((always_inline)) float drumSample(unsigned index) {
        auto& d = drums[index];
        if (!d.active) return 0;
        float n = noise(), high = (n - d.noisePrev) * .5f;
        d.noisePrev = n;
        float value = 0, frequency = 0, decay = .999f;
        float attack = std::min(1.f, d.age / 12.f);
        switch (index) {
        case 0:
            frequency = 65 + d.pitchDrop; decay = .9991f; d.pitchDrop *= .9975f;
            value = .40f * (sinPhase(d.phase) + .3f * sinPhase(d.phase * 2) + .12f * sinPhase(d.phase * 3));
            if (current.drumTest == 1) value = 0;
            if (d.age < 432 && current.drumTest != 2)
                value += (.50f * high + .32f * sinPhase(d.age * 650.f / SampleRate)) * (1 - d.age / 432.f);
            break;
        case 1: frequency = 180; decay = .9988f; value = .36f * high + .12f * sinPhase(d.phase); break;
        case 2: {
            decay = .999f;
            float burst = d.age < 600 ? (d.age % 200 < 90 ? 1.f : .15f) : .35f;
            value = high * .5f * burst; break;
        }
        case 3: decay = .996f; value = .38f * high; break;
        case 4: decay = .9993f; value = .24f * high; break;
        case 5:
        case 6: {
            frequency = (index == 5 ? 110.f : 210.f) + d.pitchDrop;
            decay = index == 5 ? .99955f : .9994f;
            d.pitchDrop *= .998f;
            // Membrane partials and a short stick attack make the pitched
            // body audible without depending entirely on the fundamental.
            value = .38f * (sinPhase(d.phase) + .50f * sinPhase(d.phase * 1.59f)
                          + .32f * sinPhase(d.phase * 2.14f));
            if (d.age < 144) value += high * .16f * (1 - d.age / 144.f);
            break;
        }
        case 7: frequency = 840; decay = .995f; value = .2f * sinPhase(d.phase) + .12f * high; break;
        }
        d.phase += frequency * pitch[index] / SampleRate;
        if (d.phase >= 1) d.phase -= 1;
        float result = value * d.env * attack;
        d.env *= decay; ++d.age;
        if (d.env < .00002f || d.age > SampleRate) d.active = false;
        return result;
    }
    static float blep(float t, float dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1; }
        if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
        return 0;
    }
    float oscillator(float phase, float increment) const {
        switch (current.wave) {
        case 0: return sinPhase(phase) * 1.41421356f;
        case 1: return (1 - 4 * std::fabs(phase - .5f)) * 1.7320508f;
        case 2: {
            float shifted = phase + .5f; if (shifted >= 1) shifted -= 1;
            return (phase < .5f ? 1.f : -1.f) + blep(phase, increment) - blep(shifted, increment);
        }
        default: return (2 * phase - 1 - blep(phase, increment)) * 1.7320508f;
        }
    }
};
}
