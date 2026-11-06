#pragma once
#include "Workstation.h"
namespace pocket {
constexpr unsigned StepLengths[] = {16, 8, 4, 2, 1};
// Scratch destination must be distinct: commit only after success for atomic edits.
inline bool replaceSpan(const MelodyTrack& source, unsigned span, unsigned start, unsigned duration,
                        const NoteEvent* inserted, unsigned insertedCount, MelodyTrack& scratch) {
    if (&source == &scratch || !span || start >= span || !duration || duration > span || insertedCount > MaxEvents) return false;
    scratch = source; scratch.count = 0;
    auto append = [&](NoteEvent event) { if (scratch.count >= MaxEvents) return false; scratch.events[scratch.count++] = event; return true; };
    for (unsigned i=0;i<source.count;++i) {
        const auto& original = source.events[i]; int offset=(original.start+span-start)%span;
        for(int shift : {-int(span),0}) {
            int begin=std::max(int(duration),offset+shift), end=std::min(int(span),offset+shift+original.duration);
            if(end>begin) { auto note=original; note.start=(start+begin)%span; note.duration=end-begin; if(!append(note)) return false; }
        }
    }
    for(unsigned i=0;i<insertedCount;++i) {
        auto note=inserted[i];
        if(note.midi<24 || note.midi>119 || !note.duration || note.duration>span || note.start>=duration || note.start+note.duration>duration) return false;
        note.start=(start+note.start)%span; if(!append(note)) return false;
    }
    std::fill(scratch.events.begin()+scratch.count,scratch.events.end(),NoteEvent{}); return true;
}
inline bool overwriteSpan(const MelodyTrack& source,unsigned span,unsigned start,unsigned duration,
                          const uint8_t* pitches,unsigned pitchCount,MelodyTrack& scratch) {
    if(pitchCount>4) return false;
    NoteEvent notes[4];
    for(unsigned i=0;i<pitchCount;++i) { notes[i].start=0;notes[i].duration=duration;notes[i].midi=pitches[i]; }
    return replaceSpan(source,span,start,duration,notes,pitchCount,scratch);
}
inline bool mergeCaptured(const MelodyTrack& source,const Capture& capture,MelodyTrack& scratch) {
    scratch=source;
    unsigned count=capture.noteCount?capture.noteCount:1;
    if(count>4) return false;
    for(unsigned i=0;i<count;++i) {
        auto note=capture.noteCount?capture.notes[i]:capture.note;
        bool replaced=false;
        for(unsigned j=0;j<scratch.count;++j) if(scratch.events[j].midi==note.midi && scratch.events[j].start==note.start) {
            scratch.events[j]=note;replaced=true;break;
        }
        if(!replaced) {if(scratch.count==MaxEvents) return false;scratch.events[scratch.count++]=note;}
    }
    return true;
}
inline unsigned makeStepNotes(unsigned root,unsigned scale,unsigned octave,uint32_t keys,unsigned lastKey,
                              const ToolsSettings& settings,unsigned duration,NoteEvent* output,unsigned capacity) {
    PitchSet pitches=playedPitches(root,scale,octave,keys,settings,lastKey);
    if(!settings.chord) {unsigned count=0;for(unsigned k=0;k<24;++k) if(keys&(1u<<k)) ++count;if(count>4) return capacity+1;}
    unsigned count=0;
    if(!settings.arp) {
        for(unsigned i=0;i<pitches.count;++i) {if(count>=capacity)return capacity+1;auto& note=output[count++];note.start=0;note.duration=duration;note.midi=pitches.notes[i];note.velocity=100;}
    } else {
        ArpState arp;
        unsigned rate=arpRateTicks(settings), gate=std::max(1u,rate*settings.gate/100);
        for(unsigned tick=0;tick<duration;tick+=rate) {
            auto out=arp.update(pitches,settings,tick);
            if(out.count) {if(count>=capacity)return capacity+1;auto& note=output[count++];note.start=tick;note.duration=std::min(gate,duration-tick);note.midi=out.notes[0];note.velocity=100;}
        }
    }
    return count;
}
inline bool deleteAtStep(MelodyTrack& track, unsigned step) {
    unsigned count = 0;
    for (unsigned i = 0; i < track.count; ++i)
        if (track.events[i].start / TicksPerStep != step) track.events[count++] = track.events[i];
    bool changed = count != track.count; track.count = count;
    std::fill(track.events.begin() + count, track.events.end(), NoteEvent{});
    return changed;
}
}
