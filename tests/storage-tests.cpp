#include "SavedProject.h"
#include <cstdio>
#include <cstdlib>
using namespace pocket;
void require(bool test, const char* label) { if (!test) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); } }
void repairOuterCRC(std::array<uint8_t, SavedProjectBytes>& data) {
    uint32_t crc = crc32(data.data(), data.size() - 4);
    for (unsigned i = 0; i < 4; ++i) data[data.size() - 4 + i] = crc >> (8 * i);
}
int main() {
    Project p; p.drums.bpm = 137; p.drums.steps[0] = 0x101;
    p.tracks[2].count = 1; p.tracks[2].events[0] = {24, 48, 67, 100};
    auto encoded = encodeSavedProject(p, "Night loop", 17);
    SavedProject result;
    require(decodeSavedProject(encoded.data(), encoded.size(), result) && result.revision == 17
        && std::strcmp(result.title.data(), "Night loop") == 0 && result.song.drums.bpm == 137
        && result.song.tracks[2].events[0].midi == 67, "named slot roundtrip");
    auto before = result;
    encoded[100] ^= 1;
    require(!decodeSavedProject(encoded.data(), encoded.size(), result) && result.revision == before.revision
        && result.title == before.title && projectFingerprint(result.song) == projectFingerprint(before.song), "corrupt record rejected unchanged");
    require(!decodeSavedProject(encoded.data(), encoded.size() - 1, result), "truncated record rejected");
    encoded = encodeSavedProject(p, "Night loop", 18); encoded[8] = 0xc0; repairOuterCRC(encoded);
    require(!decodeSavedProject(encoded.data(), encoded.size(), result), "nonprintable title rejected");
    encoded = encodeSavedProject(p, "Night loop", 18); encoded[8 + TitleBytes - 1] = 'x'; repairOuterCRC(encoded);
    require(!decodeSavedProject(encoded.data(), encoded.size(), result), "unterminated title rejected");
    encoded = encodeSavedProject(p, "12345678901234567890", 0xffffffff);
    require(decodeSavedProject(encoded.data(), encoded.size(), result) && strlen(result.title.data()) == 15
        && result.revision == 0xffffffff, "title bounded and full revision retained");
    encoded = encodeSavedProject(p, "Night loop", 18); encoded[40] ^= 1; repairOuterCRC(encoded);
    require(!decodeSavedProject(encoded.data(), encoded.size(), result), "inner project checksum independently checked");
    auto v3 = encodeProject(p); strcpy(result.title.data(), "SLOT 2");
    require(decodeSavedProject(v3.data(), v3.size(), result) && result.revision == 0
        && strcmp(result.title.data(), "SLOT 2") == 0 && result.song.tracks[2].count == 1, "v0.3 imported with default title");
    auto v2 = encodeSong(p.drums);
    require(decodeSavedProject(v2.data(), v2.size(), result) && result.song.tracks[2].count == 0
        && result.song.drums.bpm == 137, "v0.2 imported");
    uint32_t initial = projectFingerprint(p); ++p.drums.hits[0]; p.drums.notes = 1; p.drums.running = true;
    require(projectFingerprint(p) == initial, "live controls do not dirty a saved project");
    ++p.tracks[2].events[0].duration;
    require(projectFingerprint(p) != initial, "edited notes mark project unsaved");
    std::printf("PASS: %u-byte named slots, checksums, metadata, legacy imports and unsaved tracking\n", SavedProjectBytes);
}
