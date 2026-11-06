#pragma once
#include "Workstation.h"
#include <cstring>

namespace pocket {
constexpr unsigned TitleBytes = 16, SavedProjectBytes = 28 + ProjectBytes + 4;
struct SavedProject {
    Project song;
    std::array<char, TitleBytes> title{};
    uint32_t revision = 0;
};
inline uint32_t projectFingerprint(const Project& song) {
    auto data = encodeProject(song); uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result |= uint32_t(data[ProjectBytes - 4 + i]) << (8 * i);
    return result;
}
inline void encodeSavedProjectTo(const Project& song, const char* title, uint32_t revision, uint8_t* data) {
    std::fill(data, data + SavedProjectBytes, 0);
    for (unsigned i = 0; i < 6; ++i) data[i] = "PSAVE1"[i];
    data[6] = 1;
    for (unsigned i = 0; i < TitleBytes - 1 && title[i]; ++i) data[8 + i] = title[i];
    for (unsigned i = 0; i < 4; ++i) data[24 + i] = revision >> (i * 8);
    encodeProjectTo(song, data + 28);
    uint32_t crc = crc32(data, SavedProjectBytes - 4);
    for (unsigned i = 0; i < 4; ++i) data[SavedProjectBytes - 4 + i] = crc >> (i * 8);
}
inline std::array<uint8_t, SavedProjectBytes> encodeSavedProject(const Project& song, const char* title, uint32_t revision) {
    std::array<uint8_t, SavedProjectBytes> data{};
    encodeSavedProjectTo(song, title, revision, data.data()); return data;
}
inline bool decodeSavedProject(const uint8_t* bytes, unsigned size, SavedProject& result) {
    // Legacy files have no title; leave the caller's default title intact.
    if (size == ProjectBytes || size == SongBytes) {
        if (!decodeProject(bytes, size, result.song)) return false;
        result.revision = 0; return true;
    }
    if (size != SavedProjectBytes || bytes[6] != 1) return false;
    for (unsigned i = 0; i < 6; ++i) if (bytes[i] != "PSAVE1"[i]) return false;
    uint32_t checksum = 0;
    for (unsigned i = 0; i < 4; ++i) checksum |= uint32_t(bytes[SavedProjectBytes - 4 + i]) << (i * 8);
    if (checksum != crc32(bytes, SavedProjectBytes - 4) || bytes[8 + TitleBytes - 1] != 0) return false;
    for (unsigned i = 0; i < TitleBytes - 1 && bytes[8 + i]; ++i)
        if (bytes[8 + i] < 32 || bytes[8 + i] > 126) return false;
    // Project decoding does not mutate the destination on failure.
    if (!decodeProject(bytes + 28, ProjectBytes, result.song)) return false;
    std::memcpy(result.title.data(), bytes + 8, TitleBytes);
    result.revision = 0;
    for (unsigned i = 0; i < 4; ++i) result.revision |= uint32_t(bytes[24 + i]) << (i * 8);
    return true;
}
}
