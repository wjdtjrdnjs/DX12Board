#pragma once
// =============================================================================
// D3DUtil.h - DX12 공용 헬퍼. DX를 쓰는 모든 파일이 include 함
// =============================================================================

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

// HRESULT 실패 시 예외 던지기
inline void ThrowIfFailed(HRESULT hr, const char* msg = "HRESULT failed")
{
    if (FAILED(hr)) throw std::runtime_error(msg);
}

// 프로젝트 전역 상수 (헤더의 constexpr 변수는 파일마다 복사돼도 링크 에러 없음)
constexpr UINT        kFrameCount = 2;                              // 백버퍼 개수
constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

// 리소스 생성용 설명 구조체 헬퍼 (구현은 D3DUtil.cpp)
D3D12_HEAP_PROPERTIES  HeapProps(D3D12_HEAP_TYPE type);
D3D12_RESOURCE_DESC    BufferDesc(UINT64 size);
D3D12_RESOURCE_DESC    Texture2DDesc(UINT width, UINT height, DXGI_FORMAT format,
    D3D12_RESOURCE_FLAGS flags);
D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after);

// HLSL 문자열 → 바이트코드
ComPtr<ID3DBlob> CompileShader(const char* src, const char* entry, const char* target);