// =============================================================================
// D3DUtil.cpp - 공용 헬퍼 구현
// =============================================================================
#include "D3DUtil.h"
#include <d3dcompiler.h>
#include <cstring>

// 라이브러리 링크는 프로젝트에서 한 곳에만 있으면 됨
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// 힙 종류: DEFAULT(GPU 전용) / UPLOAD(CPU 쓰기 → GPU 읽기) / READBACK(GPU → CPU)
D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES p = {};
    p.Type = type;
    p.CreationNodeMask = 1;
    p.VisibleNodeMask = 1;
    return p;
}

// 1차원 바이트 버퍼
D3D12_RESOURCE_DESC BufferDesc(UINT64 size)
{
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

// 2D 텍스처 설명 (깊이 버퍼, 칠판 텍스처, 라벨 텍스처가 공용으로 사용)
D3D12_RESOURCE_DESC Texture2DDesc(UINT width, UINT height, DXGI_FORMAT format,
    D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = width;
    d.Height = height;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;                            // 밉맵 없음
    d.Format = format;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;    // 드라이버가 최적 배치 결정
    d.Flags = flags;                            // 렌더 타겟 / 깊이 버퍼 용도 지정
    return d;
}

// 리소스 상태 전환 배리어
D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}

ComPtr<ID3DBlob> CompileShader(const char* src, const char* entry, const char* target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
        entry, target, flags, 0, &code, &errors);
    if (FAILED(hr))
    {
        const char* msg = errors ? (const char*)errors->GetBufferPointer() : "Shader compile failed";
        OutputDebugStringA(msg);
        throw std::runtime_error(msg);
    }
    return code;
}