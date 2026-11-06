#pragma once
#include <cstdint>

// Match the original synthesizer palette, including every surface/selection.
// No extra framebuffer or runtime theme allocation.
namespace studio_ui {
constexpr uint16_t rgb(unsigned r,unsigned g,unsigned b) {
    return uint16_t(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}
constexpr uint16_t Background=0x0842; // Original synthesizer background on every page.
constexpr uint16_t Panel=0x1082;
constexpr uint16_t Selection=0x2104;
constexpr uint16_t Idle=0x4208;
constexpr uint16_t Border=0x632c;
constexpr uint16_t Text=0xffff;
constexpr uint16_t Muted=0x7bef;
constexpr uint16_t Accent=0x07ff;
constexpr uint16_t Purple=0xb81f;
constexpr uint16_t Yellow=0xffe0;
constexpr uint16_t Green=0x07e0;
constexpr uint16_t Danger=0xf800;
}
