// =============================================================================
// Board.cpp
// =============================================================================
#include "Board.h"
#include "Shaders.h"
#include <cmath>

using namespace DirectX;

void Board::Init(ID3D12Device* device, UINT width, UINT height,
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
    m_width = width;
    m_height = height;

    // ---------------- 칠판 텍스처 (렌더 타겟 + SRV) ----------------
    D3D12_RESOURCE_DESC desc = Texture2DDesc(width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Color 는 {} 로 이미 전부 0 (투명)

    D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    m_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        m_state, &clear, IID_PPV_ARGS(&m_texture)), "Board Texture");

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "Board RTV Heap");
    device->CreateRenderTargetView(m_texture.Get(), nullptr, m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_texture.Get(), &srv, srvHandle);

    // ---------------- 브러시 파이프라인 ----------------
    BuildPipeline(device);

    // ---------------- 프레임별 인스턴스 버퍼 ----------------
    const UINT64 bufferSize = sizeof(StampInstance) * kMaxStampsPerFrame; // 256KB
    D3D12_HEAP_PROPERTIES uploadHeap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC bufferDesc = BufferDesc(bufferSize);

    for (UINT i = 0; i < kFrameCount; ++i)
    {
        ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_instanceBuffers[i])),
            "Stamp Instance Buffer");

        // UPLOAD 힙은 계속 Map 해둬도 됨 (Persistent Mapping) → 매 프레임 Map/Unmap 비용 없음
        void* ptr = nullptr;
        D3D12_RANGE readRange = { 0, 0 };
        ThrowIfFailed(m_instanceBuffers[i]->Map(0, &readRange, &ptr), "Map Stamp Buffer");
        m_mapped[i] = static_cast<StampInstance*>(ptr);
    }

    m_pending.reserve(kMaxStampsPerFrame); // 미리 메모리 확보 (push_back 때 재할당 방지)
    m_clearRequested = true;               // 첫 Flush 때 투명하게 초기화
}

// -----------------------------------------------------------------------------
// 브러시 파이프라인: 루트 시그니처(빈 것) + PSO 2개(분필, 지우개)
// -----------------------------------------------------------------------------
void Board::BuildPipeline(ID3D12Device* device)
{
    // 셰이더가 상수/텍스처를 안 쓰므로 파라미터 없는 루트 시그니처
    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr))
        throw std::runtime_error(errBlob ? (const char*)errBlob->GetBufferPointer() : "Brush RootSignature");
    ThrowIfFailed(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSig)), "Brush RootSignature");

    ComPtr<ID3DBlob> vs = CompileShader(kBrushShaderSource, "VSStamp", "vs_5_0");
    ComPtr<ID3DBlob> ps = CompileShader(kBrushShaderSource, "PSStamp", "ps_5_0");

    // 입력 레이아웃: 전부 "인스턴스마다" 바뀌는 데이터 (PER_INSTANCE, 스텝 1 = 인스턴스 1개당 1칸 전진)
    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "CENTER", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "RADIUS", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "COLOR",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_rootSig.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { inputLayout, _countof(inputLayout) };

    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // 2D 사각형이라 컬링 불필요
    pso.RasterizerState.DepthClipEnable = TRUE;

    // 분필 블렌드 (프리멀티플라이드 "덮어 칠하기")
    //   결과 = 새 색 * 1 + 기존 색 * (1 - 새 알파)
    D3D12_RENDER_TARGET_BLEND_DESC& b = pso.BlendState.RenderTarget[0];
    b.BlendEnable = TRUE;
    b.SrcBlend = D3D12_BLEND_ONE;
    b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    b.BlendOp = D3D12_BLEND_OP_ADD;
    b.SrcBlendAlpha = D3D12_BLEND_ONE;
    b.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    b.LogicOp = D3D12_LOGIC_OP_NOOP;
    b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // 2D 그리기라 깊이 안 씀
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso.DepthStencilState.StencilEnable = FALSE;
    const D3D12_DEPTH_STENCILOP_DESC stencilOp =
    { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
    pso.DepthStencilState.FrontFace = stencilOp;
    pso.DepthStencilState.BackFace = stencilOp;

    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // 칠판 텍스처 포맷
    pso.DSVFormat = DXGI_FORMAT_UNKNOWN;            // 깊이 버퍼 없음
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_chalkPso)), "Chalk PSO");

    // 지우개 블렌드 ("깎아내기")
    //   결과 = 새 색 * 0 + 기존 색 * (1 - 새 알파)  → 알파만큼 기존 그림이 사라짐
    b.SrcBlend = D3D12_BLEND_ZERO;
    b.SrcBlendAlpha = D3D12_BLEND_ZERO;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_erasePso)), "Erase PSO");
}

// -----------------------------------------------------------------------------
// 선 그리기: 이전 점 → 현재 점 사이에 일정 간격으로 도장
// 마우스 이벤트는 띄엄띄엄 오니까 사이를 채워야 선이 끊기지 않음
// -----------------------------------------------------------------------------
void Board::StrokeTo(XMFLOAT2 uv, const Brush& brush)
{
    const XMFLOAT2 p = { uv.x * m_width, uv.y * m_height };       // UV → 텍스처 픽셀
    const float radius = brush.EffectiveRadius(); // 지우개는 4배 크기

    // 선의 첫 점: 도장 하나 찍고 시작
    if (!m_hasLast)
    {
        AddStamp(p, radius, brush);
        m_last = p;
        m_hasLast = true;
        return;
    }

    const float dx = p.x - m_last.x;
    const float dy = p.y - m_last.y;
    const float dist = sqrtf(dx * dx + dy * dy);

    float spacing = radius * 0.25f;     // 반지름의 1/4 간격 → 매끈한 선
    if (spacing < 1.0f) spacing = 1.0f;
    if (dist < spacing) return;         // 아직 가까우면 m_last 유지하고 다음 이벤트 기다림

    const int   steps = (int)(dist / spacing); // 들어갈 도장 수
    const float stepT = spacing / dist;        // 도장 하나당 진행 비율

    for (int i = 1; i <= steps; ++i)
    {
        const float t = stepT * i;
        AddStamp({ m_last.x + dx * t, m_last.y + dy * t }, radius, brush);
    }

    // 마지막으로 찍은 위치까지만 전진 (남은 거리는 다음 이벤트에 이어서)
    m_last.x += dx * stepT * steps;
    m_last.y += dy * stepT * steps;
}

void Board::EndStroke()
{
    m_hasLast = false;
}

void Board::RequestClear()
{
    m_clearRequested = true;
    m_pending.clear(); // 아직 안 찍은 도장도 버림
}

// 텍스처 픽셀 좌표 도장 → NDC 로 변환해서 대기열에 추가
void Board::AddStamp(XMFLOAT2 pixel, float radius, const Brush& brush)
{
    if (m_pending.size() >= kMaxStampsPerFrame) return; // 버퍼 넘치면 버림

    PendingStamp s;
    // 픽셀(0~W) → NDC(-1~1), y는 뒤집힘 (텍스처는 아래로, NDC는 위로 증가)
    s.data.center = { pixel.x / m_width * 2.0f - 1.0f,
                      1.0f - pixel.y / m_height * 2.0f };
    s.data.radius = { radius / m_width * 2.0f, radius / m_height * 2.0f };
    s.data.color = brush.color;
    if (brush.eraser) s.data.color.w = 1.0f; // 지우개는 알파(세기)만 의미 있음
    s.erase = brush.eraser;

    m_pending.push_back(s);
}

// -----------------------------------------------------------------------------
// 대기열 → GPU 명령
// -----------------------------------------------------------------------------
void Board::Flush(ID3D12GraphicsCommandList* cmd, UINT frameIndex)
{
    if (!m_clearRequested && m_pending.empty()) return; // 할 일 없으면 상태 전환도 안 함

    Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    if (m_clearRequested)
    {
        const float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        cmd->ClearRenderTargetView(rtv, transparent, 0, nullptr);
        m_clearRequested = false;
    }

    if (!m_pending.empty())
    {
        // 1) CPU 대기열 → 이번 프레임용 업로드 버퍼로 복사
        const UINT count = (UINT)m_pending.size();
        StampInstance* dst = m_mapped[frameIndex];
        for (UINT i = 0; i < count; ++i)
            dst[i] = m_pending[i].data;

        // 2) 칠판 텍스처를 렌더 타겟으로, 뷰포트는 텍스처 전체
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
        D3D12_RECT scissor = { 0, 0, (LONG)m_width, (LONG)m_height };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &scissor);

        cmd->SetGraphicsRootSignature(m_rootSig.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); // 정점 4개 = 사각형

        // 인스턴스 버퍼를 정점 버퍼 슬롯 0에 바인딩
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = m_instanceBuffers[frameIndex]->GetGPUVirtualAddress();
        vbv.SizeInBytes = count * sizeof(StampInstance);
        vbv.StrideInBytes = sizeof(StampInstance);
        cmd->IASetVertexBuffers(0, 1, &vbv);

        // 3) 같은 모드(분필/지우개)끼리 연속 구간을 묶어서 한 번에 그림
        UINT start = 0;
        while (start < count)
        {
            const bool erase = m_pending[start].erase;
            UINT end = start + 1;
            while (end < count && m_pending[end].erase == erase) ++end;

            cmd->SetPipelineState(erase ? m_erasePso.Get() : m_chalkPso.Get());
            // 정점 4개짜리 인스턴스를 (end - start)개, 버퍼의 start 번째 인스턴스부터
            cmd->DrawInstanced(4, end - start, 0, start);

            start = end;
        }

        m_pending.clear(); // 크기만 0으로 (reserve 한 메모리는 유지)
    }
}

void Board::Transition(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
{
    if (m_state == after) return;
    D3D12_RESOURCE_BARRIER b = TransitionBarrier(m_texture.Get(), m_state, after);
    cmd->ResourceBarrier(1, &b);
    m_state = after;
}

// -----------------------------------------------------------------------------
// 저장/불러오기 지원
// -----------------------------------------------------------------------------
void Board::DiscardPending()
{
    m_pending.clear();
    m_clearRequested = false;
    m_hasLast = false;
}

void Board::CopyToBuffer(ID3D12GraphicsCommandList* cmd, ID3D12Resource* buffer,
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint)
{
    Transition(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE); // 복사 원본 상태로

    D3D12_TEXTURE_COPY_LOCATION dst = {};               // 목적지: 버퍼 (텍스처 모양 배치)
    dst.pResource = buffer;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION src = {};               // 원본: 텍스처 0번 서브리소스
    src.pResource = m_texture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}

void Board::CopyFromBuffer(ID3D12GraphicsCommandList* cmd, ID3D12Resource* buffer,
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint)
{
    Transition(cmd, D3D12_RESOURCE_STATE_COPY_DEST); // 복사 대상 상태로

    D3D12_TEXTURE_COPY_LOCATION dst = {};             // 목적지: 텍스처
    dst.pResource = m_texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};             // 원본: 업로드 버퍼
    src.pResource = buffer;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}