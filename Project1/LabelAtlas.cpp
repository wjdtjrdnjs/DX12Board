// =============================================================================
// LabelAtlas.cpp
// =============================================================================
#include "LabelAtlas.h"
#include "Scene.h"
#include <cwchar>
#include <stdexcept>
#include <utility>   // std::move

#pragma comment(lib, "gdi32.lib")

namespace
{
    // 버튼 하나의 라벨 정보
    struct LabelSpec
    {
        const wchar_t* title;      // 큰 글씨
        const wchar_t* shortcut;   // 작은 글씨 (단축키)
        COLORREF       background; // 버튼 바탕색
    };

    // ButtonId 순서와 반드시 같아야 함
    const LabelSpec kLabelSpecs[kButtonCount] =
    {
        { L"새로 만들기",        L"Ctrl+N",       RGB(236, 234, 226) }, // 연회색
        { L"불러오기",           L"Ctrl+O",       RGB(250, 214, 150) }, // 주황
        { L"저장",               L"Ctrl+S",       RGB(170, 210, 245) }, // 파랑
        { L"다른 이름으로 저장", L"Ctrl+Shift+S", RGB(175, 230, 205) }, // 민트
    };

    // 맑은 고딕 폰트 생성 (size = 글자 높이 픽셀)
    HFONT MakeFont(int size, int weight)
    {
        // 높이를 음수로 주면 "줄 간격 제외한 글자 자체 높이"로 해석됨
        return CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,             // 회색조 안티에일리어싱
            DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic");
    }

    // maxWidth 안에 들어갈 때까지 글자 크기를 줄여가며 폰트 생성
    // (긴 라벨 "다른 이름으로 저장"도 자동으로 칸에 맞춰짐)
    HFONT CreateFittingFont(HDC dc, const wchar_t* text, int maxWidth, int maxHeight, int weight)
    {
        for (int size = maxHeight; size >= 10; size -= 2)
        {
            HFONT font = MakeFont(size, weight);

            // 폰트를 잠깐 DC 에 끼워서 글자 폭 측정
            HGDIOBJ old = SelectObject(dc, font);
            SIZE extent = {};
            GetTextExtentPoint32W(dc, text, (int)wcslen(text), &extent);
            SelectObject(dc, old); // 원래 폰트로 복구

            if (extent.cx <= maxWidth) return font; // 들어가면 이 폰트 사용
            DeleteObject(font);                     // 안 들어가면 지우고 더 작게
        }
        return MakeFont(10, weight);
    }
}

// -----------------------------------------------------------------------------
// GDI 로 메모리 비트맵에 라벨을 그리고 RGBA 배열로 꺼냄
// -----------------------------------------------------------------------------
ImageRGBA BuildButtonLabelAtlas()
{
    const int cellW = (int)kLabelCellWidth;
    const int cellH = (int)kLabelCellHeight;
    const int width = cellW;
    const int height = cellH * kButtonCount;

    // 1) 32비트 DIB 섹션: CPU 가 픽셀 메모리에 직접 접근할 수 있는 비트맵
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;     // 음수 = 위에서 아래로 저장(top-down). 양수면 거꾸로
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;        // 픽셀당 B, G, R, (빈칸) 4바이트
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC dc = CreateCompatibleDC(nullptr); // 화면 없는 메모리 DC (그리기 도화지 역할)
    void* bits = nullptr;                 // 픽셀 메모리 시작 주소가 여기로 들어옴
    HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bitmap || !bits) throw std::runtime_error("CreateDIBSection failed");

    HGDIOBJ oldBitmap = SelectObject(dc, bitmap); // DC 에 비트맵 끼우기 → 이제 그리면 여기에 그려짐
    SetBkMode(dc, TRANSPARENT);                   // 글자 배경은 칠하지 않음

    HPEN borderPen = CreatePen(PS_SOLID, 6, RGB(120, 100, 70)); // 테두리 펜 (6픽셀 갈색)

    for (int i = 0; i < kButtonCount; ++i)
    {
        const LabelSpec& spec = kLabelSpecs[i];
        const int top = i * cellH; // 이 칸의 시작 y

        // 2) 바탕
        RECT cell = { 0, top, cellW, top + cellH };
        HBRUSH bgBrush = CreateSolidBrush(spec.background);
        FillRect(dc, &cell, bgBrush);
        DeleteObject(bgBrush);

        // 3) 둥근 테두리 (속은 안 채우게 NULL_BRUSH)
        HGDIOBJ oldPen = SelectObject(dc, borderPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, 10, top + 10, cellW - 10, top + cellH - 10, 36, 36);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);

        // 4) 큰 글씨: 칸의 위쪽 2/3 영역
        RECT titleRect = { 24, top + 16, cellW - 24, top + (int)(cellH * 0.66f) };
        HFONT titleFont = CreateFittingFont(dc, spec.title, titleRect.right - titleRect.left,
            (int)(cellH * 0.36f), FW_BOLD);
        HGDIOBJ oldFont = SelectObject(dc, titleFont);
        SetTextColor(dc, RGB(45, 40, 35));
        DrawTextW(dc, spec.title, -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 5) 작은 글씨(단축키): 아래쪽
        RECT keyRect = { 24, top + (int)(cellH * 0.62f), cellW - 24, top + cellH - 22 };
        HFONT keyFont = CreateFittingFont(dc, spec.shortcut, keyRect.right - keyRect.left,
            (int)(cellH * 0.16f), FW_NORMAL);
        SelectObject(dc, keyFont);
        SetTextColor(dc, RGB(95, 88, 80));
        DrawTextW(dc, spec.shortcut, -1, &keyRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 폰트는 DC 에서 빼낸 다음에 삭제해야 함
        SelectObject(dc, oldFont);
        DeleteObject(titleFont);
        DeleteObject(keyFont);
    }

    GdiFlush(); // GDI 가 모아둔 그리기 명령을 지금 다 실행 (픽셀 읽기 전에 필수)

    // 6) DIB 의 B,G,R,X 순서 → R,G,B,A 로 변환. GDI 는 알파를 안 쓰므로 255 로 채움
    ImageRGBA image;
    image.width = (UINT)width;
    image.height = (UINT)height;
    image.pixels.resize((size_t)width * height * 4);

    const uint8_t* src = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < (size_t)width * height; ++i)
    {
        image.pixels[i * 4 + 0] = src[i * 4 + 2]; // R
        image.pixels[i * 4 + 1] = src[i * 4 + 1]; // G
        image.pixels[i * 4 + 2] = src[i * 4 + 0]; // B
        image.pixels[i * 4 + 3] = 255;            // A
    }

    // 7) GDI 객체 정리 (만든 역순으로, DC 에서 빼낸 뒤 삭제)
    DeleteObject(borderPen);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);

    return image;
}

// -----------------------------------------------------------------------------
// 2×2 픽셀 평균으로 절반 크기 이미지를 반복 생성 (박스 필터)
// -----------------------------------------------------------------------------
std::vector<ImageRGBA> GenerateMipChain(const ImageRGBA& base, UINT maxLevels)
{
    std::vector<ImageRGBA> levels;
    levels.push_back(base);

    while (levels.size() < maxLevels)
    {
        const ImageRGBA& prev = levels.back(); // 주의: push_back 하면 이 참조는 무효가 됨
        if (prev.width == 1 && prev.height == 1) break;

        ImageRGBA next;
        next.width = (prev.width > 1) ? prev.width / 2 : 1;
        next.height = (prev.height > 1) ? prev.height / 2 : 1;
        next.pixels.resize((size_t)next.width * next.height * 4);

        // 이전 레벨의 (x, y) 픽셀 c 채널 값
        auto at = [&prev](UINT x, UINT y, int c) -> UINT
            {
                return prev.pixels[((size_t)y * prev.width + x) * 4 + c];
            };

        for (UINT y = 0; y < next.height; ++y)
        {
            for (UINT x = 0; x < next.width; ++x)
            {
                // 대응하는 2×2 영역 (홀수 크기 끝부분은 같은 픽셀 재사용)
                const UINT x0 = x * 2, y0 = y * 2;
                const UINT x1 = (x0 + 1 < prev.width) ? x0 + 1 : x0;
                const UINT y1 = (y0 + 1 < prev.height) ? y0 + 1 : y0;

                for (int c = 0; c < 4; ++c)
                {
                    const UINT sum = at(x0, y0, c) + at(x1, y0, c) + at(x0, y1, c) + at(x1, y1, c);
                    next.pixels[((size_t)y * next.width + x) * 4 + c] = (uint8_t)((sum + 2) / 4); // 반올림 평균
                }
            }
        }

        levels.push_back(std::move(next)); // 복사 대신 이동 (픽셀 배열을 통째로 넘김)
    }
    return levels;
}