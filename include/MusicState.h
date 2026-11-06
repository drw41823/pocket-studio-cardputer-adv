#pragma once
#include <array>
#include <cstdint>

namespace pocket {
constexpr unsigned SampleRate = 24000, BlockSize = 128, DrumCount = 8;
constexpr float Pi = 3.14159265358979323846f;
constexpr const char* LowKeys = "e4r5ty7u8i9o";
constexpr const char* HighKeys = "zdxfcvgbhnjm";
constexpr const char* PadKeys = "qwertyui";
constexpr const char* DrumNames[DrumCount] = {"KICK", "SNARE", "CLAP", "C HAT", "O HAT", "L TOM", "H TOM", "PERC"};

struct Controls {
    uint32_t notes = 0;
    std::array<uint32_t, 4> midiNotes{};
    std::array<uint32_t, 4> retriggerNotes{};
    uint8_t drumTest = 0;
    uint16_t bpm = 120;
    uint8_t octave = 4, wave = 0, volume = 100, swing = 0, length = 16;
    bool running = false, recording = false;
    uint32_t panic = 0;
    std::array<uint32_t, DrumCount> hits{};
    std::array<uint64_t, DrumCount> steps{};
    std::array<uint8_t, DrumCount> levels{{220, 190, 180, 170, 170, 200, 190, 180}};
    std::array<uint8_t, DrumCount> tunes{{64,64,64,64,64,64,64,64}};
    uint8_t muted = 0, solo = 0;
};
inline int noteForKey(char key) {
    for (int i = 0; i < 12; ++i) {
        if (LowKeys[i] == key) return i;
        if (HighKeys[i] == key) return i + 12;
    }
    return -1;
}
inline void preset(Controls& c, unsigned index) {
    const uint16_t p[3][DrumCount] = {
        {0x0101,0x1010,0,0x5555,0,0,0,0},
        {0x1111,0x1010,0x1010,0x5555,0x4000,0,0,0},
        {0x0521,0x1010,0,0xdddd,0x4000,0x0800,0x8000,0x0200}
    };
    for (unsigned d = 0; d < DrumCount; ++d) {
        uint64_t bits = p[index % 3][d];
        c.steps[d] = bits | (bits << 16) | (bits << 32) | (bits << 48);
    }
}
inline uint32_t crc32(const uint8_t* data, unsigned count) {
    uint32_t crc = 0xffffffff;
    for (unsigned i = 0; i < count; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
constexpr unsigned SongBytes = 102;
inline std::array<uint8_t, SongBytes> encodeSong(const Controls& c) {
    std::array<uint8_t, SongBytes> out{};
    const char* magic = "PSYN02";
    for (unsigned i = 0; i < 6; ++i) out[i] = magic[i];
    out[8] = 2; out[9] = c.length; out[10] = c.bpm; out[11] = c.bpm >> 8;
    out[12] = c.swing; out[13] = c.volume; out[14] = c.wave; out[15] = c.octave;
    for (unsigned d = 0; d < DrumCount; ++d) {
        for (unsigned b = 0; b < 8; ++b) out[16 + d * 8 + b] = c.steps[d] >> (b * 8);
        out[80 + d] = c.levels[d]; out[88 + d] = c.tunes[d];
    }
    out[96] = c.muted; out[97] = c.solo;
    uint32_t crc = crc32(out.data(), 98);
    for (unsigned b = 0; b < 4; ++b) out[98 + b] = crc >> (b * 8);
    return out;
}
inline bool decodeSong(const uint8_t* bytes, unsigned count, Controls& result) {
    if (count != SongBytes) return false;
    for (unsigned i = 0; i < 6; ++i) if (bytes[i] != "PSYN02"[i]) return false;
    uint32_t stored = 0;
    for (unsigned b = 0; b < 4; ++b) stored |= uint32_t(bytes[98 + b]) << (b * 8);
    if (stored != crc32(bytes, 98) || bytes[8] != 2) return false;
    Controls c = result; // Keep live counters and transport outside the file.
    c.length = bytes[9]; c.bpm = bytes[10] | (uint16_t(bytes[11]) << 8);
    c.swing = bytes[12]; c.volume = bytes[13]; c.wave = bytes[14]; c.octave = bytes[15];
    if ((c.length != 16 && c.length != 32 && c.length != 64) || c.bpm < 40 || c.bpm > 240
        || c.swing > 50 || c.volume > 200 || c.wave > 3 || c.octave < 2 || c.octave > 6) return false;
    for (unsigned d = 0; d < DrumCount; ++d) {
        c.steps[d] = 0;
        for (unsigned b = 0; b < 8; ++b) c.steps[d] |= uint64_t(bytes[16 + d * 8 + b]) << (b * 8);
        c.levels[d] = bytes[80 + d]; c.tunes[d] = bytes[88 + d];
        if (c.tunes[d] < 32 || c.tunes[d] > 96) return false;
    }
    c.muted = bytes[96]; c.solo = bytes[97];
    result = c; return true;
}
}
