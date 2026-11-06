#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <atomic>
#include <cstring>
#include "Workstation.h"
#include "SynthChecks.h"
#include "SavedProject.h"
#include "StepEditor.h"
#include "SamplerEngine.h"
#include "SamplerPatch.h"
#include "WavReader.h"
#include "SampleEdit.h"
#include "SampleTrigger.h"
#include "SampleMemory.h"
#include "AudioPauseGate.h"
#include "SamplerStorage.h"
#include "SamplePreview.h"
#include "SampleBase.h"
#include "TakeTrim.h"
#include "TakePreviewAudio.h"
#include "ToneNavigation.h"
#include "SamplerNavigation.h"
#include "PlayerBrowser.h"
#include "PlayerMP3.h"
#include "AudioStream.h"
#include <cstdlib>
#include "ui_theme.h"
#include "adv_keyboard.h"

namespace {
using namespace pocket;
StudioAdvKeyboard* studioKeyboard=nullptr;
void serviceKeyboardHardware(){if(studioKeyboard)studioKeyboard->pollHardware();}
namespace ui = studio_ui;
Input input, sharedInput;
Project undoSong;
Workstation workstation;
enum class Workspace : uint8_t { Home, Synth, Sampler, Player };
Workspace workspace = Workspace::Home, sharedWorkspace = Workspace::Home;
void renderPlayer(int16_t*,unsigned);void tickPlayer();void closePlayer();bool enterPlayer();void drawPlayer();void scanPlayer(const uint8_t*,bool,bool,bool);
SamplerEngine sampler;
std::atomic<bool> takePreviewRouting{false};
void renderTakePreview(int16_t*,unsigned);
struct SynthSample {const int16_t* data=nullptr;unsigned frames=0,rate=24000;SampleSettings sound;uint32_t generation=0;};
SynthSample synthSamples[4],sharedSynthSamples[4],audioSynthSamples[4];
SamplerEngine synthSamplers[3];
unsigned audioSampleSlots[3]={255,255,255};
uint32_t audioSampleGeneration[3]{};
SamplePatch samplePatch;
SampleControl sampleControl, sharedSampleControl;
std::atomic<unsigned> samplerPosition{0}, samplerVoices{0};
unsigned samplerOctave = 4;
SynthEngine bootEngine;
portMUX_TYPE inputLock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<int> playStep{-1}, countBeat{0}, recStatus{0}, outputPeak{0};
std::atomic<uint32_t> blocksRendered{0}, kickTriggers{0};
std::atomic<uint32_t> renderWorstUs{0};
std::atomic<bool> audioReady{false};
AudioPauseGate audioGate;
void resumeAudio(){audioGate.resume();}
std::atomic<unsigned> droppedRecords{0};
QueueHandle_t captures = nullptr;
int16_t pcm[3][BlockSize];
std::atomic<bool> occupied[3]{};
M5Canvas screen(&M5Cardputer.Display);
bool uiReady = false, sdOK = false, checksOK = false, overview = true, diagnostics = false;
bool dirty = true, undoValid = false;
bool stepMode = false, autoAdvance = true, confirmClear = false;
unsigned clearTarget = 0;
bool toolsPage = false;
unsigned toolsField = 0;
ToolsSettings pendingTools;
std::array<NoteEvent,32> stepPattern;
const char* arpNames[] = {"OFF", "UP", "DOWN", "UPDOWN"};
uint32_t navRepeatAt[4]{};
std::array<NoteEvent, MaxEvents> noteClipboard{};
unsigned clipboardCount = 0, clipboardSteps = 1;
unsigned editStep = 0, lengthIndex = 4;
uint32_t pendingStepKeys = 0;
unsigned pendingStepTrack = 0, pendingStepCursor = 0, pendingStepLength = 1;
MelodyTrack stepScratch;
bool musicPage = false;
unsigned musicField = 0, pendingRoot = 0, pendingScale = 0;
bool filesPage = false, useSD = false, localOK = false, renameMode = false, confirmFile = false;
bool localWriteChecked = false;
unsigned fileAction = 0;
Preferences localStore;
enum class SlotState : uint8_t { Empty, Saved, Recovered, Invalid, Unavailable };
struct SlotInfo {
    SlotState state = SlotState::Empty;
    std::array<char, TitleBytes> title{};
    uint16_t bpm = 120, notes = 0;
    uint8_t length = 32;
    uint32_t revision = 0;
};
std::array<SlotInfo, 4> slots;
std::array<uint8_t, SavedProjectBytes> storageReadBuffer{}, storageWriteBuffer{};
SavedProject storageScratch;
std::array<char, TitleBytes> renameText{};
uint32_t loadedFingerprint = 0;
char storageResult[40]{};
unsigned subpage = 0, selectedDrum = 0, stepPage = 0, saveSlot = 0, presetIndex = 0;
unsigned drumEditStep = 0, drumScroll = 0;
uint8_t oldKeys[128]{}, blockedKeys[128]{};
bool oldTab = false, oldEnter = false, oldDelete = false;
int battery = -1, batteryMV = -1, shownPeak = 0;
uint32_t batteryAt = 0, messageUntil = 0, flashUntil = 0, peakUntil = 0;
uint32_t lastHit[DrumCount]{};
char message[40]{};
const char* names[] = {"S1", "S2", "S3", "S4"};
const char* waves[] = {"SINE", "TRI", "SQUARE", "SAW", "SMP1", "SMP2", "SMP3", "SMP4"};
const char* drumPages[] = {"GRID", "PAD", "MIX"};
// Only presentation order changes; pad keys and saved drum indices stay intact.
constexpr unsigned drumOrder[] = {0, 1, 3, 4, 2, 5, 6, 7};
constexpr uint16_t drumColors[] = {0xfd20, 0xf81f, 0xfbe0, 0x07ff, 0x07e0, 0x9dff, 0x5cff, 0xfc90};
const char* synthPages[] = {"PLAY", "NOTES", "SOUND"};
void drawUI();
unsigned drumRow() {
    for (unsigned row = 0; row < DrumCount; ++row) if (drumOrder[row] == selectedDrum) return row;
    return 0;
}
void revealDrum() {
    unsigned row = drumRow();
    if (row < drumScroll) drumScroll = row;
    if (row >= drumScroll + 4) drumScroll = row - 3;
    drumEditStep %= input.song.drums.length;
}
void moveDrum(bool up) {
    selectedDrum = drumOrder[(drumRow() + (up ? DrumCount - 1 : 1)) % DrumCount];
    revealDrum();
}

void tell(const char* text) { snprintf(message, sizeof(message), "%s", text); messageUntil = millis() + 2600; dirty = true; }
void publish() {
    sampleControl.sound = samplePatch.sound;
    sampleControl.base = (samplerOctave + 1) * 12 + input.song.root;
    sampleControl.master = input.song.drums.volume;
    portENTER_CRITICAL(&inputLock);
    sharedInput = input; sharedSampleControl = sampleControl; sharedWorkspace = workspace;
    for(unsigned i=0;i<4;++i)sharedSynthSamples[i]=synthSamples[i];
    portEXIT_CRITICAL(&inputLock);
}
void rememberUndo() { if (!input.record) { undoSong = input.song; undoValid = true; } }
void stop(bool invalidate = false) {
    pendingStepKeys = 0;
    input.playing = false; input.record = false; input.countIn = false; input.liveKeys = 0;
    ++input.epoch; if (invalidate) ++input.recordSession;
}
void undo() {
    if (!undoValid) { tell("Nothing to undo"); return; }
    stop(true); input.song = undoSong; undoValid = false; stepPage = 0; editStep = 0; drumEditStep %= input.song.drums.length; tell("Undo complete");
}
void toggleRecord() {
    if (stepMode && input.selected < 3) { tell("STEP: use piano or ENTER for rest"); return; }
    if (input.record) { input.record = false; tell("REC off - loop keeps playing"); return; }
    rememberUndo(); ++input.recordSession; input.recordTarget = input.selected;
    input.countIn = !input.playing; input.record = true;
    if (!input.playing) { input.playing = true; ++input.epoch; }
    tell(input.countIn ? "Count 4 beats, then play" : "Armed for next bar");
}
void captureCallback(void*, const Capture& event) {
    if (xQueueSend(captures, &event, 0) != pdTRUE) ++droppedRecords;
}
void consumeCaptures() {
    Capture event;
    while (captures && xQueueReceive(captures, &event, 0) == pdTRUE) {
        if (event.session != input.recordSession || event.track > 3) continue;
        if (event.isDrum && event.drum < DrumCount) {
            input.song.drums.steps[event.drum] |= uint64_t(1) << (event.note.start / TicksPerStep);
        } else if (event.track < 3) {
            auto& track = input.song.tracks[event.track];
            if (!mergeCaptured(track,event,stepScratch)) { tell("Track full - capture group kept out"); continue; }
            track=stepScratch;
        }
        dirty = true;
    }
    if (droppedRecords.exchange(0)) tell("REC overflow - some notes lost");
}
void slotDefault(unsigned slot, SavedProject& record) {
    snprintf(record.title.data(), record.title.size(), "SLOT %u", slot + 1); record.revision = 0;
}
bool readFileRecord(const char* path, SavedProject& record) {
    File file = SD.open(path, FILE_READ);
    if (!file || (file.size() != SavedProjectBytes && file.size() != ProjectBytes && file.size() != SongBytes)) { file.close(); return false; }
    unsigned got = file.read(storageReadBuffer.data(), storageReadBuffer.size()); file.close();
    return decodeSavedProject(storageReadBuffer.data(), got, record);
}
void paths(unsigned slot, char* final, char* temp, char* backup) {
    snprintf(final, 40, "/PocketSynth/project%u.psyn", slot + 1);
    snprintf(temp, 40, "/PocketSynth/project%u.tmp", slot + 1);
    snprintf(backup, 40, "/PocketSynth/project%u.prev", slot + 1);
}
SlotState readSlot(unsigned slot, SavedProject& record) {
    slotDefault(slot, record);
    if (!useSD) {
        if (!localOK) return SlotState::Unavailable;
        char key[8]; snprintf(key, sizeof(key), "slot%u", slot + 1);
        size_t size = localStore.getBytesLength(key);
        if (!size) return SlotState::Empty;
        if (size != SavedProjectBytes) return SlotState::Invalid;
        size_t got = localStore.getBytes(key, storageReadBuffer.data(), storageReadBuffer.size());
        return decodeSavedProject(storageReadBuffer.data(), got, record) ? SlotState::Saved : SlotState::Invalid;
    }
    if (!sdOK) return SlotState::Unavailable;
    char final[40], temp[40], backup[40]; paths(slot, final, temp, backup);
    bool exists = SD.exists(final) || SD.exists(backup);
    if (readFileRecord(final, record)) return SlotState::Saved;
    if (readFileRecord(backup, record)) return SlotState::Recovered;
    snprintf(final, sizeof(final), "/PocketSynth/slot%u.psyn", slot + 1);
    exists |= SD.exists(final);
    if (readFileRecord(final, record)) return SlotState::Saved;
    snprintf(backup, sizeof(backup), "/PocketSynth/slot%u.prev", slot + 1);
    exists |= SD.exists(backup);
    if (readFileRecord(backup, record)) return SlotState::Recovered;
    return exists ? SlotState::Invalid : SlotState::Empty;
}
void refreshSlots() {
    for (unsigned i = 0; i < slots.size(); ++i) {
        auto& info = slots[i];
        info.state = readSlot(i, storageScratch);
        info.title = storageScratch.title; info.revision = storageScratch.revision;
        info.notes = 0;
        if (info.state == SlotState::Saved || info.state == SlotState::Recovered) {
            info.bpm = storageScratch.song.drums.bpm; info.length = storageScratch.song.drums.length;
            for (auto& track : storageScratch.song.tracks) info.notes += track.count;
            uint64_t mask = info.length == 64 ? ~uint64_t(0) : (uint64_t(1) << info.length) - 1;
            for (auto bits : storageScratch.song.drums.steps) info.notes += __builtin_popcountll(bits & mask);
        }
    }
    dirty = true;
}
void fileResult(const char* result) {
    snprintf(storageResult, sizeof(storageResult), "%s", result); tell(result);
}
bool pauseForFlash() {
    if (!audioReady.load()) return true;
    publish(); uint32_t ticket=audioGate.request();
    uint32_t start = millis();
    while (!audioGate.acknowledged(ticket) && millis() - start < 500) delay(1);
    if (!audioGate.acknowledged(ticket)) { resumeAudio(); fileResult("Audio busy - save cancelled"); return false; }
    // The library's DMA can still contain the final queued samples.
    // Give its idle mixer enough time to replace those with silence.
    delay(12);
    return true;
}
bool writeSlot(const Project& song, const char* title, uint32_t revision) {
    encodeSavedProjectTo(song, title, revision, storageWriteBuffer.data());
    if (!useSD) {
        if (!localOK) { fileResult("LOCAL storage unavailable"); return false; }
        if (!pauseForFlash()) return false;
        char key[8]; snprintf(key, sizeof(key), "slot%u", saveSlot + 1);
        size_t written = localStore.putBytes(key, storageWriteBuffer.data(), storageWriteBuffer.size());
        size_t got = localStore.getBytes(key, storageReadBuffer.data(), storageReadBuffer.size());
        bool valid = written == SavedProjectBytes && got == SavedProjectBytes
            && storageReadBuffer == storageWriteBuffer && decodeSavedProject(storageReadBuffer.data(), got, storageScratch);
        resumeAudio();
        if (!valid) { fileResult(written ? "LOCAL read-back check failed" : "LOCAL full / write failed"); return false; }
        return true;
    }
    if (!sdOK) { fileResult("SD not inserted"); return false; }
    if (!SD.exists("/PocketSynth") && !SD.mkdir("/PocketSynth")) { fileResult("SD directory failed"); return false; }
    char final[40], temp[40], backup[40]; paths(saveSlot, final, temp, backup);
    File file = SD.open(temp, FILE_WRITE);
    if (!file) { fileResult("SD write failed"); return false; }
    unsigned written = file.write(storageWriteBuffer.data(), storageWriteBuffer.size()); file.flush(); file.close();
    if (written != SavedProjectBytes || !readFileRecord(temp, storageScratch)) { fileResult("SD verification failed"); return false; }
    if (SD.exists(final)) {
        if (SD.exists(backup) && !SD.remove(backup)) { fileResult("SD backup failed"); return false; }
        if (!SD.rename(final, backup)) { fileResult("SD backup rename failed"); return false; }
    }
    if (!SD.rename(temp, final)) {
        if (!SD.exists(final) && SD.exists(backup)) SD.rename(backup, final);
        fileResult("SD commit failed"); return false;
    }
    return true;
}
bool canStore() {
    if (input.record || recStatus.load() != int(RecState::Off) || (captures && uxQueueMessagesWaiting(captures))) {
        fileResult("End REC, wait, then save/load"); return false;
    }
    return true;
}
void saveProject() {
    if (!canStore()) return;
    fileResult("Saving... please wait"); drawUI();
    uint32_t fingerprint = projectFingerprint(input.song);
    if (!writeSlot(input.song, slots[saveSlot].title.data(), slots[saveSlot].revision + 1)) { refreshSlots(); return; }
    loadedFingerprint = fingerprint; refreshSlots();
    char result[40]; snprintf(result, sizeof(result), "SAVED %s SLOT %u - VERIFIED", useSD ? "SD" : "LOCAL", saveSlot + 1); fileResult(result);
}
void loadProject(bool boot = false) {
    if (!boot && !canStore()) return;
    SlotState state = readSlot(saveSlot, storageScratch);
    if (state != SlotState::Saved && state != SlotState::Recovered) { if (!boot) fileResult("Slot empty / invalid"); return; }
    rememberUndo(); stop(true); input.song = storageScratch.song; stepPage = 0; editStep = 0; drumEditStep = 0;
    loadedFingerprint = projectFingerprint(input.song);
    char result[40]; snprintf(result, sizeof(result), "%s %s SLOT %u", state == SlotState::Recovered ? "RECOVERED" : "LOADED", useSD ? "SD" : "LOCAL", saveSlot + 1); fileResult(result);
}
void openFiles(unsigned action) {
    filesPage = true; diagnostics = false; input.diagnostic = 0; input.liveKeys = 0;
    renameMode = confirmFile = false; fileAction = action;
    storageResult[0] = 0; refreshSlots();
}
void scanFiles(const uint8_t* held, bool enter, bool backspace) {
    const auto& special = M5Cardputer.Keyboard.keysState();
    auto pressed = [&](char key) { return held[unsigned(key)] && !oldKeys[unsigned(key)]; };
    input.liveKeys = 0;
    if (renameMode) {
        unsigned size = strlen(renameText.data());
        if (backspace && size) { renameText[--size] = 0; dirty = true; }
        if (!special.ctrl && !special.fn) for (const auto& point : M5Cardputer.Keyboard.keyList()) {
            char raw = M5Cardputer.Keyboard.getKeyValue(point).value_first;
            if (raw < 32 || raw >= 127 || !pressed(raw)) continue;
            char key = M5Cardputer.Keyboard.getKey(point);
            if (key >= 32 && key < 127 && size < TitleBytes - 1) {
                renameText[size++] = key; renameText[size] = 0; dirty = true;
            }
        }
        if (enter) {
            if (!size) { fileResult("Please enter a name"); return; }
            if (!canStore()) return;
            SlotState state = readSlot(saveSlot, storageScratch);
            if (state == SlotState::Saved || state == SlotState::Recovered) {
                if (!writeSlot(storageScratch.song, renameText.data(), storageScratch.revision + 1)) return;
                refreshSlots(); fileResult("NAME SAVED - VERIFIED");
            } else if (state == SlotState::Empty) {
                slots[saveSlot].title = renameText; fileResult("Name ready - select SAVE");
            } else { fileResult("Slot unavailable / invalid"); return; }
            renameMode = false; dirty = true;
        }
        return;
    }
    if (special.ctrl) {
        if (pressed('s')) { fileAction = 0; confirmFile = false; dirty = true; }
        if (pressed('l')) { fileAction = 1; confirmFile = false; dirty = true; }
        return;
    }
    bool up = pressed(';'), down = pressed('.'), left = pressed(','), right = pressed('/');
    if (up || down) { saveSlot = (saveSlot + (up ? 3 : 1)) % 4; confirmFile = false; storageResult[0] = 0; dirty = true; }
    if (left || right) { fileAction = (fileAction + (left ? 3 : 1)) % 4; confirmFile = false; storageResult[0] = 0; dirty = true; }
    if (backspace) { confirmFile = false; dirty = true; }
    if (!enter) return;
    if (fileAction == 3) {
        useSD = !useSD; confirmFile = false; storageResult[0] = 0; refreshSlots();
        tell(useSD ? "Source: SD card" : "Source: LOCAL flash"); return;
    }
    if (!canStore()) return;
    auto state = slots[saveSlot].state;
    if (state == SlotState::Unavailable) { fileResult(useSD ? "SD not inserted" : "LOCAL storage unavailable"); return; }
    if (fileAction == 2) {
        if (state == SlotState::Invalid) { fileResult("Cannot rename invalid slot"); return; }
        renameText = slots[saveSlot].title; renameMode = true; dirty = true; return;
    }
    if (fileAction == 1 && state != SlotState::Saved && state != SlotState::Recovered) { fileResult("LOAD failed: empty / invalid"); return; }
    bool needsConfirm = fileAction == 0 ? state != SlotState::Empty : projectFingerprint(input.song) != loadedFingerprint;
    if (needsConfirm && !confirmFile) { confirmFile = true; dirty = true; return; }
    confirmFile = false;
    if (fileAction == 0) saveProject(); else loadProject();
    dirty = true;
}
void bufferReleased(void*, const void* data, uint8_t) {
    for (unsigned i = 0; i < 3; ++i) if (data == pcm[i]) occupied[i].store(false, std::memory_order_release);
}
void applySynthSample(void*,unsigned track,unsigned slot,const Controls& controls) {
    if(slot>=4||!audioSynthSamples[slot].data){synthSamplers[track].attach(nullptr,0,24000);audioSampleSlots[track]=255;return;}
    synthSamplers[track].setPolyphony(4);
    const auto& source=audioSynthSamples[slot];
    if(audioSampleSlots[track]!=slot||audioSampleGeneration[track]!=source.generation) {
        synthSamplers[track].attach(source.data,source.frames,source.rate);audioSampleSlots[track]=slot;audioSampleGeneration[track]=source.generation;
    }
    SampleControl control;control.sound=source.sound;control.master=controls.volume;control.panic=controls.panic;
    synthSamplers[track].applyMidi(control,controls.midiNotes,controls.retriggerNotes);
}
void renderSynthSample(void*,unsigned track,int16_t* out,unsigned count){synthSamplers[track].render(out,count);}
void audioTask(void*) {
    unsigned next = 0; Input state; SampleControl sampled; Workspace mode, previousMode = Workspace::Home;
    audioReady = true;
    for (;;) {
        if (audioGate.requested()) {
            // Finish queued blocks and DMA before writing local project storage.
            while (M5Cardputer.Speaker.isPlaying(0)) vTaskDelay(1);
            vTaskDelay(pdMS_TO_TICKS(15));
            while (audioGate.requested()) {audioGate.acknowledge();vTaskDelay(1);}
        }
        if (M5Cardputer.Speaker.isPlaying(0) >= 2 || occupied[next].load(std::memory_order_acquire)) { vTaskDelay(1); continue; }
        portENTER_CRITICAL(&inputLock); state = sharedInput; sampled = sharedSampleControl; mode = sharedWorkspace;
        for(unsigned i=0;i<4;++i)audioSynthSamples[i]=sharedSynthSamples[i];
        portEXIT_CRITICAL(&inputLock);
        uint32_t renderAt = micros();
        workstation.apply(state);
        if (mode != previousMode) { sampler.stop(); samplerVoices = 0; previousMode = mode; }
        if (mode == Workspace::Synth) workstation.render(pcm[next], BlockSize);
        else if (mode == Workspace::Sampler) {
            if(takePreviewRouting.load(std::memory_order_acquire)){renderTakePreview(pcm[next],BlockSize);samplerVoices=0;}
            else {sampler.apply(sampled);sampler.render(pcm[next],BlockSize);samplerPosition=sampler.position();samplerVoices=sampler.active();}
        } else if(mode==Workspace::Player)renderPlayer(pcm[next],BlockSize);
        else std::fill(pcm[next], pcm[next] + BlockSize, 0);
        uint32_t elapsed = micros() - renderAt;
        if (elapsed > renderWorstUs.load()) renderWorstUs = elapsed;
        playStep = state.playing ? workstation.position() : -1;
        countBeat = workstation.countBeat(); recStatus = int(workstation.recording()); kickTriggers = workstation.kickTriggers();
        int peak = 0; for (auto sample : pcm[next]) peak = std::max(peak, std::abs(int(sample)));
        int old = outputPeak.load(); while (peak > old && !outputPeak.compare_exchange_weak(old, peak)) {}
        occupied[next].store(true, std::memory_order_release);
        while (!M5Cardputer.Speaker.playRaw(pcm[next], BlockSize, mode==Workspace::Player?48000:SampleRate, false, 1, 0, false)) vTaskDelay(1);
        ++blocksRendered; next = (next + 1) % 3;
    }
}
void blockHeld(const uint8_t* held) { memcpy(blockedKeys, held, 128); input.liveKeys = 0; pendingStepKeys = 0; ++input.liveReset; }
void blockPiano(const uint8_t* held) {
    for(unsigned key=32;key<127;++key) if(held[key] && noteForKey(key)>=0) blockedKeys[key]=1;
    input.liveKeys=0;pendingStepKeys=0;++input.liveReset;
}
__attribute__((noinline)) void newProject() {
    rememberUndo(); stop(true); input.song = Project{}; loadedFingerprint = 0; stepPage = 0; editStep = 0; drumEditStep = 0;
    tell("Empty project - CTRL+Z undo");
}
void clearTrack() {
    rememberUndo(); stop(true);
    if (input.selected == 3) input.song.drums.steps.fill(0);
    else { auto& track=input.song.tracks[input.selected]; track.count=0; track.events.fill(NoteEvent{}); }
    tell("Track cleared - CTRL+Z undo");
}
void commitStep(uint32_t keys, bool rest) {
    unsigned trackIndex = rest ? input.selected : pendingStepTrack;
    unsigned cursor = rest ? editStep : pendingStepCursor;
    unsigned length = rest ? StepLengths[lengthIndex] : pendingStepLength;
    auto& track = input.song.tracks[trackIndex];
    unsigned count=rest?0:makeStepNotes(input.song.root,input.song.scale,track.octave,keys,input.lastKey,track.tools,length*24,stepPattern.data(),stepPattern.size());
    if(count>stepPattern.size()) {tell("Max 4 notes - nothing inserted");return;}
    if (!replaceSpan(track, input.song.span(), cursor * TicksPerStep, length * TicksPerStep, stepPattern.data(), count, stepScratch)) {
        tell("Track full - nothing inserted"); return;
    }
    rememberUndo(); track = stepScratch;
    if (autoAdvance) editStep = (cursor + length) % input.song.drums.length;
    tell(rest ? "REST inserted - CTRL+Z undo" : "NOTE inserted - CTRL+Z undo");
}
#include "sampler_ui.inc"
#include "sampler_check.inc"
#include "player.inc"
#include "player_check.inc"
void scanKeys() {
    uint8_t held[128]{};
    for (const auto& point : M5Cardputer.Keyboard.keyList()) {
        if ((point.y == 0 && point.x == 13) || (point.y == 1 && point.x == 0) || (point.y == 2 && point.x == 13)) continue;
        unsigned char key = M5Cardputer.Keyboard.getKeyValue(point).value_first;
        if (key >= 32 && key < 127) held[key] = 1;
    }
    const auto& special = M5Cardputer.Keyboard.keysState();
    if (special.ctrl && special.tab && !oldTab) {
        if(micTake!=MicTake::Idle)finishSampleRecording(false);
        if(workspace==Workspace::Sampler&&!leaveSampler()){sampleResult(false,message);memcpy(oldKeys,held,128);oldTab=special.tab;return;}
        stop(); sampleControl.keys = 0; ++sampleControl.panic;
        if(workspace==Workspace::Player)closePlayer();
        workspace = Workspace::Home; samplerPage = SamplerPage::Play;
        filesPage = toolsPage = musicPage = diagnostics = confirmClear = renameMode = confirmFile = false;
        input.diagnostic = 0; blockHeld(held); dirty = true;
        memcpy(oldKeys, held, 128); oldTab = special.tab; oldEnter = special.enter; oldDelete = special.backspace;
        return;
    }
    if (workspace != Workspace::Synth) {
        for (unsigned key=0; key<128; ++key) {
            if (!held[key]) blockedKeys[key]=0;
            if (blockedKeys[key]) held[key]=0;
        }
        if(workspace==Workspace::Player)scanPlayer(held,special.enter&&!oldEnter,special.tab&&!oldTab,special.backspace&&!oldDelete);
        else scanWorkspace(held, special.enter && !oldEnter, special.tab && !oldTab, special.backspace && !oldDelete);
        memcpy(oldKeys, held, 128); oldTab = special.tab; oldEnter = special.enter; oldDelete = special.backspace;
        return;
    }
    if (special.tab && !oldTab) {
        if (confirmClear) { confirmClear = false;
        } else if (filesPage) {
            if (renameMode) renameMode = false;
            else if (confirmFile) confirmFile = false;
            else filesPage = false;
        } else if (toolsPage) { toolsPage = false;
        } else if (musicPage) { musicPage = false;
        } else if (diagnostics) { diagnostics = false; input.diagnostic = 0; }
        else {
            if (overview && input.selected == 3) subpage = 0;
            overview = !overview;
        }
        blockHeld(held); dirty = true;
    }
    for (unsigned k = 0; k < 128; ++k) {
        if (!held[k]) blockedKeys[k] = 0;
        if (blockedKeys[k]) held[k] = 0;
    }
    auto pressed = [&](char key) { return held[unsigned(key)] && !oldKeys[unsigned(key)]; };
    if (memcmp(held, oldKeys, 128)) dirty = true;
    bool enter = special.enter && !oldEnter;
    if (filesPage) {
        scanFiles(held, enter, special.backspace && !oldDelete);
        memcpy(oldKeys, held, 128); oldTab = special.tab; oldEnter = special.enter; oldDelete = special.backspace;
        return;
    }
    if (confirmClear) {
        if (enter) { input.selected = clearTarget; clearTrack(); confirmClear = false; }
        input.liveKeys = 0;
        memcpy(oldKeys, held, 128); oldTab = special.tab; oldEnter = special.enter; oldDelete = special.backspace;
        return;
    }
    if(toolsPage) {
        bool up=pressed(';'),down=pressed('.'),left=pressed(','),right=pressed('/');
        if(up||down) toolsField=(toolsField+(up?5:1))%6;
        if(left||right) {
            int delta=right?1:-1;
            if(toolsField==0) pendingTools.chord=!pendingTools.chord;
            if(toolsField==1) pendingTools.arp=(pendingTools.arp+(right?1:3))%4;
            if(toolsField==2) pendingTools.rate=(pendingTools.rate+(right?1:2))%3;
            if(toolsField==3) pendingTools.gate=std::max(10,std::min(100,int(pendingTools.gate)+delta*5));
            if(toolsField==4) pendingTools.octaves=pendingTools.octaves==1?2:1;
            if(toolsField==5) pendingTools.hold=!pendingTools.hold;
        }
        if(enter) {rememberUndo();input.song.tracks[input.selected].tools=pendingTools;toolsPage=false;blockHeld(held);tell("CHORD/ARP settings applied");}
        if(up||down||left||right||enter) dirty=true;
        input.liveKeys=0;
        memcpy(oldKeys,held,128);oldTab=special.tab;oldEnter=special.enter;oldDelete=special.backspace;return;
    }
    if (musicPage) {
        bool up = pressed(';'), down = pressed('.'), left = pressed(','), right = pressed('/');
        if (up || down) musicField = 1 - musicField;
        if (left || right) {
            if (musicField == 0) pendingRoot = (pendingRoot + (left ? 11 : 1)) % 12;
            else pendingScale = 1 - pendingScale;
        }
        if (enter) {
            if (canTranspose(input.song, pendingRoot)) {
                rememberUndo(); stop(true); transposeProject(input.song, pendingRoot);
                input.song.scale = pendingScale; musicPage = false; blockHeld(held);
                tell("Project key applied - CTRL+Z undo");
            } else tell("Key rejected: a note is out of range");
        }
        input.liveKeys = 0; if (up || down || left || right || enter) dirty = true;
        memcpy(oldKeys, held, 128); oldTab = special.tab; oldEnter = special.enter; oldDelete = special.backspace;
        return;
    }
    if (special.backspace && !oldDelete) { stop(); blockHeld(held); memset(held, 0, 128); tell("All sound stopped"); }
    if (special.ctrl) {
        if(pressed('a') && !overview && input.selected<3 && !diagnostics) {
            stop(true);toolsPage=true;toolsField=0;pendingTools=input.song.tracks[input.selected].tools;blockHeld(held);tell("CHORD/ARP - arrows ENTER apply");
        }
        if (pressed('e') && !diagnostics && input.selected < 3 && !overview) {
            stop(true); stepMode = !stepMode; subpage = stepMode ? 1 : 0; editStep %= input.song.drums.length;
            blockHeld(held); tell(stepMode ? "STEP input - arrows / piano" : "LIVE input");
        }
        if (pressed('e') && !diagnostics && input.selected == 3 && !overview) {
            subpage = subpage == 1 ? 0 : 1;
            revealDrum(); blockHeld(held);
            tell(subpage == 0 ? "GRID: arrows / ENTER toggle" : "PAD: Q-I play / CTRL+R record");
        }
        if (pressed('k') && !diagnostics) {
            stop(true); musicPage = true; pendingRoot = input.song.root; pendingScale = input.song.scale;
            musicField = 0; blockHeld(held); tell("Project KEY / SCALE - ENTER apply");
        }
        if (pressed('r') && !diagnostics) toggleRecord();
        if (pressed('z')) undo();
        if (pressed('s')) { openFiles(0); blockHeld(held); }
        if (pressed('l')) { openFiles(1); blockHeld(held); }
        if (pressed('d') && special.fn && !diagnostics) {
            confirmClear = true; clearTarget = input.selected; blockHeld(held);
            tell("Clear selected track? ENTER / TAB");
        }
        if (stepMode && !overview && input.selected < 3) {
            if (pressed('g')) { autoAdvance = !autoAdvance; blockHeld(held); tell(autoAdvance ? "Auto advance ON" : "Auto advance OFF"); }
            if (pressed('c')) {
                clipboardCount = 0; clipboardSteps = 1;
                const auto& track = input.song.tracks[input.selected];
                for (unsigned i = 0; i < track.count; ++i) if (track.events[i].start / TicksPerStep == editStep) {
                    auto note = track.events[i]; note.start %= TicksPerStep; noteClipboard[clipboardCount++] = note;
                    clipboardSteps = std::max<unsigned>(clipboardSteps, (note.start + note.duration + 23) / 24);
                }
                blockHeld(held); tell(clipboardCount ? "Copied note/chord - CTRL+V paste" : "Nothing starts at this cursor");
            }
            if (pressed('v')) {
                if (!clipboardCount) tell("Clipboard empty");
                else {
                    auto& track = input.song.tracks[input.selected];
                    if (replaceSpan(track, input.song.span(), editStep * 24, std::min<unsigned>(clipboardSteps * 24, input.song.span()), noteClipboard.data(), clipboardCount, stepScratch)) {
                        rememberUndo(); track = stepScratch;
                        if (autoAdvance) editStep = (editStep + clipboardSteps) % input.song.drums.length;
                        tell("Pasted - CTRL+Z undo");
                    } else tell("Cannot paste - capacity/length");
                }
                blockHeld(held);
            }
        }
        if (pressed('n')) newProject();
        if (pressed('[')) { saveSlot = (saveSlot + 3) % 4; tell("Storage slot changed"); }
        if (pressed(']')) { saveSlot = (saveSlot + 1) % 4; tell("Storage slot changed"); }
        if (pressed('t')) {
            stop(true); diagnostics = !diagnostics; input.diagnostic = 0; blockHeld(held);
            tell(diagnostics ? "1 sine 2 head 3 body 4 kick" : "Diagnostic closed");
        }
    } else if (diagnostics) {
        for (unsigned i = 1; i <= 4; ++i) if (pressed('0' + i)) {
            input.diagnostic = i; ++input.diagnosticTrigger; shownPeak = 0; outputPeak = 0;
            flashUntil = millis() + 600; dirty = true;
        }
    } else {
        if (pressed(' ')) {
            if (input.playing) stop(); else { input.playing = true; input.countIn = false; ++input.epoch; }
            dirty = true;
        }
        if (pressed('[')) { input.song.drums.bpm = std::max<int>(40, input.song.drums.bpm - 5); dirty = true; }
        if (pressed(']')) { input.song.drums.bpm = std::min<int>(240, input.song.drums.bpm + 5); dirty = true; }
        if (pressed('-')) { input.song.drums.volume = std::max<int>(0, input.song.drums.volume - 10); dirty = true; }
        if (pressed('=')) { input.song.drums.volume = std::min<int>(200, input.song.drums.volume + 10); dirty = true; }
        auto arrow = [&](char key, unsigned index) {
            bool allowed = overview || special.fn || input.selected == 3 || (input.selected < 3 && (stepMode || subpage == 1));
            if (!allowed || !held[unsigned(key)]) { navRepeatAt[index] = 0; return false; }
            if (pressed(key)) { navRepeatAt[index] = millis() + 350; return true; }
            if (!navRepeatAt[index]) { navRepeatAt[index] = millis() + 350; return false; }
            bool editing = input.selected == 3 ? subpage == 0 : stepMode;
            if (editing && !overview && !special.fn && index >= 2 && int32_t(millis() - navRepeatAt[index]) >= 0) {
                navRepeatAt[index] = millis() + 80; return true;
            }
            return false;
        };
        bool up = arrow(';', 0), down = arrow('.', 1), left = arrow(',', 2), right = arrow('/', 3);
        if (overview && pressed('m')) {
            rememberUndo(); bool& mute = input.selected == 3 ? input.song.drumMuted : input.song.tracks[input.selected].muted;
            mute = !mute; tell(mute ? "Selected track MUTED" : "Selected track UNMUTED");
        }
        if (overview) {
            if (pressed('p') && !special.fn) { openFiles(0); blockHeld(held); }
            if (up || down) { input.selected = (input.selected + (up ? 3 : 1)) % 4; subpage = 0; dirty = true; }
            if (left || right) {
                stepPage = (stepPage + (left ? input.song.drums.length / 16 - 1 : 1)) % (input.song.drums.length / 16);
                dirty = true;
            }
            if (enter) { overview = false; subpage = 0; stepPage = 0; blockHeld(held); dirty = true; }

        } else {
            if (enter) {
                if (input.selected == 3) {
                    if (special.fn) subpage = subpage == 2 ? 0 : 2;
                    else if (subpage == 0) {
                        revealDrum(); rememberUndo();
                        auto& drums = input.song.drums;
                        drums.steps[selectedDrum] ^= uint64_t(1) << drumEditStep;
                        if (!input.playing && ((drums.steps[selectedDrum] >> drumEditStep) & 1)) {
                            ++drums.hits[selectedDrum]; lastHit[selectedDrum] = millis(); flashUntil = millis() + 150;
                        }
                    } else subpage = 0;
                    blockHeld(held);
                } else if (stepMode) {
                    pendingStepKeys = 0;
                    if (special.fn) commitStep(0, true);
                    else { editStep = (editStep + StepLengths[lengthIndex]) % input.song.drums.length; tell("Skipped - existing notes kept"); }
                    blockHeld(held);
                } else subpage = (subpage + 1) % 3;
                dirty = true;
            }
            if (input.selected < 3) {
                auto& track = input.song.tracks[input.selected];
                if(pressed('s') && !special.fn && !input.record) {rememberUndo();track.tools.chord=!track.tools.chord;blockHeld(held);tell(track.tools.chord?"Diatonic CHORD ON":"CHORD OFF");}
                if(pressed('l') && !special.fn && !input.record) {rememberUndo();track.tools.arp=(track.tools.arp+1)%4;blockHeld(held);tell("ARP changed - CTRL+A settings");}
                if (stepMode) {
                    if (!special.fn && (left || right)) { pendingStepKeys = 0; editStep = (editStep + (left ? input.song.drums.length - 1 : 1)) % input.song.drums.length; blockPiano(held); dirty = true; }
                    if (!special.fn && (up || down)) { pendingStepKeys = 0; lengthIndex = (lengthIndex + (up ? 4 : 1)) % 5; blockPiano(held); dirty = true; }
                    if (special.fn && pressed('d')) { pendingStepKeys = 0; rememberUndo(); deleteAtStep(track, editStep); blockHeld(held); tell("Step deleted - CTRL+Z undo"); }
                }
                if (pressed('a') && !special.fn) { rememberUndo(); nextSynthWave(input.selected); dirty = true; }
                if (special.fn && left && track.octave > 2) { --track.octave; blockHeld(held); dirty = true; }
                if (special.fn && right && track.octave < 6) { ++track.octave; blockHeld(held); dirty = true; }
                if (subpage == 1 && !stepMode && !special.fn) {
                    if (left) { stepPage = (stepPage + input.song.drums.length / 16 - 1) % (input.song.drums.length / 16); dirty = true; }
                    if (right) { stepPage = (stepPage + 1) % (input.song.drums.length / 16); dirty = true; }
                }
            } else {
                auto& drums = input.song.drums;
                revealDrum();
                if (!special.fn && (up || down)) { moveDrum(up); dirty = true; }
                if (subpage == 0 && (left || right)) {
                    unsigned distance = special.fn ? 16 : 1;
                    drumEditStep = (drumEditStep + (left ? drums.length - distance : distance)) % drums.length;
                    dirty = true;
                }
                if (subpage == 0 && special.fn && pressed('d')) {
                    rememberUndo(); drums.steps[selectedDrum] &= ~(uint64_t(1) << drumEditStep);
                    tell("Drum step deleted - CTRL+Z undo");
                }
                if (!special.fn && pressed('m')) {
                    rememberUndo(); drums.muted ^= 1 << selectedDrum;
                    tell(drums.muted & (1 << selectedDrum) ? "Selected drum MUTED" : "Selected drum UNMUTED");
                }
                for (unsigned d = 0; d < DrumCount; ++d) if (pressed(PadKeys[d]) && !special.fn) {
                    selectedDrum = d; revealDrum(); ++drums.hits[d]; lastHit[d] = millis(); flashUntil = millis() + 150;
                    if (input.song.drumMuted) tell("S4 muted - Fn M");
                    else if (drums.muted & (1 << d)) tell("Pad muted - MIX J");
                    else if (drums.solo && !(drums.solo & (1 << d))) tell("Excluded by solo - MIX K");
                    else if (!drums.levels[d] || !input.song.drumVolume || !drums.volume) tell("Volume zero - MIX / Fn H");
                    dirty = true;
                }
                if (pressed('s') && !special.fn) { rememberUndo(); preset(drums, ++presetIndex % 3); tell("Preset loaded - CTRL+Z undo"); }
                if (subpage == 2 && !special.fn) {
                    if (pressed('g')) { rememberUndo(); drums.levels[selectedDrum] = std::max<int>(0, drums.levels[selectedDrum] - 8); }
                    if (pressed('h')) { rememberUndo(); drums.levels[selectedDrum] = std::min<int>(255, drums.levels[selectedDrum] + 8); }
                    if (pressed('z')) { rememberUndo(); drums.tunes[selectedDrum] = std::max<int>(52, drums.tunes[selectedDrum] - 1); }
                    if (pressed('x')) { rememberUndo(); drums.tunes[selectedDrum] = std::min<int>(76, drums.tunes[selectedDrum] + 1); }
                    if (pressed('j')) { rememberUndo(); drums.muted ^= 1 << selectedDrum; }
                    if (pressed('k')) { rememberUndo(); drums.solo = drums.solo == (1 << selectedDrum) ? 0 : 1 << selectedDrum; }
                } else {
                    if (pressed('g') && !special.fn) drums.swing = std::max<int>(0, drums.swing - 5);
                    if (pressed('h') && !special.fn) drums.swing = std::min<int>(50, drums.swing + 5);
                }
            }
            if (special.fn && (pressed('g') || pressed('h'))) {
                rememberUndo();
                uint8_t& level = input.selected == 3 ? input.song.drumVolume : input.song.tracks[input.selected].volume;
                level = std::max<int>(0, std::min<int>(200, int(level) + (pressed('h') ? 5 : -5)));
                tell("Track level changed - Fn G/H");
            }
            if (special.fn && pressed('m')) {
                rememberUndo();
                bool& muted = input.selected == 3 ? input.song.drumMuted : input.song.tracks[input.selected].muted;
                muted = !muted; tell(muted ? "Track muted - Fn M" : "Track unmuted - Fn M");
            }
            if (special.fn && pressed('j') && !input.record) {
                rememberUndo(); stop(true); auto& drums = input.song.drums;
                drums.length = drums.length == 16 ? 32 : drums.length == 32 ? 64 : 16;
                for (auto& track : input.song.tracks) for (unsigned i = 0; i < track.count; ++i) {
                    track.events[i].start %= input.song.span(); track.events[i].duration = std::min<unsigned>(track.events[i].duration, input.song.span());
                }
                stepPage = 0; editStep %= drums.length; drumEditStep %= drums.length; tell("Loop length changed");
            }
        }
    }
    uint32_t notes = 0;
    if (!overview && !diagnostics && !filesPage && !musicPage && !toolsPage && !confirmClear && input.selected < 3 && !special.ctrl && !special.fn) {
        for (unsigned key = 32; key < 127; ++key) if (held[key]) { int note = noteForKey(key); if (note >= 0) notes |= 1u << note; }
    }
    uint32_t pressedNotes=notes & ~input.liveKeys;
    for(unsigned key=0;key<24;++key) if(pressedNotes&(1u<<key)) input.lastKey=key;
    if (stepMode && !overview && !diagnostics && !filesPage && !musicPage && !toolsPage && !confirmClear && input.selected < 3) {
        if (special.ctrl || special.fn) pendingStepKeys = 0;
        if (notes && !special.ctrl && !special.fn) {
            if (!pendingStepKeys) { pendingStepTrack = input.selected; pendingStepCursor = editStep; pendingStepLength = StepLengths[lengthIndex]; }
            pendingStepKeys |= notes;
        } else if (pendingStepKeys && !special.ctrl && !special.fn) {
            uint32_t captured = pendingStepKeys; pendingStepKeys = 0; commitStep(captured, false);
        }
    }
    uint32_t newNotes=notes & ~input.liveKeys;
    for(unsigned key=0;key<24;++key) if(newNotes&(1u<<key)) input.lastKey=key;
    if (notes != input.liveKeys) dirty = true;
    input.stepInput=stepMode && !overview && input.selected<3;
    input.liveKeys = notes;
    memcpy(oldKeys, held, 128); oldTab = special.tab; oldEnter = special.enter; oldDelete = special.backspace;
}
void readBattery() {
    if (batteryAt && millis() - batteryAt < 2000) return;
    batteryAt = millis(); int now = M5Cardputer.Power.getBatteryLevel(); batteryMV = M5Cardputer.Power.getBatteryVoltage();
    int updated = now < 0 ? -1 : battery < 0 ? std::min(100, now) : (battery * 3 + std::min(100, now) + 2) / 4;
    if (updated != battery) { battery = updated; dirty = true; }
}
void text(int x, int y, const char* value, uint16_t color = 0xffff) { screen.setTextColor(color); screen.setCursor(x, y); screen.print(value); }
void keyboard(unsigned offset, int y) {
    const int whites[] = {0,2,4,5,7,9,11}, blacks[] = {1,3,6,8,10}, after[] = {0,1,3,4,5};
    const char* keys = offset ? HighKeys : LowKeys;
    for (unsigned i = 0; i < 7; ++i) {
        screen.fillRect(6 + i * 32, y, 30, 25, input.liveKeys & (1u << (whites[i] + offset)) ? 0x07ff : 0xffff);
        screen.setTextColor(0); screen.setCursor(17 + i * 32, y + 16); screen.print(char(keys[whites[i]] - 32));
    }
    for (unsigned i = 0; i < 5; ++i) {
        int x = 28 + after[i] * 32;
        screen.fillRect(x, y, 16, 14, input.liveKeys & (1u << (blacks[i] + offset)) ? 0xfd20 : 0x1082);
        screen.setTextColor(0xffff); screen.setCursor(x + 5, y + 3);
        char key = keys[blacks[i]]; screen.print(key >= 'a' ? char(key - 32) : key);
    }
}
// A bar always contains sixteen 1/16-note cells. Stored note tails can wrap.
unsigned visibleBar(int step) {
    if (stepMode && !overview && input.selected < 3) return editStep / 16;
    return input.playing && step >= 0 ? unsigned(step) / 16
        : (overview || (input.selected < 3 && subpage == 1) ? stepPage : 0) % (input.song.drums.length / 16);
}
constexpr uint16_t trackColors[] = {0x07ff, 0xb81f, 0xffe0, 0x07e0};
void loopStrip(int y, int height, int step, bool numbered = false, unsigned trackIndex = 4, int origin = 4, int pitch = 14) {
    if (trackIndex >= 4) trackIndex = input.selected;
    uint16_t color = trackColors[trackIndex];
    uint16_t tailColor = (color & 0xf7de) >> 1;
    unsigned first = visibleBar(step) * 16;
    bool recording = recStatus.load() == int(RecState::Recording) && input.recordTarget == trackIndex;
    for (unsigned col = 0; col < 16; ++col) {
        unsigned pos = first + col;
        bool onset = false, tail = false;
        if (trackIndex < 3) {
            const auto& track = input.song.tracks[trackIndex];
            for (unsigned i = 0; i < track.count; ++i) {
                const auto& note = track.events[i];
                onset |= note.start / TicksPerStep == pos;
                unsigned offset = (pos * TicksPerStep + input.song.span() - note.start) % input.song.span();
                tail |= offset < note.duration;
            }
        } else {
            for (unsigned d = 0; d < DrumCount; ++d) onset |= (input.song.drums.steps[d] >> pos) & 1;
        }
        bool cursor = input.playing && step == int(pos);
        bool editing = stepMode && !overview && input.selected == trackIndex && trackIndex < 3 && editStep == pos;
        int x = origin + col * pitch;
        screen.fillRect(x, y, pitch - 1, height, onset ? color : tail ? tailColor : 0x2104);
        if (cursor && recording && trackIndex < 3 && trackIndex == input.selected && input.liveKeys)
            screen.fillRect(x + 2, y + 2, pitch - 5, height - 4, 0xf800);
        screen.drawRect(x, y, pitch - 1, height, editing ? 0xfd20 : cursor ? (recording ? 0xf800 : 0xffff) : col % 4 == 0 ? 0x7bef : 0x632c);
        if (numbered) { screen.setTextColor(0xffff); screen.setCursor(x + (col < 9 ? 4 : 1), y + 8); screen.print(col + 1); }
    }
}
void drawDrumGrid(int step) {
    revealDrum();
    const auto& drums = input.song.drums;
    unsigned bar = drumEditStep / 16, first = bar * 16;
    screen.setTextColor(0xffff); screen.setCursor(4, 28);
    screen.printf("GRID BAR%u/%u SW%u", bar + 1, drums.length / 16, 50 + drums.swing / 2);
    screen.setCursor(190, 28); screen.printf("%02u/%u", drumEditStep + 1, drums.length);
    for (unsigned beat = 0; beat < 4; ++beat) {
        screen.setTextColor(0x7bef); screen.setCursor(60 + beat * 44, 37); screen.print(beat + 1);
    }
    for (unsigned row = 0; row < 4; ++row) {
        unsigned drum = drumOrder[drumScroll + row];
        int y = 46 + row * 17;
        bool selected = selectedDrum == drum;
        bool muted = input.song.drumMuted || (drums.muted & (1 << drum)) || (drums.solo && !(drums.solo & (1 << drum)));
        uint16_t color = muted ? (drumColors[drum] & 0xf7de) >> 1 : drumColors[drum];
        if (selected) {
            screen.fillRect(2, y - 1, 235, 17, 0x2104);
            screen.drawRect(2, y - 1, 235, 17, drumColors[drum]);
        }
        text(5, y + 4, DrumNames[drum], color);
        if (drums.muted & (1 << drum)) text(40, y + 4, "M", 0xf800);
        if (drums.solo & (1 << drum)) text(47, y + 4, "S", 0xffe0);
        for (unsigned col = 0; col < 16; ++col) {
            unsigned pos = first + col;
            int x = 58 + col * 11;
            screen.fillRect(x, y, 10, 14, (drums.steps[drum] >> pos) & 1 ? color : 0x1082);
            screen.drawRect(x, y, 10, 14, col % 4 == 0 ? 0x7bef : 0x4208);
            if (selected && pos == drumEditStep) {
                screen.drawRect(x, y, 10, 14, 0xffe0);
                screen.drawRect(x + 1, y + 1, 8, 12, 0xffe0);
            }
        }
    }
    // The edit bar stays put while the loop plays, so editing never jumps away.
    if (input.playing && step >= int(first) && step < int(first + 16)) {
        int x = 57 + (step - first) * 11;
        uint16_t color = input.recordTarget == 3 && recStatus.load() == int(RecState::Recording) ? 0xf800 : 0xffff;
        screen.drawFastVLine(x, 45, 67, color);
    }
    screen.drawFastVLine(239, 45, 67, 0x4208);
    screen.fillRect(238, 45 + drumScroll * 8, 2, 32, 0x7bef);
}
void drawUI() {
    if (!uiReady) return;
    if (workspace != Workspace::Synth) { drawWorkspace(); dirty = false; return; }
    auto& drums = input.song.drums;
    int step = playStep.load(), state = recStatus.load();
    static int lastStep = -1;
    if (input.playing && step >= 0 && lastStep != step) {
        for (unsigned d = 0; d < DrumCount; ++d) if ((drums.steps[d] >> step) & 1) lastHit[d] = millis();
    }
    lastStep = step;
    int peak = outputPeak.exchange(0);
    if (peak > shownPeak || millis() >= peakUntil) { shownPeak = peak; peakUntil = millis() + 500; }
    screen.fillScreen(0x0842); screen.setTextSize(1);
    text(4, 3, toolsPage ? "CHORD / ARP" : musicPage ? "PROJECT KEY" : filesPage ? (useSD ? "FILES SD" : "FILES LOCAL") : diagnostics ? "AUDIO TEST" : overview ? "STUDIO 4V" : names[input.selected]);
    text(106, 3, checksOK && audioReady ? "CTRL+TAB" : "FAIL", checksOK && audioReady ? 0x7bef : 0xf800);
    screen.drawRect(176, 3, 16, 8, 0xffff); screen.fillRect(192, 5, 2, 4, 0xffff);
    if (battery >= 0) screen.fillRect(178, 5, battery * 12 / 100, 4, battery < 15 ? 0xf800 : 0x07e0);
    screen.setTextColor(0xffff); screen.setCursor(199, 3); if (battery >= 0) screen.printf("%d%%", battery); else screen.print("--%");
    const char* transport = !input.playing ? "STOP" : state == int(RecState::CountIn) ? "COUNT" : state == int(RecState::Armed) ? "ARMED" : state == int(RecState::Recording) ? "REC" : "PLAY";
    screen.setCursor(4, 16); screen.setTextColor(state == int(RecState::Recording) ? 0xf800 : 0xffff);
    screen.printf("%s %uBPM %02d/%u M%u%%", transport, drums.bpm, step < 0 ? 0 : step + 1, drums.length, (drums.volume + 1) / 2);
    if (input.record) { screen.setCursor(192, 16); screen.printf("T%u", input.recordTarget + 1); }
    screen.setTextColor(0xffff);
    if(toolsPage) {
        const char* labels[]={"CHORD","ARP","RATE","GATE","OCT RANGE","HOLD"};
        for(unsigned row=0;row<6;++row) {
            int y=31+row*13;if(row==toolsField) screen.fillRect(2,y-2,235,12,0x2104);
            text(5,y,labels[row],row==toolsField?0x07ff:0xffff);screen.setCursor(100,y);screen.setTextColor(0xffff);
            if(row==0) screen.print(pendingTools.chord?"DIATONIC TRIAD":"OFF");
            if(row==1) screen.print(arpNames[pendingTools.arp]);
            if(row==2) screen.printf("1/%u",4u<<pendingTools.rate);
            if(row==3) screen.printf("%u%%",pendingTools.gate);
            if(row==4) screen.printf("%u",pendingTools.octaves);
            if(row==5) screen.print(pendingTools.hold?"ON":"OFF");
        }

    } else if (musicPage) {
        text(4, 34, "Shared by S1 / S2 / S3", 0x7bef);
        for (unsigned row = 0; row < 2; ++row) {
            int y = 51 + row * 18;
            if (row == musicField) screen.fillRect(2, y - 2, 235, 15, 0x2104);
            text(6, y, row == 0 ? "KEY" : "SCALE", row == musicField ? 0x07ff : 0xffff);
            text(85, y, row == 0 ? PitchNames[pendingRoot] : pendingScale == 0 ? "MAJOR" : "NATURAL MINOR");
        }
        text(4, 88, "Notes transpose / S4 unchanged");

    } else if (filesPage) {
        screen.fillRect(0, 14, 240, 13, 0x0842);
        text(4, 16, useSD ? (sdOK ? "SD READY" : "NO SD CARD") : (localOK && localWriteChecked ? "LOCAL READY" : "LOCAL FAILED"), (useSD ? sdOK : localOK && localWriteChecked) ? 0x07e0 : 0xf800);
        text(108, 16, projectFingerprint(input.song) == loadedFingerprint ? "CURRENT SAVED" : "CURRENT UNSAVED", 0xfd20);
        text(4, 30, "SLOT  NAME           STATE", 0x7bef);
        for (unsigned slot = 0; slot < 4; ++slot) {
            int y = 43 + slot * 13;
            if (slot == saveSlot) screen.fillRect(2, y - 2, 235, 12, 0x2104);
            screen.setTextColor(slot == saveSlot ? 0x07ff : 0xffff); screen.setCursor(5, y); screen.printf("%u", slot + 1);
            text(25, y, slots[slot].title.data(), slot == saveSlot ? 0x07ff : 0xffff);
            const char* state = slots[slot].state == SlotState::Saved ? "SAVED" : slots[slot].state == SlotState::Recovered ? "BACKUP" : slots[slot].state == SlotState::Empty ? "EMPTY" : slots[slot].state == SlotState::Invalid ? "INVALID" : "OFFLINE";
            text(160, y, state, slots[slot].state == SlotState::Invalid ? 0xf800 : slots[slot].state == SlotState::Empty ? 0x7bef : 0xffff);
        }
        screen.setCursor(4, 98); screen.setTextColor(0x7bef);
        auto& info = slots[saveSlot];
        if (info.state == SlotState::Saved || info.state == SlotState::Recovered)
            screen.printf("%uBPM %ustep %unotes/hits #%lu", info.bpm, info.length, info.notes, (unsigned long)info.revision);
        else screen.print(info.state == SlotState::Empty ? "Empty slot - SAVE to create" : "Slot unavailable / invalid");
        const char* actions[] = {"SAVE", "LOAD", "NAME", "LOCAL/SD"};
        for (unsigned action = 0; action < 4; ++action) {
            int x = 3 + action * 59;
            if (action == fileAction) screen.fillRect(x, 109, 56, 12, 0x02b6);
            text(x + 3, 111, actions[action]);
        }
        if (renameMode || confirmFile) {
            screen.fillRect(7, 39, 226, 66, 0x1082); screen.drawRect(7, 39, 226, 66, 0xfd20);
            if (renameMode) {
                text(14, 46, "NAME - max 15 characters", 0xfd20);
                screen.setCursor(14, 63); screen.setTextColor(0xffff); screen.printf("%s_", renameText.data());
                text(14, 84, "ENTER apply / TAB cancel");
            } else {
                text(14, 46, fileAction == 0 ? "Overwrite this saved slot?" : "Replace current project?", 0xfd20);
                screen.setCursor(14, 63); screen.setTextColor(0xffff); screen.printf("%u  %s", saveSlot + 1, info.title.data());
                text(14, 84, "ENTER confirm / TAB cancel");
            }
        }
    } else if (diagnostics) {
        text(4, 32, "1 C4 sine   2 KICK head"); text(4, 46, "3 KICK body 4 FULL KICK");
        screen.setCursor(4, 64); screen.printf("TEST %u  PCM PEAK %d", input.diagnostic, shownPeak);
        screen.setCursor(4, 78); screen.printf("KICK engine %lu KEY %lu", (unsigned long)kickTriggers.load(), (unsigned long)drums.hits[0]);
        screen.setCursor(4, 92); screen.printf("CPU %lu/5333us B%lu", (unsigned long)renderWorstUs.load(), (unsigned long)blocksRendered.load());
        text(4, 109, "Tests bypass mute/solo/track level");
    } else if (overview) {
        screen.setCursor(4, 28); screen.setTextColor(0x7bef);
        screen.printf("BAR%u/%u %s %s", visibleBar(step)+1,drums.length/16,PitchNames[input.song.root],input.song.scale?"MIN":"MAJ");
        screen.setCursor(125,28);screen.printf("MASTER%u%%",(drums.volume+1)/2);
        screen.drawRect(190,26,20,11,0x7bef);text(197,28,"-");screen.drawRect(214,26,20,11,0x7bef);text(221,28,"+");
        const char* labels[] = {"S1", "S2", "S3", "S4"};
        for (unsigned track = 0; track < 4; ++track) {
            int y = 38 + track * 20;
            if (track == input.selected) {
                screen.fillRect(2, y, 235, 19, 0x2104);
                screen.drawRect(2, y, 235, 19, trackColors[track]);
                text(6, y + 5, ">", trackColors[track]);
            }
            bool mute = track == 3 ? input.song.drumMuted : input.song.tracks[track].muted;
            text(17, y + 5, labels[track], mute ? 0x7bef : trackColors[track]);
            if (mute) text(40, y + 5, "M", 0xf800);
            loopStrip(y + 2, 15, step, false, track, 56, 11);
        }
    } else if (input.selected < 3) {
        auto& track = input.song.tracks[input.selected];
        text(28,3,track.tools.chord?"CHD":"---",track.tools.chord?0xfd20:0x7bef);
        text(58,3,arpNames[track.tools.arp],track.tools.arp?0x07ff:0x7bef);
        screen.setCursor(4, 28); screen.printf("%s %s %s%u/%u", stepMode ? "STEP" : synthPages[subpage], waves[track.wave], PitchNames[input.song.root], track.octave, track.octave + 1);
        screen.setCursor(183, 28); screen.printf("BAR%u/%u", visibleBar(step) + 1, drums.length / 16);
        if (!stepMode && subpage != 1) loopStrip(37, 8, step);
        if (stepMode) {
            loopStrip(45, 25, step, true);
            screen.setCursor(4, 77); screen.printf("NOTE 1/%u STEP %u/%u AUTO%s", 1u << lengthIndex, editStep + 1, drums.length, autoAdvance ? "+" : "-");
            screen.setCursor(4, 90); screen.print("AT: "); unsigned shown = 0;
            for (unsigned i = 0; i < track.count && shown < 4; ++i) if (track.events[i].start / TicksPerStep == editStep) {
                unsigned midi = track.events[i].midi; screen.printf("%s%u ", PitchNames[midi % 12], midi / 12 - 1); ++shown;
            }
            if (!shown) screen.print("--");


        } else if (subpage == 0) { keyboard(0, 48); keyboard(12, 79); }
        else if (subpage == 1) {
            loopStrip(48, 26, step, true);
            screen.setCursor(4, 83); screen.printf("%u notes / %u max", track.count, MaxEvents);


        } else {
            screen.setCursor(4, 50); screen.printf("WAVE %s   OCTAVE %u", waves[track.wave], track.octave);
            screen.setCursor(4, 68); screen.printf("LEVEL %u MUTE %s", track.volume, track.muted ? "ON" : "OFF");
            if(track.wave>=4){text(4,86,synthSamples[track.wave-4].data?synthSampleInfo[track.wave-4].title:"SAMPLE UNAVAILABLE",synthSamples[track.wave-4].data?0x07ff:0xfd20);if(synthSamples[track.wave-4].data)text(4,100,sampleModes[unsigned(synthSampleInfo[track.wave-4].sound.mode)],0xfd20);}


        }
    } else if (subpage == 0) {
        drawDrumGrid(step);
    } else {
        screen.setCursor(4, 28); screen.printf("%s %s SW%u", drumPages[subpage], DrumNames[selectedDrum], 50 + drums.swing / 2);
        screen.setCursor(183, 28); screen.printf("BAR%u/%u", visibleBar(step) + 1, drums.length / 16);
        loopStrip(37, 8, step);
        if (subpage == 1) {
            for (unsigned d = 0; d < DrumCount; ++d) {
                int x = 4 + (d % 4) * 58, y = 48 + (d / 4) * 28;
                bool flashing = lastHit[d] && millis() - lastHit[d] < 150;
                screen.fillRoundRect(x, y, 54, 25, 3, flashing ? 0x02b6 : 0x2104);
                screen.drawRoundRect(x, y, 54, 25, 3, selectedDrum == d ? 0xfd20 : 0x632c);
                screen.setCursor(x + 4, y + 3); screen.print(char(PadKeys[d] - 32));
                screen.setCursor(x + 4, y + 14); screen.print(DrumNames[d]);
            }

        } else {
            screen.setCursor(4, 50); screen.printf("LEVEL %u TUNE %+d", drums.levels[selectedDrum], int(drums.tunes[selectedDrum]) - 64);
            screen.setCursor(4, 68); screen.printf("MUTE %s SOLO %s", drums.muted & (1 << selectedDrum) ? "ON" : "OFF", drums.solo & (1 << selectedDrum) ? "ON" : "OFF");
            screen.setCursor(4, 86); screen.printf("TRACK VOL%u MUTE %s", input.song.drumVolume, input.song.drumMuted ? "ON" : "OFF");


        }
    }
    if (state == int(RecState::CountIn) && !diagnostics && !filesPage && !musicPage && !toolsPage) {
        screen.fillRect(162, 14, 76, 12, 0x0842); screen.setCursor(165, 16); screen.setTextColor(0xfd20); screen.printf("COUNT %d/4", countBeat.load());
    }
    bool outside=!overview && !filesPage && !musicPage && !toolsPage && input.selected<3 && input.song.tracks[input.selected].tools.chord && input.liveKeys && degreeOf(input.lastKey,input.song.scale)<0;
    if (outside) text(4,123,"OUT OF SCALE: single note",0xfd20);
    else if (filesPage && storageResult[0]) text(4,123,storageResult);
    else if (millis()<messageUntil) text(4,123,message);
    if (confirmClear) {
        screen.fillRect(9, 43, 222, 57, 0x1082); screen.drawRect(9, 43, 222, 57, 0xf800);
        screen.setCursor(16, 51); screen.setTextColor(0xfd20); screen.printf("CLEAR ALL NOTES ON %s?", names[clearTarget]);
        text(16, 68, "ENTER confirm / TAB cancel"); text(16, 85, "CTRL+Z can undo after clearing");
    }
    screen.pushSprite(0, 0); dirty = false;
}
}
// Storage paths also call SDK flash routines; keep a margin beyond the
// measured application frames, without placing project buffers on this stack.
size_t getArduinoLoopTaskStackSize() { return 16384; }
namespace {
bool checkLocalStorage() {
    if (!localOK) return false;
    if (localStore.getBool("verified41", false)) return true;
    encodeSavedProjectTo(input.song, "STORAGE CHECK", 41, storageWriteBuffer.data());
    size_t written = localStore.putBytes("_probe", storageWriteBuffer.data(), storageWriteBuffer.size());
    size_t got = localStore.getBytes("_probe", storageReadBuffer.data(), storageReadBuffer.size());
    bool ok = written == SavedProjectBytes && got == SavedProjectBytes && storageReadBuffer == storageWriteBuffer
        && decodeSavedProject(storageReadBuffer.data(), got, storageScratch) && storageScratch.revision == 41;
    if (localStore.isKey("_probe") && !localStore.remove("_probe")) ok = false;
    if (ok) ok = localStore.putBool("verified41", true) == 1;
    return ok;
}
}
void setup() {
    auto config = M5.config(); config.internal_spk = true; config.internal_mic = true; // Configure pins/callback only; mic starts on explicit RECORD.
    M5Cardputer.begin(config, true); Serial.begin(115200);
    if(M5.getBoard()==m5::board_t::board_M5CardputerADV){
        // Detach the SDK ISR before its reader is destroyed (it retains a raw pointer).
        detachInterrupt(digitalPinToInterrupt(11));
        std::unique_ptr<StudioAdvKeyboard> reader(new StudioAdvKeyboard());studioKeyboard=reader.get();
        M5Cardputer.Keyboard.begin(std::move(reader));
    }
    // Keep the panel in native scan order; rotate drawing in RAM.
    M5Cardputer.Display.setRotation(0); M5Cardputer.Display.setBrightness(100);
    screen.setColorDepth(16); uiReady = screen.createSprite(135, 240) != nullptr; screen.setRotation(1);
    if (!uiReady) { M5Cardputer.Display.println("Canvas allocation failed"); return; }
    screen.fillScreen(0x0842); text(4, 4, "Pocket Studio v0.25 checking..."); screen.pushSprite(0, 0);
    SPI.begin(40, 39, 14, 12); sdOK = SD.begin(12, SPI, 4000000) && SD.cardType() != CARD_NONE;
    localOK = localStore.begin("pocket-synth");
    samplerLegacyOK = samplerLegacy.begin("pocket-sampler",true); samplerStoreOK=sdOK;
    localWriteChecked = checkLocalStorage();
    input.epoch = 1; loadProject(true); publish();
    bootEngine.init(); checksOK = checkSynth(bootEngine);
    captures = xQueueCreate(64, sizeof(Capture)); workstation.init(); workstation.onCapture(nullptr, captureCallback);
    workstation.onSampleTrack(nullptr,applySynthSample,renderSynthSample);
    M5Cardputer.Speaker.end(); auto speaker = M5Cardputer.Speaker.config();
    speaker.sample_rate = 48000; speaker.stereo = true; speaker.dma_buf_len = 128; speaker.dma_buf_count = 4;
    speaker.task_priority = 4; speaker.task_pinned_core = 0;
    M5Cardputer.Speaker.config(speaker); M5Cardputer.Speaker.setVolume(128);
    if (checksOK && captures && M5Cardputer.Speaker.begin()) {
        M5Cardputer.Speaker.setBufferReleaseCallback(nullptr, bufferReleased);
        if (xTaskCreatePinnedToCore(audioTask, "workstation", 8192, nullptr, 3, nullptr, 0) != pdPASS) tell("Audio task allocation failed");
    } else tell("Audio initialization failed");
    readBattery(); drawUI();
}
void loop() {
    if (!uiReady) { delay(100); return; }
    M5Cardputer.update(); consumeCaptures(); scanKeys(); tickSampleRecording();tickTakePreview();tickTakeWave();
    static uint32_t lastSamplerSDCheck=0;
    if(workspace==Workspace::Sampler&&micTake==MicTake::Idle&&millis()-lastSamplerSDCheck>=500){lastSamplerSDCheck=millis();if(!ensureSamplerSD()){sampleSilence();workspace=Workspace::Home;dirty=true;}}
    tickPlayer();refreshSynthSamples(); publish(); readBattery();
    while (Serial.available()) {char command=Serial.read();if(command=='V')runSamplerSDCheck();if(command=='P')runPlayerCheck();if (command == '?') {
        Serial.printf("STUDIO 0.25 module=%u audio=%u heap=%u sample_frames=%lu sample_bytes=%lu voices=%u max_render_us=%lu sampler_store=%u mic_enabled=%u mic_running=%u take=%u captured=%u\n",
            unsigned(workspace), unsigned(audioReady.load()), ESP.getFreeHeap(),
            (unsigned long)samplePatch.frames, (unsigned long)(samplePatch.frames * sizeof(int16_t)),
            samplerVoices.load(), (unsigned long)renderWorstUs.load(), unsigned(samplerStoreOK),unsigned(M5Cardputer.Mic.isEnabled()),unsigned(M5Cardputer.Mic.isRunning()),unsigned(micTake),micCompleted.load());
        if(samplePatch.frames)Serial.printf("SAMPLE_SETTINGS start=%u end=%u root=%u mode=%u attack=%u decay=%u sustain=%u release=%u gain=%u auto=%u cutoff=%u resonance=%u drive=%u bits=%u page=%u field=%u editing=%u unsaved=%u slot=%d\n",samplePatch.sound.start,samplePatch.sound.end,samplePatch.sound.root,unsigned(samplePatch.sound.mode),samplePatch.sound.attack,samplePatch.sound.decay,samplePatch.sound.sustain,samplePatch.sound.release,samplePatch.sound.gain,unsigned(samplePatch.sound.autoLevel),samplePatch.sound.cutoff,samplePatch.sound.resonance,samplePatch.sound.drive,samplePatch.sound.bits,unsigned(samplerPage),samplerField,unsigned(samplerEditing),unsigned(samplerUnsaved),samplerBoundSlot+1);
        if(samplePatch.frames)Serial.printf("SAMPLE_MOD pitch=%d pitch_ms=%u filter=%d filter_ms=%u tune=%d fine=%d\n",int(samplePatch.sound.pitchEnv),samplePatch.sound.pitchDecay,int(samplePatch.sound.filterEnv),samplePatch.sound.filterDecay,int(samplePatch.sound.transpose),int(samplePatch.sound.fine));
        if(samplePatch.frames)Serial.printf("SAMPLE_OSC a=%u b=%u mix=%u tune=%d lfo_target=%u shape=%u rate=%u depth=%u\n",samplePatch.sound.oscA,samplePatch.sound.oscB,samplePatch.sound.oscMix,int(samplePatch.sound.oscTune),samplePatch.sound.lfoTarget,samplePatch.sound.lfoShape,samplePatch.sound.lfoRate,samplePatch.sound.lfoDepth);
        if(samplePatch.frames)Serial.printf("SAMPLE_SOURCE base=%u\n",unsigned(samplePatch.baseSource));
        if(studioKeyboard)Serial.printf("KEYBOARD ready=%u polls=%u events=%u pending=%u held=%u overflows=%u sw_overflows=%u invalid=%u errors=%u status=%u fifo=%u\n",unsigned(studioKeyboard->ready),studioKeyboard->pump.polls,studioKeyboard->pump.events,studioKeyboard->pump.queue.count,unsigned(M5Cardputer.Keyboard.keyList().size()),studioKeyboard->pump.overflows,studioKeyboard->pump.queue.softwareOverflows,studioKeyboard->pump.queue.invalid,studioKeyboard->pump.errors,studioKeyboard->pump.lastStatus,studioKeyboard->pump.lastCount);
    }}
    static uint32_t lastDraw = 0; static int lastStep = -2, lastRec = -1, lastBeat = -1;
    int step = playStep.load(), status = recStatus.load(), beat = countBeat.load();
    if (step != lastStep || status != lastRec || beat != lastBeat) {
        dirty = true; lastStep = step; lastRec = status; lastBeat = beat;
    }
    if (messageUntil && millis() >= messageUntil) { messageUntil = 0; dirty = true; }
    if (flashUntil && millis() >= flashUntil) { flashUntil = 0; dirty = true; }
    static unsigned lastSamplerActive = 0;
    unsigned sampleActive = samplerVoices.load();
    if (sampleActive != lastSamplerActive) { lastSamplerActive = sampleActive; dirty = true; }
    bool sampleAnimating = workspace == Workspace::Sampler && sampleActive;
    if ((dirty || diagnostics || flashUntil || sampleAnimating) && millis() - lastDraw >= 50) { lastDraw = millis(); drawUI(); }
    delay(2);
}




