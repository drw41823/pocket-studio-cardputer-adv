#include "Workstation.h"
#include "StepEditor.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>
using namespace pocket;
void require(bool test, const char* name) { if (!test) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); } }
void render(Workstation& engine, unsigned frames) {
    std::array<int16_t, BlockSize> pcm{};
    while (frames) { unsigned n = std::min<unsigned>(frames, BlockSize); engine.render(pcm.data(), n); frames -= n; }
}
std::vector<Capture> events;
void capture(void*, const Capture& event) { events.push_back(event); }
double energy(Workstation& engine, unsigned frames = 2400) {
    double sum = 0; std::array<int16_t, 128> pcm{};
    for (unsigned offset = 0; offset < frames;) {
        unsigned n = std::min<unsigned>(frames - offset, pcm.size()); engine.render(pcm.data(), n);
        for (unsigned i = 0; i < n; ++i) { require(std::abs(int(pcm[i])) <= 32112, "mix bounded"); sum += double(pcm[i]) * pcm[i]; }
        offset += n;
    }
    return std::sqrt(sum / frames);
}
int main() {
    // Master changes affect the whole mix, without the former per-track gain clamp.
    for (unsigned track = 0; track < 3; ++track) {
        Workstation mixer; mixer.init(); Input mix;
        mix.selected = track; mix.liveKeys = 1; mix.song.tracks[track].volume = 200;
        mix.song.drums.volume = 100; mixer.apply(mix); render(mixer, SampleRate / 2);
        double normal = energy(mixer);
        mix.song.drums.volume = 150; mixer.apply(mix); render(mixer, SampleRate / 2);
        double raised = energy(mixer);
        require(raised / normal > 1.45 && raised / normal < 1.55, "master gain preserves each track ratio");
        mix.song.drums.volume = 0; mixer.apply(mix); render(mixer, SampleRate / 2);
        require(energy(mixer) == 0, "master zero silences all tracks");
    }

    {
        Project keySong;
        require(noteForKey('e') == 0 && noteForKey('z') == 12 && noteForKey('d') == 13 && noteForKey('f') == 15, "upper low / lower high keyboard");
        for (unsigned root = 0; root < 12; ++root) {
            keySong.root = root;
            for (unsigned octave = 2; octave <= 6; ++octave) {
                keySong.tracks[0].octave = octave;
                require(keyMidi(keySong, 0, 12) - keyMidi(keySong, 0, 0) == 12, "two octaves stay twelve semitones apart");
                require(keyMidi(keySong, 0, 0) == (octave + 1) * 12 + root && keyMidi(keySong, 0, 23) <= 119, "all roots and register bounds");
            }
        }
        keySong.root = 0; keySong.scale = 1; keySong.tracks[0].octave = 4;
        keySong.drums.steps[0] = 0x1001;
        for (auto& track : keySong.tracks) { track.count = 1; track.events[0] = {0, 24, 60, 100}; }
        require(transposeProject(keySong, 2), "C to D accepted");
        for (const auto& track : keySong.tracks) require(track.events[0].midi == 62, "all melody tracks transpose");
        require(keySong.drums.steps[0] == 0x1001, "drum steps do not transpose");
        auto bytes = encodeProject(keySong); Project restored;
        require(decodeProject(bytes.data(), bytes.size(), restored) && restored.root == 2 && restored.scale == 1 && restored.tracks[0].events[0].midi == 62, "key scale and notes persisted");
        // Recreate an old-format project, with CRC, to exercise default migration.
        for (unsigned i = 0; i < 6; ++i) bytes[i] = "PSYN03"[i];
        bytes[8] = 3; uint32_t crc = crc32(bytes.data(), ProjectBytes - 4);
        for (unsigned i = 0; i < 4; ++i) bytes[ProjectBytes - 4 + i] = crc >> (i * 8);
        require(decodeProject(bytes.data(), bytes.size(), restored) && restored.root == 0 && restored.scale == 0 && restored.tracks[0].events[0].midi == 62, "old projects default C major without rewriting notes");
        keySong.tracks[2].events[0].midi = 119;
        require(!transposeProject(keySong, 3) && keySong.root == 2 && keySong.tracks[0].events[0].midi == 62, "out of range transpose rejects atomically");
        require(scaleDegree(4, 0) == 2 && scaleDegree(3, 0) == -1 && scaleDegree(3, 1) == 2, "major/minor scale membership");
        Workstation recorder; recorder.init(); recorder.onCapture(nullptr, capture);
        Input take; take.song.root = 2; take.selected = take.recordTarget = 0;
        take.playing = take.record = take.countIn = true; take.epoch = 1; recorder.apply(take);
        render(recorder, SampleRate * 2 + 128);
        events.clear(); take.liveKeys = 1u << 12; recorder.apply(take); render(recorder, 3000);
        take.liveKeys = 0; recorder.apply(take);
        require(events.size() == 1 && events[0].note.midi == 74, "transposed upper-register keyboard capture stores actual D5");
        events.clear();
    }
    {
        MelodyTrack source, scratch;
        uint8_t chord[] = {60,64,67};
        for (unsigned steps : StepLengths) {
            source.count = 0;
            require(overwriteSpan(source, 768, 0, steps * 24, chord, 3, scratch), "all five step lengths accept chord");
            require(scratch.count == 3 && scratch.events[2].duration == steps * 24 && scratch.events[1].start == 0, "step chord duration and alignment");
        }
        source.count = 1; source.events[0] = {0,384,60,100};
        require(overwriteSpan(source,768,96,96,nullptr,0,scratch), "rest splits held note");
        require(scratch.count == 2, "split retains both outside pieces");
        bool before = false, after = false;
        for (unsigned i=0;i<scratch.count;++i) {
            before |= scratch.events[i].start == 0 && scratch.events[i].duration == 96;
            after |= scratch.events[i].start == 192 && scratch.events[i].duration == 192;
        }
        require(before && after, "rest leaves exact outside note segments");
        source.count=1; source.events[0]={744,96,60,100};
        require(overwriteSpan(source,768,0,24,nullptr,0,scratch), "rest cuts wrapped tail");
        require(scratch.count==2, "wrapped note splits into outside segments");
        for(unsigned i=0;i<scratch.count;++i) require(scratch.events[i].start != 0, "rest removes loop beginning");
        source.count=0;
        require(overwriteSpan(source,768,744,96,chord,3,scratch) && scratch.events[0].start==744 && scratch.events[0].duration==96, "new chord can cross loop end");
        require(deleteAtStep(scratch,31) && scratch.count==0, "delete removes complete chord onset");
        source.count=MaxEvents;
        for(unsigned i=0;i<MaxEvents;++i) source.events[i]={24,24,uint8_t(60+i%12),100};
        require(!overwriteSpan(source,768,0,24,chord,3,scratch) && source.count==MaxEvents && source.events[0].start==24, "capacity failure leaves original track intact");
        require(overwriteSpan(source,768,24,24,chord,3,scratch) && scratch.count==3, "full track can replace existing span");
        Project persisted; persisted.tracks[0]=scratch; auto blob=encodeProject(persisted); Project loaded;
        require(decodeProject(blob.data(),blob.size(),loaded) && loaded.tracks[0].count==3 && loaded.tracks[0].events[1].midi==64, "step chord saves and loads");
        // Coverage oracle: retained old-note ticks must equal old minus the rest,
        // including every cursor position, loop wrap and a full-span held note.
        for(unsigned cursor=0;cursor<16;++cursor) for(unsigned length : StepLengths) {
            source.count=1; source.events[0]={312,384,60,100};
            require(overwriteSpan(source,384,cursor*24,length*24,nullptr,0,scratch), "rest coverage operation");
            for(unsigned tick=0;tick<384;++tick) {
                bool audible=false;
                for(unsigned i=0;i<scratch.count;++i) audible |= (tick+384-scratch.events[i].start)%384 < scratch.events[i].duration;
                bool expected=(tick+384-cursor*24)%384 >= length*24;
                require(audible==expected,"rest coverage exact across loop boundary");
            }
        }
    }
    {
        ToolsSettings tools;tools.chord=true;
        const uint8_t majorTriads[7][3]={{60,64,67},{62,65,69},{64,67,71},{65,69,72},{67,71,74},{69,72,76},{71,74,77}};
        for(unsigned root=0;root<12;++root) for(unsigned degree=0;degree<7;++degree) {
            unsigned key=ScaleIntervals[0][degree];auto notes=playedPitches(root,0,4,1u<<key,tools,key);
            require(notes.count==3,"all major degrees generate complete triads");
            for(unsigned i=0;i<3;++i) require(notes.notes[i]==majorTriads[degree][i]+root,"major triad inversion-free pitches across twelve keys");
        }
        const uint8_t minorTriads[7][3]={{60,63,67},{62,65,68},{63,67,70},{65,68,72},{67,70,74},{68,72,75},{70,74,77}};
        for(unsigned root=0;root<12;++root) for(unsigned degree=0;degree<7;++degree) {
            unsigned key=ScaleIntervals[1][degree];auto notes=playedPitches(root,1,4,1u<<key,tools,key);
            for(unsigned i=0;i<3;++i) require(notes.notes[i]==minorTriads[degree][i]+root,"all natural-minor triads across twelve roots");
        }
        auto high=playedPitches(11,0,6,1u<<23,tools,23);
        require(high.count==3 && high.notes[2]<=119 && high.notes[0]%12==10,"top-register chord stays within range and preserves pitch classes");
        auto minor=playedPitches(0,1,4,1,tools,0);require(minor.count==3 && minor.notes[1]==63,"natural minor tonic triad");
        auto chromatic=playedPitches(0,0,4,2,tools,1);require(chromatic.count==1 && chromatic.notes[0]==61,"out of scale key remains a single pitch");
        tools.arp=1;PitchSet chord=playedPitches(0,0,4,1,tools,0);
        const unsigned up[]={60,64,67,60}, down[]={67,64,60,67}, bounce[]={60,64,67,64,60};
        for(unsigned style=1;style<=3;++style) {
            tools.arp=style;ArpState arp;
            for(unsigned i=0;i<(style==3?5u:4u);++i) {
                auto out=arp.update(chord,tools,i*24);unsigned expected=style==1?up[i]:style==2?down[i]:bounce[i];
                require(out.count==1 && out.notes[0]==expected,"arp direction and endpoint ordering");
                require(!arp.update(chord,tools,i*24+12).count,"fifty percent gate releases at half step");
            }
        }
        tools.arp=1;tools.hold=true;ArpState held;
        held.update(chord,tools,0);require(held.update({},tools,24).count==1,"hold continues after key release");
        held.reset();require(!held.update({},tools,48).count,"reset releases held bank");
        held.update(chord,tools,0);auto nextChord=playedPitches(0,0,4,1u<<9,tools,9);
        require(held.update(nextChord,tools,30).notes[0]==69 && held.update({},tools,54).notes[0]==72,"HOLD replaces old chord and restarts new root");
        ToolsSettings off=tools;off.arp=0;require(!held.update({},off,55).count && !held.update({},tools,56).count,"ARP off releases latch permanently");
        tools.hold=false;tools.octaves=2;ArpState expanded;
        unsigned twoOctaves[]={60,64,67,72,76,79};
        for(unsigned i=0;i<6;++i) require(expanded.update(chord,tools,i*24).notes[0]==twoOctaves[i],"two octave arp bank");
        tools.octaves=1;
        std::array<NoteEvent,32> pattern;
        unsigned count=makeStepNotes(0,0,4,1,0,tools,96,pattern.data(),pattern.size());
        require(count==4 && pattern[2].start==48 && pattern[2].midi==67 && pattern[2].duration==12,"STEP bakes arp timing and gate");
        tools.arp=0;count=makeStepNotes(0,0,4,1u<<9,9,tools,96,pattern.data(),pattern.size());
        require(count==3 && pattern[0].midi==69 && pattern[1].midi==72 && pattern[2].midi==76,"STEP sixth-degree minor chord");
        Project configured;configured.tracks[1].tools=tools;configured.tracks[1].tools.arp=3;configured.tracks[1].tools.hold=true;configured.tracks[1].tools.gate=75;
        auto encodedTools=encodeProject(configured);Project loadedTools;
        require(decodeProject(encodedTools.data(),encodedTools.size(),loadedTools) && loadedTools.tracks[1].tools.chord && loadedTools.tracks[1].tools.arp==3 && loadedTools.tracks[1].tools.hold && loadedTools.tracks[1].tools.gate==75,"tools settings persistence");
        {
            auto invalid=encodedTools;unsigned metadata=12+SongBytes+8+MaxEvents*6;
            invalid[metadata+7]=101;uint32_t crc=crc32(invalid.data(),ProjectBytes-4);
            for(unsigned i=0;i<4;++i) invalid[ProjectBytes-4+i]=crc>>(8*i);
            require(!decodeProject(invalid.data(),invalid.size(),loadedTools),"invalid persisted gate rejected after valid CRC");
        }
        // Old PSYN04 metadata reserves these bytes and must not enable tools.
        for(unsigned i=0;i<6;++i) encodedTools[i]="PSYN04"[i];encodedTools[8]=4;
        uint32_t oldCrc=crc32(encodedTools.data(),ProjectBytes-4);
        for(unsigned i=0;i<4;++i) encodedTools[ProjectBytes-4+i]=oldCrc>>(8*i);
        require(decodeProject(encodedTools.data(),encodedTools.size(),loadedTools) && !loadedTools.tracks[1].tools.chord && !loadedTools.tracks[1].tools.arp,"old project defaults disable tools");
        // A captured chord either fully fits or leaves the source untouched.
        MelodyTrack full,scratch;full.count=127;
        for(unsigned i=0;i<full.count;++i) full.events[i]={24,24,uint8_t(48+i%12),100};
        Capture batch;batch.noteCount=3;for(unsigned i=0;i<3;++i) batch.notes[i]={0,96,majorTriads[0][i],100};
        require(!mergeCaptured(full,batch,scratch) && full.count==127,"recorded chord capacity is atomic");
        full.count=0;require(mergeCaptured(full,batch,scratch) && scratch.count==3,"recorded chord group merges completely");
        Workstation chordRecorder;chordRecorder.init();chordRecorder.onCapture(nullptr,capture);
        Input rec;rec.song.tracks[0].tools.chord=true;rec.record=rec.playing=rec.countIn=true;rec.epoch=1;
        chordRecorder.apply(rec);render(chordRecorder,48001);events.clear();rec.liveKeys=1;chordRecorder.apply(rec);render(chordRecorder,6000);
        rec.liveKeys=0;chordRecorder.apply(rec);require(events.size()==1 && events[0].noteCount==3 && events[0].notes[1].midi==64,"live chord recorded as atomic three-note group");
        Workstation arpRecorder;arpRecorder.init();arpRecorder.onCapture(nullptr,capture);
        rec=Input{};rec.song.tracks[0].tools.chord=true;rec.song.tracks[0].tools.arp=1;rec.record=rec.playing=rec.countIn=true;rec.epoch=1;
        arpRecorder.apply(rec);render(arpRecorder,48001);events.clear();rec.liveKeys=1;arpRecorder.apply(rec);render(arpRecorder,12000);
        require(events.size()==4,"four quarter-beat arp events captured");
        for(unsigned i=0;i<4;++i) require(events[i].note.start==i*24 && events[i].note.duration==12 && events[i].note.midi==up[i],"record actual arp pitches and substep gate");
        rec.liveKeys=0;rec.record=false;arpRecorder.apply(rec);events.clear();
        {
            Workstation retrigger;retrigger.init();retrigger.onCapture(nullptr,capture);Input note;
            note.song.tracks[0].tools.arp=1;note.song.tracks[0].tools.gate=100;
            note.record=note.playing=note.countIn=true;note.epoch=1;retrigger.apply(note);render(retrigger,48001);
            events.clear();note.liveKeys=1;retrigger.apply(note);render(retrigger,9001);
            require(events.size()==3,"full-gate repeated note retriggers captured");
            for(unsigned i=0;i<3;++i) require(events[i].note.midi==60 && events[i].note.start==i*24 && events[i].note.duration==24,"full-gate repeated note records distinct exact events");
            events.clear();
        }
        // Audio must be identical when UI refreshes repeatedly while transport stops.
        Workstation continuous,refreshed;continuous.init();refreshed.init();Input live;
        live.song.tracks[0].tools.chord=true;live.song.tracks[0].tools.arp=1;live.liveKeys=1;
        continuous.apply(live);refreshed.apply(live);
        std::vector<int16_t> reference(12000), split(12000);continuous.render(reference.data(),reference.size());
        for(unsigned offset=0;offset<split.size();) {refreshed.apply(live);unsigned n=std::min<unsigned>(73,split.size()-offset);refreshed.render(split.data()+offset,n);offset+=n;}
        require(reference==split,"stopped arp clock independent of block size and UI refresh");
        live.song.tracks[0].tools.hold=true;refreshed.apply(live);render(refreshed,100);live.liveKeys=0;refreshed.apply(live);
        require(energy(refreshed,6000)>100,"held arp sounds after key release");
        ++live.liveReset;refreshed.apply(live);render(refreshed,SampleRate/2);
        require(energy(refreshed)==0,"UI reset clears held arp without restarting loop");
        {
            Workstation mutedLoop;mutedLoop.init();Input loop;loop.playing=true;loop.epoch=1;
            loop.song.tracks[0].count=1;loop.song.tracks[0].events[0]={0,768,60,100};mutedLoop.apply(loop);
            render(mutedLoop,6000);int position=mutedLoop.position();loop.song.tracks[0].muted=true;mutedLoop.apply(loop);
            require(mutedLoop.position()==position,"track mute does not restart transport");render(mutedLoop,SampleRate/2);
            require(energy(mutedLoop)==0,"muted selected track goes silent");loop.song.tracks[0].muted=false;mutedLoop.apply(loop);render(mutedLoop,6000);
            require(energy(mutedLoop)>100,"unmuting restores held sequenced note");
        }
        // Playback of generated events ignores tools, preventing double arpeggiation.
        Workstation baked; baked.init();Input play;play.playing=true;play.epoch=1;
        play.song.tracks[0].tools.chord=true;play.song.tracks[0].tools.arp=1;play.song.tracks[0].count=1;play.song.tracks[0].events[0]={0,12,60,100};
        baked.apply(play);require(energy(baked,1500)>100,"baked event plays with tools enabled");render(baked,SampleRate/2);require(energy(baked)==0,"baked playback does not synthesize extra chord/arp notes");
    }
    Project song;
    require(song.drums.length == 32 && song.tracks[0].count == 0 && song.drums.steps[0] == 0, "blank two-bar project");
    song.tracks[0].count = 2;
    song.tracks[0].events[0] = {0, 48, 60, 100}; song.tracks[0].events[1] = {744, 48, 67, 80};
    song.tracks[1].wave = 2; song.tracks[1].muted = true; song.drumVolume = 85;
    song.drums.steps[0] = uint64_t(1) << 31;
    auto encoded = encodeProject(song); Project decoded;
    require(decodeProject(encoded.data(), encoded.size(), decoded) && decoded.tracks[0].count == 2
        && decoded.tracks[0].events[1].midi == 67 && decoded.tracks[1].muted && decoded.drumVolume == 85
        && decoded.drums.steps[0] == song.drums.steps[0], "full project roundtrip");
    encoded[150] ^= 1; Project original = decoded;
    require(!decodeProject(encoded.data(), encoded.size(), decoded) && decoded.tracks[0].count == original.tracks[0].count, "corrupt project rejected without mutation");
    require(!decodeProject(encoded.data(), 200, decoded), "truncated project rejected");
    auto old = encodeSong(song.drums);
    require(decodeProject(old.data(), old.size(), decoded) && decoded.drums.steps[0] == song.drums.steps[0]
        && decoded.tracks[0].count == 0, "legacy drums imported");
    song.tracks[0].count = MaxEvents + 1; encoded = encodeProject(song);
    require(!decodeProject(encoded.data(), encoded.size(), decoded), "event capacity validation");

    Workstation engine; engine.init(); engine.onCapture(nullptr, capture); Input input;
    input.epoch = 1; input.playing = true; input.record = true; input.countIn = true; input.recordSession = 7;
    engine.apply(input); require(engine.countBeat() == 1 && engine.recording() == RecState::CountIn, "four beat count-in starts");
    render(engine, SampleRate * 2); require(engine.recording() == RecState::CountIn, "count-in lasts one full bar");
    render(engine, 1); require(engine.recording() == RecState::Recording && engine.position() == 0, "recording starts exactly after count-in");
    input.liveKeys = 1; engine.apply(input); render(engine, 6000); input.liveKeys = 0; engine.apply(input);
    require(events.size() == 1 && !events[0].isDrum && events[0].session == 7
        && events[0].note.start == 0 && events[0].note.duration == 48 && events[0].note.midi == 60, "record pitch and duration");
    input.liveKeys = 2; engine.apply(input); render(engine, 3000); input.record = false; engine.apply(input);
    require(events.size() == 2 && events[1].note.midi == 61 && events[1].note.duration == 24, "ending REC flushes held note");
    require(engine.recording() == RecState::Off && input.playing, "ending REC keeps transport playing");
    input.liveKeys = 0; input.recordTarget = input.selected = 3; input.record = true; input.countIn = false; engine.apply(input);
    require(engine.recording() == RecState::Armed, "arm at next bar during playback");
    render(engine, 48000 - 9001); render(engine, 1);
    require(engine.recording() == RecState::Recording && engine.position() == 16, "armed enters REC at next bar");
    input.song.drums.hits[0]++; engine.apply(input);
    require(events.size() == 3 && events.back().isDrum && events.back().note.start == 384, "drum capture in same musical clock");
    input.record = false; input.playing = false; ++input.epoch; engine.apply(input); render(engine, 12000);
    require(energy(engine) == 0, "stop releases all audio");

    for (unsigned track = 0; track < 3; ++track) {
        Workstation one; one.init(); Input state; state.epoch = 1; state.playing = true;
        state.song.tracks[track].count = 1; state.song.tracks[track].events[0] = {0, 96, uint8_t(60 + track * 5), 100};
        one.apply(state); require(energy(one) > 500, "each melody track plays recorded MIDI");
        state.song.tracks[track].muted = true; one.apply(state); render(one, 12000);
        require(energy(one) == 0, "each melody track mute");
    }
    {
        Workstation swing; swing.init(); Input state; state.epoch = 1; state.playing = true; state.song.drums.swing = 50;
        swing.apply(state); render(swing, 4500); require(swing.position() == 0, "swing long step");
        render(swing, 1); require(swing.position() == 1, "swing long step boundary");
        render(swing, 1499); require(swing.position() == 1, "swing short step");
        render(swing, 1); require(swing.position() == 2, "swing short step boundary");
    }
    {
        Workstation wrap; wrap.init(); Input state; state.epoch = 1; state.playing = true;
        state.song.drums.length = 16; state.song.tracks[0].count = 1;
        state.song.tracks[0].events[0] = {360, 48, 60, 100}; wrap.apply(state);
        require(energy(wrap, 3000) == 0, "wrapping note has no ghost onset on first play");
        render(wrap, 42000); require(energy(wrap, 6000) > 500, "note sustains across loop wrap");
    }
    {
        Workstation wrapRecord; wrapRecord.init(); wrapRecord.onCapture(nullptr, capture);
        Input state; state.epoch = 1; state.playing = true; state.record = true; state.countIn = true;
        state.song.drums.length = 16; wrapRecord.apply(state); render(wrapRecord, 93001);
        state.liveKeys = 1; wrapRecord.apply(state); render(wrapRecord, 6000); state.liveKeys = 0; wrapRecord.apply(state);
        require(events.back().note.start == 360 && events.back().note.duration == 48, "record duration crosses loop wrap");
    }
    for (unsigned test = 1; test <= 4; ++test) {
        Workstation diagnostic; diagnostic.init(); Input state; state.epoch = 1;
        state.song.drums.volume = 0; state.song.drumMuted = true; state.song.drums.muted = 255;
        state.song.drums.solo = 128; state.song.drums.levels[0] = 0;
        state.diagnostic = test; state.diagnosticTrigger = 1; diagnostic.apply(state);
        double level = energy(diagnostic); std::printf("Diagnostic %u RMS %.1f\n", test, level);
        require(level > (test == 2 ? 100 : 300), "diagnostic tone bypasses user mixer");
    }
    Workstation dense; dense.init(); Input state; state.epoch = 1; state.playing = true; state.song.drums.length = 64;
    state.song.drums.bpm = 240; state.song.drums.swing = 50; state.song.drums.volume = 200;
    for (auto& bits : state.song.drums.steps) bits = ~uint64_t(0);
    for (auto& track : state.song.tracks) {
        track.wave = 2; track.volume = 200; track.count = 8;
        for (unsigned i = 0; i < 8; ++i) track.events[i] = {0, 1536, uint8_t(48 + i), 100};
    }
    dense.apply(state); auto before = std::chrono::steady_clock::now();
    require(energy(dense, SampleRate * 4) > 1000, "dense four-track playback");
    std::printf("Host dense 4s render: %lld ms\n", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count());
    std::puts("PASS: four tracks, STEP edits, key/scale, diatonic chords, ARP timing/hold/recording, mute/master, legacy migration and persistence");
}
