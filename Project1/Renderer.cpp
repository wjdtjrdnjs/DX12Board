// =============================================================================
// Renderer.cpp
// =============================================================================
#include "Renderer.h"
#include "Camera.h"
#include "Shaders.h"
#include "LabelAtlas.h" // ★ [7-2단계]
#include <DirectXMath.h>
#include <cstring>
#include <cstdint>

using namespace DirectX;

// 익명 네임스페이스: 이 cpp 안에서만 쓰는 타입/상수
namespace
{
    struct Vertex
    {
        XMFLOAT3 pos;
        XMFLOAT3 normal;
        XMFLOAT2 uv;      // ★ [3단계] 텍스처 좌표
    };

    struct ObjectConstants  // 루트 상수 b0 (★ [7-2단계] 28개 float)
    {
        XMFLOAT4X4 world;
        XMFLOAT4   color;
        XMFLOAT4   uvRect;  // ★ [7-2단계]
        XMFLOAT4   params;  // ★ [7-2단계] x = 하이라이트
    };

    struct FrameConstants   // 루트 상수 b1 (★ [5단계] 28개 float)
    {
        XMFLOAT4X4 viewProj;
        XMFLOAT4   lightDir;
        XMFLOAT4   cursor;      // ★ [5단계] uv.x, uv.y, 반지름, 표시 여부
        XMFLOAT4   cursorColor; // ★ [5단계]
    };

    enum RootParam : UINT
    {
        kRootObject = 0,   // b0
        kRootFrame = 1,    // b1
        kRootTextures = 2, // ★ [7-2단계] t0(칠판) ~ t1(라벨) 디스크립터 테이블 (이름 변경)
    };
}

Renderer::~Renderer()
{
    if (m_initialized) WaitForGPU();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
}

void Renderer::Init(HWND hwnd, UINT width, UINT height)
{
    m_width = width;
    m_height = height;

    CreateDevice(hwnd);
    BuildPipeline();
    BuildGeometry();
    InitBoard();
    CreateLabelAtlas();   // ★ [7-2단계]

    m_initialized = true;
}

// -----------------------------------------------------------------------------
// 디바이스, 큐, 스왑체인, 디스크립터 힙, 깊이 버퍼, 커맨드 리스트, 펜스
// -----------------------------------------------------------------------------
void Renderer::CreateDevice(HWND hwnd)
{
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory2");

    // 고성능 하드웨어 GPU 선택
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;
        m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
        ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
            break;
    }
    if (!m_device) throw std::runtime_error("No D3D12 device");

    // 커맨드 큐
    D3D12_COMMAND_QUEUE_DESC qDesc = {};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_queue)), "CommandQueue");

    // 스왑체인
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = m_width;
    scDesc.Height = m_height;
    scDesc.Format = kBackBufferFormat;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = kFrameCount;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &scDesc,
        nullptr, nullptr, &sc1), "SwapChain");
    ThrowIfFailed(sc1.As(&m_swapChain), "SwapChain3");
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // RTV 힙 + 백버퍼 RTV
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = kFrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "RTV Heap");
    m_rtvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CreateRTVs();

    // DSV 힙 + 깊이 버퍼
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)), "DSV Heap");
    CreateDepthBuffer();

    // ★ [7-2단계] SRV 힙 2칸: 0번 = 칠판 텍스처, 1번 = 버튼 라벨 아틀라스
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 2;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)), "SRV Heap");
    m_srvSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 커맨드 할당자 / 리스트
    for (UINT i = 0; i < kFrameCount; ++i)
        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_allocators[i])), "CommandAllocator");
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_allocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_cmdList)), "CommandList");
    m_cmdList->Close();

    // 펜스
    ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&m_fence)), "Fence");
    m_fenceValues[m_frameIndex]++;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) throw std::runtime_error("CreateEvent failed");
}

void Renderer::CreateRTVs()
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrameCount; ++i)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])), "GetBuffer");
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, h);
        h.ptr += m_rtvSize;
    }
}

void Renderer::CreateDepthBuffer()
{
    m_depthBuffer.Reset();

    D3D12_RESOURCE_DESC desc = Texture2DDesc(m_width, m_height, kDepthFormat,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = kDepthFormat;
    clear.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&m_depthBuffer)), "DepthBuffer");

    m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

// -----------------------------------------------------------------------------
// 루트 시그니처 + PSO (일반 / 칠판 / ★ 버튼)
// -----------------------------------------------------------------------------
void Renderer::BuildPipeline()
{
    // ★ [7-2단계] t0, t1 연속 2칸짜리 디스크립터 범위 (SRV 힙 0번, 1번과 대응)
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 2;
    srvRange.BaseShaderRegister = 0; // t0 부터
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3] = {};

    params[kRootObject].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[kRootObject].Constants.ShaderRegister = 0;
    params[kRootObject].Constants.Num32BitValues = sizeof(ObjectConstants) / 4; // 28
    params[kRootObject].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[kRootFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[kRootFrame].Constants.ShaderRegister = 1;
    params[kRootFrame].Constants.Num32BitValues = sizeof(FrameConstants) / 4;   // 28
    params[kRootFrame].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[kRootTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootTextures].DescriptorTable.NumDescriptorRanges = 1;
    params[kRootTextures].DescriptorTable.pDescriptorRanges = &srvRange;
    params[kRootTextures].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 정적 샘플러 (MIP_LINEAR 라서 라벨 밉맵도 자동으로 부드럽게 보간됨)
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = _countof(params);
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr))
        throw std::runtime_error(errBlob ? (const char*)errBlob->GetBufferPointer() : "SerializeRootSignature");
    ThrowIfFailed(m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSig)), "RootSignature");

    ComPtr<ID3DBlob> vs = CompileShader(kShaderSource, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = CompileShader(kShaderSource, "PSMain", "ps_5_0");
    ComPtr<ID3DBlob> psBoard = CompileShader(kShaderSource, "PSBoard", "ps_5_0");
    ComPtr<ID3DBlob> psLabel = CompileShader(kShaderSource, "PSLabel", "ps_5_0"); // ★ [7-2단계]

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_rootSig.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { inputLayout, _countof(inputLayout) };

    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso.RasterizerState.FrontCounterClockwise = FALSE;
    pso.RasterizerState.DepthClipEnable = TRUE;

    D3D12_RENDER_TARGET_BLEND_DESC& rtBlend = pso.BlendState.RenderTarget[0];
    rtBlend.BlendEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_ONE;
    rtBlend.DestBlend = D3D12_BLEND_ZERO;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState.StencilEnable = FALSE;
    const D3D12_DEPTH_STENCILOP_DESC stencilOp =
    { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
    pso.DepthStencilState.FrontFace = stencilOp;
    pso.DepthStencilState.BackFace = stencilOp;

    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = kBackBufferFormat;
    pso.DSVFormat = kDepthFormat;
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)), "PSO");

    pso.PS = { psBoard->GetBufferPointer(), psBoard->GetBufferSize() };
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_boardPso)), "Board PSO");

    // ★ [7-2단계] 버튼 PSO: 픽셀 셰이더만 교체
    pso.PS = { psLabel->GetBufferPointer(), psLabel->GetBufferSize() };
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_labelPso)), "Label PSO");
}
ComPtr<ID3D12Resource> Renderer::CreateUploadBuffer(const void* data, UINT64 size)
{
    D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC desc = BufferDesc(size);

    ComPtr<ID3D12Resource> buffer;
    ThrowIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)), "UploadBuffer");

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    ThrowIfFailed(buffer->Map(0, &readRange, &mapped), "Map");
    memcpy(mapped, data, (size_t)size);
    buffer->Unmap(0, nullptr);
    return buffer;
}

// -----------------------------------------------------------------------------
// 단위 박스. ★ [3단계] 면마다 UV 추가
// 각 면을 바깥에서 볼 때: 좌하(0,1) → 좌상(0,0) → 우상(1,0) → 우하(1,1)
// 텍스처 좌표는 좌상단이 (0,0), 아래로 갈수록 v 증가
// -----------------------------------------------------------------------------
void Renderer::BuildGeometry()
{
    const Vertex vertices[] =
    {
        // -Z 면 (앞, 칠판 그림이 보이는 면)
        { {-0.5f,-0.5f,-0.5f}, {0.0f,0.0f,-1.0f}, {0.0f,1.0f} }, { {-0.5f, 0.5f,-0.5f}, {0.0f,0.0f,-1.0f}, {0.0f,0.0f} },
        { { 0.5f, 0.5f,-0.5f}, {0.0f,0.0f,-1.0f}, {1.0f,0.0f} }, { { 0.5f,-0.5f,-0.5f}, {0.0f,0.0f,-1.0f}, {1.0f,1.0f} },
        // +Z 면 (뒤)
        { { 0.5f,-0.5f, 0.5f}, {0.0f,0.0f, 1.0f}, {0.0f,1.0f} }, { { 0.5f, 0.5f, 0.5f}, {0.0f,0.0f, 1.0f}, {0.0f,0.0f} },
        { {-0.5f, 0.5f, 0.5f}, {0.0f,0.0f, 1.0f}, {1.0f,0.0f} }, { {-0.5f,-0.5f, 0.5f}, {0.0f,0.0f, 1.0f}, {1.0f,1.0f} },
        // -X 면 (왼쪽)
        { {-0.5f,-0.5f, 0.5f}, {-1.0f,0.0f,0.0f}, {0.0f,1.0f} }, { {-0.5f, 0.5f, 0.5f}, {-1.0f,0.0f,0.0f}, {0.0f,0.0f} },
        { {-0.5f, 0.5f,-0.5f}, {-1.0f,0.0f,0.0f}, {1.0f,0.0f} }, { {-0.5f,-0.5f,-0.5f}, {-1.0f,0.0f,0.0f}, {1.0f,1.0f} },
        // +X 면 (오른쪽)
        { { 0.5f,-0.5f,-0.5f}, { 1.0f,0.0f,0.0f}, {0.0f,1.0f} }, { { 0.5f, 0.5f,-0.5f}, { 1.0f,0.0f,0.0f}, {0.0f,0.0f} },
        { { 0.5f, 0.5f, 0.5f}, { 1.0f,0.0f,0.0f}, {1.0f,0.0f} }, { { 0.5f,-0.5f, 0.5f}, { 1.0f,0.0f,0.0f}, {1.0f,1.0f} },
        // +Y 면 (위)
        { {-0.5f, 0.5f,-0.5f}, {0.0f, 1.0f,0.0f}, {0.0f,1.0f} }, { {-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f,0.0f}, {0.0f,0.0f} },
        { { 0.5f, 0.5f, 0.5f}, {0.0f, 1.0f,0.0f}, {1.0f,0.0f} }, { { 0.5f, 0.5f,-0.5f}, {0.0f, 1.0f,0.0f}, {1.0f,1.0f} },
        // -Y 면 (아래)
        { { 0.5f,-0.5f,-0.5f}, {0.0f,-1.0f,0.0f}, {0.0f,1.0f} }, { { 0.5f,-0.5f, 0.5f}, {0.0f,-1.0f,0.0f}, {0.0f,0.0f} },
        { {-0.5f,-0.5f, 0.5f}, {0.0f,-1.0f,0.0f}, {1.0f,0.0f} }, { {-0.5f,-0.5f,-0.5f}, {0.0f,-1.0f,0.0f}, {1.0f,1.0f} },
    };

    std::vector<uint16_t> indices;
    for (uint16_t face = 0; face < 6; ++face)
    {
        uint16_t b = face * 4;
        indices.push_back(b + 0); indices.push_back(b + 1); indices.push_back(b + 2);
        indices.push_back(b + 0); indices.push_back(b + 2); indices.push_back(b + 3);
    }
    m_indexCount = (UINT)indices.size();

    const UINT vbSize = sizeof(vertices);
    const UINT ibSize = (UINT)(indices.size() * sizeof(uint16_t));
    m_vertexBuffer = CreateUploadBuffer(vertices, vbSize);
    m_indexBuffer = CreateUploadBuffer(indices.data(), ibSize);

    m_vbv.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbv.SizeInBytes = vbSize;
    m_vbv.StrideInBytes = sizeof(Vertex); // ★ [3단계] 32바이트로 늘어남

    m_ibv.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_ibv.SizeInBytes = ibSize;
    m_ibv.Format = DXGI_FORMAT_R16_UINT;
}

// -----------------------------------------------------------------------------
// 칠판 텍스처 생성 + 초기 클리어
// ★ [4단계] 테스트 패턴 대신 Board::Init 이 요청해 둔 투명 클리어를 Flush 로 실행
// -----------------------------------------------------------------------------
void Renderer::InitBoard()
{
    m_board.Init(m_device.Get(), kBoardTexWidth, kBoardTexHeight,
        m_srvHeap->GetCPUDescriptorHandleForHeapStart());

    ThrowIfFailed(m_allocators[m_frameIndex]->Reset(), "Allocator Reset");
    ThrowIfFailed(m_cmdList->Reset(m_allocators[m_frameIndex].Get(), nullptr), "CmdList Reset");

    m_board.Flush(m_cmdList.Get(), m_frameIndex);                                     // 투명 클리어
    m_board.Transition(m_cmdList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);  // 읽기용으로

    ThrowIfFailed(m_cmdList->Close(), "CmdList Close");
    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    WaitForGPU();
}

void Renderer::Resize(UINT width, UINT height)
{
    if (!m_initialized || width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;

    WaitForGPU();
    for (UINT i = 0; i < kFrameCount; ++i)
    {
        m_backBuffers[i].Reset();
        m_fenceValues[i] = m_fenceValues[m_frameIndex];
    }
    ThrowIfFailed(m_swapChain->ResizeBuffers(kFrameCount, width, height, kBackBufferFormat, 0), "ResizeBuffers");

    m_width = width;
    m_height = height;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    CreateRTVs();
    CreateDepthBuffer();
}

// -----------------------------------------------------------------------------
// 한 프레임 렌더링
// 1) 칠판 텍스처에 이번 프레임 도장 찍기  2) 씬 그리기
// -----------------------------------------------------------------------------
void Renderer::Render(const Camera& camera, const std::vector<SceneObject>& objects,
    const BrushCursor& cursor)
{
    FrameConstants fc;
    const float aspect = (float)m_width / (float)m_height;
    XMStoreFloat4x4(&fc.viewProj, XMMatrixTranspose(camera.GetView() * camera.GetProj(aspect)));
    fc.lightDir = XMFLOAT4(0.3f, -0.6f, 1.0f, 0.0f);
    fc.cursor = XMFLOAT4(cursor.uv.x, cursor.uv.y, cursor.radiusPx, cursor.visible ? 1.0f : 0.0f);
    fc.cursorColor = cursor.color;

    auto& alloc = m_allocators[m_frameIndex];
    ThrowIfFailed(alloc->Reset(), "Allocator Reset");
    ThrowIfFailed(m_cmdList->Reset(alloc.Get(), m_pso.Get()), "CmdList Reset");

    m_board.Flush(m_cmdList.Get(), m_frameIndex);
    m_board.Transition(m_cmdList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    ID3D12Resource* backBuffer = m_backBuffers[m_frameIndex].Get();
    D3D12_RESOURCE_BARRIER toRT = TransitionBarrier(backBuffer,
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_cmdList->ResourceBarrier(1, &toRT);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += SIZE_T(m_frameIndex) * m_rtvSize;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    const float wallColor[4] = { 0.93f, 0.92f, 0.89f, 1.0f };
    m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    m_cmdList->ClearRenderTargetView(rtv, wallColor, 0, nullptr);
    m_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, (LONG)m_width, (LONG)m_height };
    m_cmdList->RSSetViewports(1, &vp);
    m_cmdList->RSSetScissorRects(1, &scissor);

    m_cmdList->SetGraphicsRootSignature(m_rootSig.Get());

    // 테이블이 힙 0번부터 2칸(t0, t1)을 가리킴
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    m_cmdList->SetGraphicsRootDescriptorTable(kRootTextures, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    m_cmdList->SetGraphicsRoot32BitConstants(kRootFrame, sizeof(FrameConstants) / 4, &fc, 0);

    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmdList->IASetVertexBuffers(0, 1, &m_vbv);
    m_cmdList->IASetIndexBuffer(&m_ibv);

    ID3D12PipelineState* currentPso = nullptr;
    for (const SceneObject& obj : objects)
    {
        // ★ [7-2단계] 종류별 PSO 선택
        ID3D12PipelineState* wantPso = m_pso.Get();
        switch (obj.type)
        {
        case ObjectType::Board: wantPso = m_boardPso.Get(); break;
        case ObjectType::Label: wantPso = m_labelPso.Get(); break;
        default: break;
        }
        if (wantPso != currentPso)
        {
            m_cmdList->SetPipelineState(wantPso);
            currentPso = wantPso;
        }

        // ★ [7-2단계] lift(위로) + push(안쪽으로) 반영
        XMMATRIX world = XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z)
            * XMMatrixTranslation(obj.position.x,
                obj.position.y + obj.lift,
                obj.position.z + obj.push);

        ObjectConstants oc;
        XMStoreFloat4x4(&oc.world, XMMatrixTranspose(world));
        oc.color = obj.color;
        oc.uvRect = obj.uvRect;                              // ★ [7-2단계]
        oc.params = XMFLOAT4(obj.highlight, 0.0f, 0.0f, 0.0f); // ★ [7-2단계]

        m_cmdList->SetGraphicsRoot32BitConstants(kRootObject, sizeof(ObjectConstants) / 4, &oc, 0);
        m_cmdList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
    }

    D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_cmdList->ResourceBarrier(1, &toPresent);

    ThrowIfFailed(m_cmdList->Close(), "CmdList Close");
    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    ThrowIfFailed(m_swapChain->Present(1, 0), "Present");
    MoveToNextFrame();
}
void Renderer::WaitForGPU()
{
    ThrowIfFailed(m_queue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]), "Signal");
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), "SetEvent");
    WaitForSingleObject(m_fenceEvent, INFINITE);
    m_fenceValues[m_frameIndex]++;
}

void Renderer::MoveToNextFrame()
{
    const UINT64 currentValue = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_queue->Signal(m_fence.Get(), currentValue), "Signal");
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent), "SetEvent");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_fenceValues[m_frameIndex] = currentValue + 1;
}

// -----------------------------------------------------------------------------
// ★ [7-2단계] 버튼 라벨 아틀라스: CPU 에서 그리기 → 밉맵 → 텍스처 업로드 → SRV(1번 칸)
// -----------------------------------------------------------------------------
void Renderer::CreateLabelAtlas()
{
    // 1) CPU 이미지 + 밉맵 (6단계: 512 → 256 → ... → 16)
    ImageRGBA base = BuildButtonLabelAtlas();
    std::vector<ImageRGBA> mips = GenerateMipChain(base, 6);
    const UINT mipCount = (UINT)mips.size();

    // 2) 밉맵 여러 장을 가진 텍스처 (복사 받을 거라 COPY_DEST 로 생성)
    D3D12_RESOURCE_DESC desc = Texture2DDesc(base.width, base.height,
        DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
    desc.MipLevels = (UINT16)mipCount;

    D3D12_HEAP_PROPERTIES defaultHeap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_labelTexture)), "Label Texture");

    // 3) 서브리소스(밉 레벨)마다 업로드 버퍼 안 배치 계산
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mipCount);
    std::vector<UINT>   numRows(mipCount);
    std::vector<UINT64> rowBytes(mipCount);
    UINT64 totalBytes = 0;
    m_device->GetCopyableFootprints(&desc, 0, mipCount, 0,
        footprints.data(), numRows.data(), rowBytes.data(), &totalBytes);

    // 4) 업로드 버퍼에 모든 레벨을 RowPitch 간격으로 복사
    D3D12_HEAP_PROPERTIES uploadHeap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC bufDesc = BufferDesc(totalBytes);
    ComPtr<ID3D12Resource> upload;
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)), "Label Upload");

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    ThrowIfFailed(upload->Map(0, &readRange, reinterpret_cast<void**>(&mapped)), "Map Label Upload");

    for (UINT m = 0; m < mipCount; ++m)
    {
        const ImageRGBA& img = mips[m];
        const size_t rowSize = (size_t)img.width * 4;
        uint8_t* dst = mapped + footprints[m].Offset;
        for (UINT y = 0; y < img.height; ++y)
            memcpy(dst + (size_t)y * footprints[m].Footprint.RowPitch, &img.pixels[y * rowSize], rowSize);
    }
    upload->Unmap(0, nullptr);

    // 5) 레벨마다 CopyTextureRegion → 읽기 상태로 전환
    BeginOneTimeCommands();
    for (UINT m = 0; m < mipCount; ++m)
    {
        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = m_labelTexture.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = m; // 밉 레벨 번호 = 서브리소스 번호 (배열 1장일 때)

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = upload.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = footprints[m];

        m_cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    }
    D3D12_RESOURCE_BARRIER toRead = TransitionBarrier(m_labelTexture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_cmdList->ResourceBarrier(1, &toRead);
    ExecuteAndWait();

    // 6) SRV 를 힙 1번 칸에 (0번은 칠판)
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += m_srvSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = mipCount; // 모든 밉 레벨 사용
    m_device->CreateShaderResourceView(m_labelTexture.Get(), &srv, handle);
}

// =============================================================================
// ★ [7단계] 저장/불러오기
// =============================================================================

// 프레임 사이(MoveToNextFrame 직후)에는 현재 프레임 할당자를 GPU가 다 썼음이 보장돼서 리셋 가능
void Renderer::BeginOneTimeCommands()
{
    ThrowIfFailed(m_allocators[m_frameIndex]->Reset(), "Allocator Reset");
    ThrowIfFailed(m_cmdList->Reset(m_allocators[m_frameIndex].Get(), nullptr), "CmdList Reset");
}

void Renderer::ExecuteAndWait()
{
    ThrowIfFailed(m_cmdList->Close(), "CmdList Close");
    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    WaitForGPU(); // 일회성 작업이라 끝까지 기다려도 괜찮음
}

// -----------------------------------------------------------------------------
// 텍스처 → READBACK 버퍼 → CPU 배열
// -----------------------------------------------------------------------------
void Renderer::ReadBoardPixels(std::vector<uint8_t>& outRGBA)
{
    // 1) 텍스처를 버퍼에 복사할 때의 메모리 배치(footprint) 계산
    //    행 하나의 실제 간격(RowPitch)은 256바이트 배수로 정렬돼서 가로*4 보다 클 수 있음
    D3D12_RESOURCE_DESC texDesc = m_board.Texture()->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT   numRows = 0;
    UINT64 rowBytes = 0, totalBytes = 0;
    m_device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowBytes, &totalBytes);

    // 2) READBACK 힙 버퍼: GPU가 쓰고 CPU가 읽는 메모리
    D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC bufDesc = BufferDesc(totalBytes);
    ComPtr<ID3D12Resource> readback;
    ThrowIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "Readback Buffer");

    // 3) 아직 안 찍힌 도장까지 반영한 뒤 복사
    BeginOneTimeCommands();
    m_board.Flush(m_cmdList.Get(), m_frameIndex);
    m_board.CopyToBuffer(m_cmdList.Get(), readback.Get(), footprint);
    m_board.Transition(m_cmdList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ExecuteAndWait();

    // 4) Map 해서 행 단위로 꺼내기 (RowPitch 패딩 제거 → 빈틈없는 배열로)
    const UINT   width = (UINT)texDesc.Width;
    const UINT   height = texDesc.Height;
    const size_t rowSize = (size_t)width * 4;
    outRGBA.resize(rowSize * height);

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, (SIZE_T)totalBytes }; // CPU가 읽을 범위
    ThrowIfFailed(readback->Map(0, &readRange, &mapped), "Map Readback");

    const uint8_t* src = static_cast<const uint8_t*>(mapped) + footprint.Offset;
    for (UINT y = 0; y < height; ++y)
        memcpy(&outRGBA[y * rowSize], src + (size_t)y * footprint.Footprint.RowPitch, rowSize);

    D3D12_RANGE writeRange = { 0, 0 }; // CPU가 쓴 범위 없음
    readback->Unmap(0, &writeRange);
}

// -----------------------------------------------------------------------------
// CPU 배열 → UPLOAD 버퍼 → 텍스처
// -----------------------------------------------------------------------------
void Renderer::WriteBoardPixels(const std::vector<uint8_t>& rgba)
{
    D3D12_RESOURCE_DESC texDesc = m_board.Texture()->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT   numRows = 0;
    UINT64 rowBytes = 0, totalBytes = 0;
    m_device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowBytes, &totalBytes);

    const UINT   width = (UINT)texDesc.Width;
    const UINT   height = texDesc.Height;
    const size_t rowSize = (size_t)width * 4;
    if (rgba.size() != rowSize * height)
        throw std::runtime_error("WriteBoardPixels: size mismatch");

    // 1) UPLOAD 버퍼에 RowPitch 간격으로 행 복사
    D3D12_HEAP_PROPERTIES heap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC bufDesc = BufferDesc(totalBytes);
    ComPtr<ID3D12Resource> upload;
    ThrowIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)), "Upload Buffer");

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    ThrowIfFailed(upload->Map(0, &readRange, &mapped), "Map Upload");

    uint8_t* dst = static_cast<uint8_t*>(mapped) + footprint.Offset;
    for (UINT y = 0; y < height; ++y)
        memcpy(dst + (size_t)y * footprint.Footprint.RowPitch, &rgba[y * rowSize], rowSize);
    upload->Unmap(0, nullptr);

    // 2) 버퍼 → 텍스처 복사. 대기 중이던 도장은 덮어써질 거라 버림
    BeginOneTimeCommands();
    m_board.DiscardPending();
    m_board.CopyFromBuffer(m_cmdList.Get(), upload.Get(), footprint);
    m_board.Transition(m_cmdList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ExecuteAndWait();
    // 함수가 끝나면 upload 가 해제됨. GPU 복사가 이미 끝났으니 안전
}
