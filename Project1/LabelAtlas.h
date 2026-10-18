#pragma once
// =============================================================================
// LabelAtlas.h - GDI 로 버튼 라벨 텍스처 만들기 + 밉맵 생성
// =============================================================================
#include <windows.h>
#include <vector>
#include <cstdint>

// CPU 쪽 RGBA 이미지 한 장
struct ImageRGBA
{
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> pixels; // RGBA 8비트, 행 = width*4 바이트
};

// 버튼 라벨 4칸을 세로로 쌓은 아틀라스 (칸 크기·순서는 Scene.h 의 kLabelCell*, ButtonId)
ImageRGBA BuildButtonLabelAtlas();

// 밉맵 체인: [0] = 원본, [1] = 가로세로 1/2, [2] = 1/4 ... 최대 maxLevels 장
std::vector<ImageRGBA> GenerateMipChain(const ImageRGBA& base, UINT maxLevels);