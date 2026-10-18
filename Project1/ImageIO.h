#pragma once
// =============================================================================
// ImageIO.h - ★ [7단계] WIC(Windows Imaging Component)로 이미지 파일 읽기/쓰기
// DX 와 무관한 순수 이미지 처리 모듈. 실패하면 std::runtime_error 를 던짐
// =============================================================================
#include <windows.h>
#include <vector>
#include <cstdint>

// BGRA 8비트 픽셀 배열(빈틈없이 연속, 행 = width*4 바이트)을 PNG 로 저장
void SavePngBGRA(const wchar_t* path, UINT width, UINT height, const std::vector<uint8_t>& bgra);

// 이미지 파일을 읽어서 targetW×targetH 안에 비율 유지로 맞춘 뒤 가운데 배치
// 결과: 프리멀티플라이드 RGBA 8비트 (남는 여백은 투명 0)
void LoadImageFitPremulRGBA(const wchar_t* path, UINT targetW, UINT targetH, std::vector<uint8_t>& out);