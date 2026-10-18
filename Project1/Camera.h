#pragma once
// =============================================================================
// Camera.h - 1인칭 자유 카메라 (WASD/QE 이동, 우클릭 드래그 회전)
// =============================================================================
#include <windows.h>
#include <DirectXMath.h>

class Camera
{
public:
    void Update(float dt, bool hasFocus);  // 키보드로 이동 (창 활성 상태일 때만)

    void BeginRotate(int x, int y);        // 우클릭 누름
    void EndRotate();                      // 우클릭 뗌
    void OnMouseMove(int x, int y);        // 회전 중이면 yaw/pitch 변경

    DirectX::XMMATRIX GetView() const;               // 뷰 행렬
    DirectX::XMMATRIX GetProj(float aspect) const;   // 투영 행렬
    DirectX::XMFLOAT3 GetPosition() const { return m_pos; }

private:
    DirectX::XMVECTOR Forward() const;     // yaw/pitch → 앞 방향
    DirectX::XMVECTOR Right() const;       // 수평 오른쪽 방향

private:
    DirectX::XMFLOAT3 m_pos = { 0.0f, 1.4f, -4.5f };
    float m_yaw = 0.0f;          // 좌우 회전 (라디안)
    float m_pitch = -0.05f;      // 상하 회전 (라디안)
    float m_fovDeg = 60.0f;      // 세로 시야각
    bool  m_rotating = false;
    POINT m_lastMouse = {};
};