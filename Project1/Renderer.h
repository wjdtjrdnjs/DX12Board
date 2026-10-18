#pragma once
// =============================================================================
// Renderer.h - DX12 렌더링 전담
// =============================================================================
#include "D3DUtil.h"
#include "Board.h"
#include "Scene.h"
#include <vector>
#include <cstdint> // uint8_t

class Camera; // 전방 선언: 포인터/참조로만 쓰니까 include 불필요

class Renderer
{
public:
    ~Renderer();

    void Init(HWND hwnd, UINT width, UINT height);
    void Resize(UINT width, UINT height);
    // 칠판 텍스처 갱신 → 씬 그리기 → Present (cursor = 칠판 위 브러시 크기 원)
    void Render(const Camera& camera, const std::vector<SceneObject>& objects,
        const BrushCursor& cursor);

    Board& GetBoard() { return m_board; }
    UINT   Width() const { return m_width; }
    UINT   Height() const { return m_height; }

    // 칠판 텍스처 픽셀 읽기/쓰기 (프리멀티플라이드 RGBA, 행 = 가로*4 바이트)
    // GPU 작업이 끝날 때까지 기다리는 동기 함수 → 프레임 사이에만 호출
    void ReadBoardPixels(std::vector<uint8_t>& outRGBA);
    void WriteBoardPixels(const std::vector<uint8_t>& rgba);

private:
    void CreateDevice(HWND hwnd);  // 디바이스 ~ 펜스까지
    void CreateRTVs();
    void CreateDepthBuffer();
    void BuildPipeline();          // 루트 시그니처 + PSO 3개(일반, 칠판, 버튼)
    void BuildGeometry();          // 박스 메쉬
    void InitBoard();              // 칠판 텍스처 생성 + 투명하게 초기화
    void BeginOneTimeCommands(); // 일회성 명령 기록 시작
    void ExecuteAndWait();       // 제출 후 GPU 완료까지 대기
    void CreateLabelAtlas();     // 버튼 라벨 텍스처 생성 + 업로드
    ComPtr<ID3D12Resource> CreateUploadBuffer(const void* data, UINT64 size);
    void WaitForGPU();
    void MoveToNextFrame();

private:
    bool m_initialized = false;
    UINT m_width = 0;
    UINT m_height = 0;

    // 핵심 객체
    ComPtr<IDXGIFactory6>             m_factory;
    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>        m_queue;
    ComPtr<IDXGISwapChain3>           m_swapChain;
    ComPtr<ID3D12CommandAllocator>    m_allocators[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;

    // 백버퍼 / 깊이
    ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
    UINT                              m_rtvSize = 0;
    ComPtr<ID3D12Resource>            m_backBuffers[kFrameCount];
    ComPtr<ID3D12DescriptorHeap>      m_dsvHeap;
    ComPtr<ID3D12Resource>            m_depthBuffer;

    // 셰이더에서 보이는 SRV 힙(0번 칠판, 1번 라벨) + 칠판 텍스처
    ComPtr<ID3D12DescriptorHeap>      m_srvHeap;
    Board                             m_board;
    UINT                              m_srvSize = 0;   // SRV 힙 한 칸 크기
    ComPtr<ID3D12Resource>            m_labelTexture;  // 라벨 아틀라스 (밉맵 포함)

    // 파이프라인
    ComPtr<ID3D12RootSignature>       m_rootSig;
    ComPtr<ID3D12PipelineState>       m_pso;       // 일반 물체용
    ComPtr<ID3D12PipelineState>       m_boardPso;  // 칠판 면용
    ComPtr<ID3D12PipelineState>       m_labelPso;      // 버튼용

    // 박스 메쉬
    ComPtr<ID3D12Resource>            m_vertexBuffer;
    ComPtr<ID3D12Resource>            m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW          m_vbv = {};
    D3D12_INDEX_BUFFER_VIEW           m_ibv = {};
    UINT                              m_indexCount = 0;

    // 동기화
    ComPtr<ID3D12Fence> m_fence;
    UINT64              m_fenceValues[kFrameCount] = {};
    HANDLE              m_fenceEvent = nullptr;
    UINT                m_frameIndex = 0;
};