// =============================================================================
// ImageIO.cpp
// =============================================================================
#include "ImageIO.h"
#include <wincodec.h>    // WIC
#include <wrl/client.h>  // ComPtr
#include <stdexcept>
#include <cstring>       // memcpy

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // HRESULT 실패 시 예외 (이 파일 전용)
    void Check(HRESULT hr, const char* msg)
    {
        if (FAILED(hr)) throw std::runtime_error(msg);
    }

    // WIC 의 모든 기능은 이 팩토리에서 시작 (COM 객체라 CoCreateInstance 로 생성)
    ComPtr<IWICImagingFactory> CreateWicFactory()
    {
        ComPtr<IWICImagingFactory> factory;
        Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)), "Failed to create WIC factory");
        return factory;
    }
}

// -----------------------------------------------------------------------------
// 저장: 스트림(파일) → 인코더(PNG) → 프레임(이미지 1장) → 픽셀 쓰기 → Commit
// -----------------------------------------------------------------------------
void SavePngBGRA(const wchar_t* path, UINT width, UINT height, const std::vector<uint8_t>& bgra)
{
    ComPtr<IWICImagingFactory> factory = CreateWicFactory();

    // 1) 파일 스트림
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream), "CreateStream");
    Check(stream->InitializeFromFilename(path, GENERIC_WRITE), "Cannot open file for writing");

    // 2) PNG 인코더를 스트림에 연결
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "CreateEncoder");
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Encoder Initialize");

    // 3) 프레임(이미지 한 장) 생성 + 크기/포맷 지정
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props; // 인코딩 옵션 (기본값 사용)
    Check(encoder->CreateNewFrame(&frame, &props), "CreateNewFrame");
    Check(frame->Initialize(props.Get()), "Frame Initialize");
    Check(frame->SetSize(width, height), "SetSize");

    // 원하는 포맷을 넘기면, 인코더가 지원 안 할 경우 가까운 포맷으로 바꿔서 돌려줌
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(frame->SetPixelFormat(&format), "SetPixelFormat");
    if (format != GUID_WICPixelFormat32bppBGRA)
        throw std::runtime_error("PNG encoder does not support 32bpp BGRA");

    // 4) 픽셀 쓰기. WIC API 가 const 를 안 받아서 const_cast (실제로 수정하진 않음)
    const UINT stride = width * 4;
    Check(frame->WritePixels(height, stride, (UINT)bgra.size(),
        const_cast<BYTE*>(bgra.data())), "WritePixels");

    // 5) 확정 (이걸 해야 파일에 실제로 기록됨)
    Check(frame->Commit(), "Frame Commit");
    Check(encoder->Commit(), "Encoder Commit");
}

// -----------------------------------------------------------------------------
// 불러오기: 디코더 → 포맷 변환기 → (크기가 다를 때만) 크기 조절기
//          → 최종 포맷 확인(다르면 변환기 하나 더) → CopyPixels
// 단계들이 "파이프라인"으로 연결만 되고, 마지막 CopyPixels 때 한 번에 실행됨
// -----------------------------------------------------------------------------
void LoadImageFitPremulRGBA(const wchar_t* path, UINT targetW, UINT targetH, std::vector<uint8_t>& out)
{
    ComPtr<IWICImagingFactory> factory = CreateWicFactory();

    // 1) 파일 형식(PNG/JPG/BMP...)을 자동 판별하는 디코더
    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder), "Cannot open image file");

    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0, &frame), "GetFrame"); // 첫 번째 프레임 (GIF 등은 여러 장)

    UINT srcW = 0, srcH = 0;
    Check(frame->GetSize(&srcW, &srcH), "GetSize");
    if (srcW == 0 || srcH == 0) throw std::runtime_error("Empty image");

    // 2) 어떤 포맷이든 "프리멀티플라이드 RGBA 8비트"로 변환 → 칠판 텍스처와 같은 형식
    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(&converter), "CreateFormatConverter");
    Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom), "Format Convert");

    // 파이프라인의 "현재 마지막 단계"를 가리키는 변수
    //   변환기, 크기 조절기 모두 IWICBitmapSource 를 상속해서 부모 타입 하나로 가리킬 수 있음
    ComPtr<IWICBitmapSource> source = converter;

    // 3) 비율 유지하며 목표 크기 안에 들어가는 크기 (가로/세로 중 더 작은 배율)
    const float sx = (float)targetW / (float)srcW;
    const float sy = (float)targetH / (float)srcH;
    const float scale = (sx < sy) ? sx : sy;

    UINT dstW = (UINT)(srcW * scale + 0.5f); // +0.5 후 버림 = 반올림
    UINT dstH = (UINT)(srcH * scale + 0.5f);
    if (dstW < 1) dstW = 1;
    if (dstH < 1) dstH = 1;
    if (dstW > targetW) dstW = targetW;
    if (dstH > targetH) dstH = targetH;

    // 크기가 다를 때만 크기 조절기 연결 (직접 저장한 PNG 는 크기가 같아서 그대로 통과)
    if (dstW != srcW || dstH != srcH)
    {
        ComPtr<IWICBitmapScaler> scaler;
        Check(factory->CreateBitmapScaler(&scaler), "CreateBitmapScaler");
        Check(scaler->Initialize(source.Get(), dstW, dstH, WICBitmapInterpolationModeFant), "Scale");
        source = scaler; // 파이프라인 끝 = 크기 조절기
    }

    // 4) 최종 포맷 확인
    //   크기 조절기는 입력과 "다른 픽셀 포맷"으로 내보낼 수 있음 (예: RGBA 대신 BGRA 순서)
    //   확인 없이 RGBA 로 읽으면 R 과 B 가 뒤바뀌어 색이 변함 → 다르면 변환기를 하나 더 붙임
    WICPixelFormatGUID finalFormat = {};
    Check(source->GetPixelFormat(&finalFormat), "GetPixelFormat");
    if (finalFormat != GUID_WICPixelFormat32bppPRGBA)
    {
        // 디버그 실행(F5) 시 VS "출력" 창에 표시
        OutputDebugStringA("[ImageIO] scaler output format differs -> converting back to 32bppPRGBA\n");

        ComPtr<IWICFormatConverter> fixer;
        Check(factory->CreateFormatConverter(&fixer), "CreateFormatConverter (fix)");
        Check(fixer->Initialize(source.Get(), GUID_WICPixelFormat32bppPRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom), "Format Convert (fix)");
        source = fixer; // 파이프라인 끝 = 보정 변환기
    }

    // 5) 최종 픽셀을 CPU 배열로 (여기서 디코딩→변환→크기조절이 실제로 실행됨)
    std::vector<uint8_t> pixels((size_t)dstW * dstH * 4);
    Check(source->CopyPixels(nullptr, dstW * 4, (UINT)pixels.size(), pixels.data()), "CopyPixels");

    // 6) 목표 크기 배열(전부 투명 0)의 가운데에 행 단위로 복사
    out.assign((size_t)targetW * targetH * 4, 0);
    const UINT offX = (targetW - dstW) / 2;
    const UINT offY = (targetH - dstH) / 2;
    for (UINT y = 0; y < dstH; ++y)
    {
        uint8_t* dst = &out[((size_t)(offY + y) * targetW + offX) * 4];
        const uint8_t* src = &pixels[(size_t)y * dstW * 4];
        memcpy(dst, src, (size_t)dstW * 4);
    }
}