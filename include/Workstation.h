#pragma once
#include "SynthEngine.h"
#include "MusicTools.h"

namespace pocket {
constexpr unsigned TrackCount = 4, MelodyTracks = 3, MaxEvents = 128, TicksPerStep = 24;
struct NoteEvent { uint16_t start = 0, duration = 24; uint8_t midi = 60, velocity = 100; };
struct MelodyTrack {
    ToolsSettings tools;
    uint8_t wave = 0, octave = 4, volume = 100;
    bool muted = false;
    uint16_t count = 0;
    std::array<NoteEvent, MaxEvents> events{};
};
struct Project {
    Controls drums;
    std::array<MelodyTrack, MelodyTracks> tracks{};
    uint8_t root = 0, scale = 0; // Project key: chromatic root, major / natural minor.
    uint8_t drumVolume = 100;
    bool drumMuted = false;
    Project() { drums.length = 32; tracks[1].octave = 3; tracks[2].wave = 1; }
    unsigned span() const { return drums.length * TicksPerStep; }
};
constexpr unsigned ProjectBytes = 12 + SongBytes + MelodyTracks * (8 + MaxEvents * 6) + 4;
inline void encodeProjectTo(const Project& p, uint8_t* data) {
    std::fill(data, data + ProjectBytes, 0);
    for (unsigned i = 0; i < 6; ++i) data[i] = "PSYN06"[i];
    data[6] = p.root; data[7] = p.scale;
    data[8] = 6; data[10] = p.drumVolume; data[11] = p.drumMuted;
    auto legacy = encodeSong(p.drums);
    std::copy(legacy.begin(), legacy.end(), data + 12);
    unsigned cursor = 12 + SongBytes;
    for (const auto& track : p.tracks) {
        data[cursor] = track.wave; data[cursor+1] = track.octave;
        data[cursor+2] = track.volume; data[cursor+3] = track.muted;
        data[cursor+4] = track.count; data[cursor+5] = track.count >> 8;
        data[cursor+6] = track.tools.chord | (track.tools.arp<<1) | (track.tools.rate<<3) | ((track.tools.octaves-1)<<5) | (track.tools.hold<<6);
        data[cursor+7] = track.tools.gate;
        cursor += 8;
        for (const auto& e : track.events) {
            data[cursor++] = e.start; data[cursor++] = e.start >> 8;
            data[cursor++] = e.duration; data[cursor++] = e.duration >> 8;
            data[cursor++] = e.midi; data[cursor++] = e.velocity;
        }
    }
    uint32_t crc = crc32(data, ProjectBytes - 4);
    for (unsigned i = 0; i < 4; ++i) data[ProjectBytes - 4 + i] = crc >> (i * 8);
}
inline std::array<uint8_t, ProjectBytes> encodeProject(const Project& p) {
    std::array<uint8_t, ProjectBytes> data{};
    encodeProjectTo(p, data.data()); return data;
}
inline bool decodeProject(const uint8_t* data, unsigned size, Project& output) {
    Project p;
    if (size == SongBytes) {
        if (!decodeSong(data, size, p.drums)) return false;
        p.tracks[0].wave = p.drums.wave; p.tracks[0].octave = p.drums.octave;
        output = p; return true;
    }
    if (size != ProjectBytes) return false;
    bool oldFormat = data[8] == 3;
    const char* magic = oldFormat ? "PSYN03" : data[8]==4 ? "PSYN04" : data[8]==5 ? "PSYN05" : "PSYN06";
    for (unsigned i = 0; i < 6; ++i) if (data[i] != magic[i]) return false;
    uint32_t crc = 0;
    for (unsigned i = 0; i < 4; ++i) crc |= uint32_t(data[ProjectBytes - 4 + i]) << (i * 8);
    if ((data[8] != 3 && data[8] != 4 && data[8] != 5 && data[8]!=6) || crc != crc32(data, ProjectBytes - 4)
        || data[10] > 200 || data[11] > 1 || !decodeSong(data + 12, SongBytes, p.drums)) return false;
    if (!oldFormat) { p.root = data[6]; p.scale = data[7]; }
    if (p.root > 11 || p.scale > 1) return false;
    p.drumVolume = data[10]; p.drumMuted = data[11];
    unsigned cursor = 12 + SongBytes;
    for (auto& t : p.tracks) {
        t.wave = data[cursor]; t.octave = data[cursor+1]; t.volume = data[cursor+2];
        t.muted = data[cursor+3]; t.count = data[cursor+4] | (unsigned(data[cursor+5]) << 8);
        if (t.wave > (data[8]==6?7:3) || t.octave < 2 || t.octave > 6 || t.volume > 200 || data[cursor+3] > 1 || t.count > MaxEvents) return false;
        if(data[8]>=5) {
            unsigned flags=data[cursor+6];
            t.tools.chord=flags&1;t.tools.arp=(flags>>1)&3;t.tools.rate=(flags>>3)&3;
            t.tools.octaves=1+((flags>>5)&1);t.tools.hold=(flags>>6)&1;t.tools.gate=data[cursor+7];
            if((flags&128) || t.tools.rate>2 || !t.tools.gate || t.tools.gate>100) return false;
        }
        cursor += 8;
        for (unsigned i = 0; i < MaxEvents; ++i) {
            auto& e = t.events[i];
            e.start = data[cursor] | (unsigned(data[cursor+1]) << 8);
            e.duration = data[cursor+2] | (unsigned(data[cursor+3]) << 8);
            e.midi = data[cursor+4]; e.velocity = data[cursor+5]; cursor += 6;
            if (i < t.count && (e.start >= p.span() || !e.duration || e.duration > p.span() || e.midi > 119 || e.midi < 24 || !e.velocity || e.velocity > 127)) return false;
        }
    }
    output = p; return true;
}
constexpr const char* PitchNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline bool canTranspose(const Project& song, unsigned root) {
    if (root > 11) return false;
    int delta = int(root) - song.root;
    for (const auto& track : song.tracks) for (unsigned i = 0; i < track.count; ++i) {
        int midi = track.events[i].midi + delta;
        if (midi < 24 || midi > 119) return false;
    }
    return true;
}
inline bool transposeProject(Project& song, unsigned root) {
    if (!canTranspose(song, root)) return false;
    int delta = int(root) - song.root;
    for (auto& track : song.tracks) for (unsigned i = 0; i < track.count; ++i) track.events[i].midi += delta;
    song.root = root; return true;
}
inline unsigned keyMidi(const Project& song, unsigned track, unsigned key) {
    return (song.tracks[track].octave + 1) * 12 + song.root + key;
}
inline int scaleDegree(unsigned pitchClass, unsigned scale) {
    constexpr uint8_t intervals[2][7] = {{0,2,4,5,7,9,11},{0,2,3,5,7,8,10}};
    for (int degree = 0; degree < 7; ++degree) if (intervals[scale % 2][degree] == pitchClass % 12) return degree;
    return -1;
}
struct Input {
    Project song;
    uint32_t liveKeys = 0, epoch = 0, diagnosticTrigger = 0, recordSession = 0, liveReset = 0;
    uint8_t lastKey = 0;
    bool stepInput = false;
    uint8_t selected = 0, recordTarget = 0, diagnostic = 0;
    bool playing = false, record = false, countIn = false;
};
enum class RecState : uint8_t { Off, CountIn, Armed, Recording };
struct Capture { uint32_t session = 0; uint8_t track = 0, drum = 0; NoteEvent note; bool isDrum = false; uint8_t noteCount = 0; std::array<NoteEvent,4> notes{}; };
using CaptureCallback = void (*)(void*, const Capture&);

class Workstation {
public:
    void init() {
        pending.fill(-1);
        for (unsigned i = 0; i < 4; ++i) engines[i].init();
        for(unsigned i=0;i<MelodyTracks;++i)engines[i].setPolyphony(4);
    }
    void onCapture(void* context, CaptureCallback callback) { captureContext = context; captureCallback = callback; }
    void onSampleTrack(void* context,void(*apply)(void*,unsigned,unsigned,const Controls&),void(*render)(void*,unsigned,int16_t*,unsigned)) {sampleContext=context;sampleApply=apply;sampleRender=render;}
    int position() const { return !input.playing || absoluteTick < 0 ? -1 : (absoluteTick % input.song.span()) / TicksPerStep; }
    int countBeat() const { return absoluteTick < 0 ? (absoluteTick + 384) / 96 + 1 : 0; }
    RecState recording() const { return recState; }
    uint32_t kickTriggers() const { return kickCount; }
#if defined(ARDUINO_ARCH_ESP32)
    IRAM_ATTR __attribute__((optimize("O3")))
#endif
    void apply(const Input& value) {
        bool wasPlaying = input.playing;
        bool restart = value.epoch != input.epoch || (value.playing && !input.playing);
        bool arm = value.record && (!input.record || restart);
        if ((!value.record || !value.playing || restart) && recState == RecState::Recording) finishNotes();
        if (!value.record || !value.playing || restart) recState = RecState::Off;
        if (restart) {
            absoluteTick = value.record && value.countIn ? -384 : 0;
            clock = 0; started = false; pending.fill(-1);
            ++panic;
        }
        if(restart || value.liveReset!=input.liveReset || value.selected!=input.selected) for(auto& arp : arps) arp.reset();
        input = value;
        if (arm) {
            recState = value.countIn && absoluteTick < 0 ? RecState::CountIn : RecState::Armed;
            recordAt = absoluteTick < 0 ? 0 : ((absoluteTick / 384) + 1) * 384;
        }
        if (!value.playing && (wasPlaying || restart)) { absoluteTick = 0; started = false; clock = 0; }
        if (input.diagnosticTrigger != lastDiagnostic) {
            lastDiagnostic = input.diagnosticTrigger; ++panic;
            if (input.diagnostic >= 2) { ++drumHits[0]; ++kickCount; }
            diagnosticLeft = SampleRate / 2;
        }
        captureLive(); refreshVoices();
    }
#if defined(ARDUINO_ARCH_ESP32)
    IRAM_ATTR __attribute__((optimize("O3")))
#endif
    void render(int16_t* output, unsigned count) {
        unsigned offset = 0;
        while (offset < count) {
            if (input.playing && !started) { started = true; onTick(); }
            while (clock >= duration()) {
                clock -= duration(); ++absoluteTick; onTick();
            }
            unsigned n = std::min<unsigned>(BlockSize, count - offset);
            {
                uint32_t inc = input.song.drums.bpm * 400u * TicksPerStep;
                n = std::min<unsigned>(n, (duration() - clock + inc - 1) / inc);
                clock += n * inc;
            }
            if (diagnosticLeft) n = std::min<unsigned>(n, diagnosticLeft);

            std::fill(output + offset, output + offset + n, 0);
            std::array<int32_t, BlockSize> sum{};
            for (unsigned track=0;track<4;++track) {
                if(track<3&&sampleTracks[track]) {
                    if(sampleRender)sampleRender(sampleContext,track,scratch.data(),n);
                    else std::fill(scratch.begin(),scratch.begin()+n,0);
                } else engines[track].render(scratch.data(), n);
                for (unsigned i = 0; i < n; ++i) sum[i] += scratch[i];
            }
            // One master gain after the sum preserves the relative track levels.
            float targetGain = input.diagnostic ? 1.f : input.song.drums.volume / 100.f;
            for (unsigned i = 0; i < n; ++i) {
                masterGain += (targetGain - masterGain) * .002f;
                float value = sum[i] * masterGain;
                float magnitude = std::fabs(value);
                if (magnitude > 24000.f) {
                    float excess = magnitude - 24000.f;
                    value = std::copysign(24000.f + excess / (1.f + excess / 8112.f), value);
                }
                output[offset+i] = std::max(-32112, std::min(32112, int(value)));
            }
            offset += n;
            if (diagnosticLeft) {
                diagnosticLeft -= n;
                if (!diagnosticLeft) refreshVoices();
            }
        }
    }
private:
    Input input;
    std::array<SynthEngine, 4> engines;
    std::array<int16_t, BlockSize> scratch{};
    std::array<uint32_t, DrumCount> drumHits{}, manualHits{};
    std::array<int64_t,128> pending;
    std::array<bool,128> pendingExact{};
    std::array<ArpState,3> arps;
    std::array<PitchSet,3> livePitches;
    uint32_t clock = 0, panic = 0, kickCount = 0, lastDiagnostic = 0, diagnosticLeft = 0;
    int64_t absoluteTick = 0, recordAt = 0;
    bool started = false;
    float masterGain = 1.f;
    RecState recState = RecState::Off;
    CaptureCallback captureCallback = nullptr;
    void* captureContext = nullptr;
    std::array<bool,3> sampleTracks{};
    void* sampleContext=nullptr;
    void(*sampleApply)(void*,unsigned,unsigned,const Controls&)=nullptr;
    void(*sampleRender)(void*,unsigned,int16_t*,unsigned)=nullptr;
    uint32_t duration() const {
        int64_t step = absoluteTick / TicksPerStep;
        int factor = 100 + ((step & 1) ? -int(input.song.drums.swing) : int(input.song.drums.swing));
        return SampleRate * 60u * factor;
    }
    int64_t nearestStep() const {
        return ((std::max<int64_t>(0, absoluteTick) + TicksPerStep / 2) / TicksPerStep) * TicksPerStep;
    }
    void emit(Capture value) { value.session = input.recordSession; if (captureCallback) captureCallback(captureContext, value); }
    void finishNotes() {
        Capture group;group.track=input.recordTarget;
        for(unsigned midi=0;midi<128;++midi) if(pending[midi]>=0) {
            NoteEvent note;note.start=pending[midi]%input.song.span();note.midi=midi;
            int64_t now=pendingExact[midi]?std::max<int64_t>(0,absoluteTick):nearestStep();
            note.duration=std::min<int64_t>(input.song.span(),std::max<int64_t>(pendingExact[midi]?1:24,now-pending[midi]));
            group.notes[group.noteCount++]=note;pending[midi]=-1;
            if(group.noteCount==4) {group.note=group.notes[0];emit(group);group.noteCount=0;}
        }
        if(group.noteCount) {group.note=group.notes[0];emit(group);}
    }
    void capturePlayed(unsigned track,const PitchSet& pitches,bool retrigger) {
        if(recState!=RecState::Recording || input.recordTarget!=track || track>=3) return;
        bool exact=input.song.tracks[track].tools.arp && !input.stepInput;
        auto held=[&](unsigned midi){return std::find(pitches.notes.begin(),pitches.notes.begin()+pitches.count,midi)!=pitches.notes.begin()+pitches.count;};
        Capture group;group.track=track;
        for(unsigned midi=0;midi<128;++midi) {
            bool on=held(midi);
            if(pending[midi]>=0 && (!on || retrigger)) {
                NoteEvent note;note.start=pending[midi]%input.song.span();note.midi=midi;
                int64_t now=pendingExact[midi]?std::max<int64_t>(0,absoluteTick):nearestStep();
                note.duration=std::min<int64_t>(input.song.span(),std::max<int64_t>(pendingExact[midi]?1:24,now-pending[midi]));
                group.notes[group.noteCount++]=note;pending[midi]=-1;
                if(group.noteCount==4) {group.note=group.notes[0];emit(group);group.noteCount=0;}
            }
            if(on && pending[midi]<0) {pending[midi]=exact?std::max<int64_t>(0,absoluteTick):nearestStep();pendingExact[midi]=exact;}
        }
        if(group.noteCount) {group.note=group.notes[0];emit(group);}
    }
    void captureLive() {
        for (unsigned d = 0; d < DrumCount; ++d) if (input.song.drums.hits[d] != manualHits[d]) {
            manualHits[d] = input.song.drums.hits[d]; ++drumHits[d];
            if (d == 0) ++kickCount;
            if (recState == RecState::Recording && input.recordTarget == 3 && input.selected == 3) {
                Capture event; event.track = 3; event.isDrum = true; event.drum = d;
                event.note.start = nearestStep() % input.song.span(); emit(event);
            }
        }
    }
    void onTick() {
        if(!input.playing) { refreshVoices(true); return; }
        if ((recState == RecState::CountIn || recState == RecState::Armed) && absoluteTick >= recordAt) {
            recState = RecState::Recording; pending.fill(-1); captureLive();
        }
        if (absoluteTick < 0) {
            if (absoluteTick % 96 == 0) ++drumHits[7];
        } else if (absoluteTick % TicksPerStep == 0) {
            unsigned step = (absoluteTick % input.song.span()) / TicksPerStep;
            for (unsigned d = 0; d < DrumCount; ++d) if ((input.song.drums.steps[d] >> step) & 1) {
                ++drumHits[d]; if (d == 0) ++kickCount;
            }
        }
        refreshVoices(true);
    }
    void refreshVoices(bool tickEdge = false) {
        for (unsigned track = 0; track < 3; ++track) {
            Controls c;
            const auto& t = input.song.tracks[track];
            c.panic = panic; c.wave = t.wave<4?t.wave:0; c.octave = t.octave;
            c.volume = t.muted ? 0 : t.volume;
            ToolsSettings tools=t.tools;
            if(input.stepInput) {tools.arp=0;tools.hold=false;}
            uint32_t keys=!input.diagnostic && input.selected==track ? input.liveKeys : 0;
            PitchSet pressed=playedPitches(input.song.root,input.song.scale,t.octave,keys,tools,input.lastKey);
            if(input.selected!=track || input.diagnostic) arps[track].reset();
            livePitches[track]=arps[track].update(pressed,tools,absoluteTick);
            bool trigger=tickEdge && tools.arp && arps[track].onset(tools,absoluteTick);
            capturePlayed(track,livePitches[track],trigger);
            for(unsigned i=0;i<livePitches[track].count;++i) {
                unsigned midi=livePitches[track].notes[i];c.midiNotes[midi/32]|=1u<<(midi%32);
                if(trigger) c.retriggerNotes[midi/32]|=1u<<(midi%32);
            }
            if (input.playing && absoluteTick >= 0 && !input.diagnostic) {
                unsigned pos = absoluteTick % input.song.span();
                for (unsigned i = 0; i < t.count; ++i) {
                    const auto& event = t.events[i];
                    if (absoluteTick >= event.start && (pos + input.song.span() - event.start) % input.song.span() < event.duration)
                        c.midiNotes[event.midi / 32] |= 1u << (event.midi % 32);
                    if (tickEdge && pos == event.start) c.retriggerNotes[event.midi / 32] |= 1u << (event.midi % 32);
                }
            }
            if (track == 0 && input.diagnostic == 1 && diagnosticLeft) {
                c.wave = 0; c.octave = 4; c.volume = 140; c.notes = 1;
            }
            sampleTracks[track]=t.wave>=4&&!input.diagnostic;
            if(sampleApply)sampleApply(sampleContext,track,sampleTracks[track]?t.wave-4:255,c);
            if(sampleTracks[track]){Controls silent;silent.panic=panic;engines[track].apply(silent);}
            else engines[track].apply(c);
        }
        Controls c = input.song.drums;
        c.panic = panic; c.notes = 0; c.running = false; c.recording = false; c.hits = drumHits;
        c.volume = input.song.drumMuted ? 0 : input.song.drumVolume;
        if (input.diagnostic) {
            c.volume = 140; c.levels[0] = 255; c.muted = 0; c.solo = 1;
            c.drumTest = input.diagnostic == 2 ? 1 : input.diagnostic == 3 ? 2 : 0;
        }
        engines[3].apply(c);
    }
};
}
