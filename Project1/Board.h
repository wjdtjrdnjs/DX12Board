#pragma once
// =============================================================================
// Board.h - 칠판 그림 텍스처 + 브러시 도장 그리기 + 저장/불러오기 지원
// =============================================================================
#include "D3DUtil.h"
#include <DirectXMath.h>
#include <vector>

// 현재 붓 설정 (App 이 들고 있고, 그릴 때 넘겨줌)
struct Brush
{
    DirectX::XMFLOAT4 color = { 0.95f, 0.95f, 0.95f, 1.0f };
    float radius = 6.0f;   // 텍스처 픽셀 단위 반지름
    bool  eraser = false;

    // 실제 적용 반지름 (지우개는 4배). 그리기와 커서 표시가 같은 값을 쓰게 한 곳에서 계산
    float EffectiveRadius() const { return eraser ? radius * 4.0f : radius; }
};

// 칠판 위에 표시할 브러시 크기 원
struct BrushCursor
{
    bool              visible = false;
    DirectX::XMFLOAT2 uv = {};          // 칠판 UV
    float             radiusPx = 0.0f;  // 텍스처 픽셀 반지름
    DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

class Board
{
public:
    // srvHandle: 렌더러의 SRV 힙에서 이 텍스처용으로 비워둔 칸
    void Init(ID3D12Device* device, UINT width, UINT height,
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);

    // 그리기 API (CPU 쪽: 도장 목록에 쌓기만 함)
    void StrokeTo(DirectX::XMFLOAT2 uv, const Brush& brush); // 선을 이 점까지 이어 그림
    void EndStroke();                                        // 선 끊기
    void RequestClear();                                     // 다음 Flush 때 전체 지우기

    // 쌓인 클리어/도장을 GPU 명령으로 기록 (프레임마다 렌더러가 호출)
    void Flush(ID3D12GraphicsCommandList* cmd, UINT frameIndex);

    // 현재 상태와 다를 때만 배리어 기록
    void Transition(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);

    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }

    // ★ [7단계] 저장/불러오기용 ← 이 부분이 빠졌던 것
    ID3D12Resource* Texture() const { return m_texture.Get(); }
    void DiscardPending(); // 대기 중인 도장/클리어 버리기 (불러오기 직전)

    // 텍스처 → 버퍼 (저장: READBACK 버퍼로)
    void CopyToBuffer(ID3D12GraphicsCommandList* cmd, ID3D12Resource* buffer,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint);
    // 버퍼 → 텍스처 (불러오기: UPLOAD 버퍼에서)
    void CopyFromBuffer(ID3D12GraphicsCommandList* cmd, ID3D12Resource* buffer,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint);

private:
    // GPU로 보내는 도장 하나 (셰이더 StampIn 의 인스턴스 데이터와 일치, 32바이트)
    struct StampInstance
    {
        DirectX::XMFLOAT2 center; // NDC
        DirectX::XMFLOAT2 radius; // NDC
        DirectX::XMFLOAT4 color;
    };

    // CPU 대기열의 도장: GPU 데이터 + 분필/지우개 구분
    struct PendingStamp
    {
        StampInstance data;
        bool          erase;
    };

    void BuildPipeline(ID3D12Device* device);
    void AddStamp(DirectX::XMFLOAT2 pixel, float radius, const Brush& brush);

private:
    static constexpr UINT kMaxStampsPerFrame = 8192; // 한 프레임 최대 도장 수

    // 텍스처
    ComPtr<ID3D12Resource>       m_texture;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    D3D12_RESOURCE_STATES        m_state = D3D12_RESOURCE_STATE_COMMON;
    UINT m_width = 0;
    UINT m_height = 0;

    // 브러시 파이프라인
    ComPtr<ID3D12RootSignature> m_rootSig;
    ComPtr<ID3D12PipelineState> m_chalkPso;  // 덧칠 블렌드
    ComPtr<ID3D12PipelineState> m_erasePso;  // 깎아내기 블렌드

    // 프레임별 인스턴스 버퍼
    ComPtr<ID3D12Resource> m_instanceBuffers[kFrameCount];
    StampInstance* m_mapped[kFrameCount] = {}; // 계속 Map 해둔 CPU 주소

    // 선 그리기 상태
    std::vector<PendingStamp> m_pending;
    bool                      m_clearRequested = false;
    bool                      m_hasLast = false;
    DirectX::XMFLOAT2         m_last = {};
};