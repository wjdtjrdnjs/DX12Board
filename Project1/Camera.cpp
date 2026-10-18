// =============================================================================
// Camera.cpp
// =============================================================================
#include "Camera.h"
#include <cmath>

using namespace DirectX; // cpp 안에서만 사용 (헤더엔 안 씀)

XMVECTOR Camera::Forward() const
{
    // 구면 좌표 → 직교 좌표. yaw=0, pitch=0 이면 (0,0,1)
    const float cp = cosf(m_pitch);
    return XMVectorSet(cp * sinf(m_yaw), sinf(m_pitch), cp * cosf(m_yaw), 0.0f);
}

XMVECTOR Camera::Right() const
{
    // 수평면에서 forward 를 오른쪽으로 90도 돌린 방향
    return XMVectorSet(cosf(m_yaw), 0.0f, -sinf(m_yaw), 0.0f);
}

void Camera::Update(float dt, bool hasFocus)
{
    if (!hasFocus) return; // 다른 창 쓰는 중엔 키 무시

    float speed = 3.0f * dt;                                 // 초당 3m
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) speed *= 3.0f;  // Shift = 3배

    const XMVECTOR forward = Forward();
    const XMVECTOR right = Right();
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMVECTOR move = XMVectorZero();
    if (GetAsyncKeyState('W') & 0x8000) move += forward;
    if (GetAsyncKeyState('S') & 0x8000) move -= forward;
    if (GetAsyncKeyState('D') & 0x8000) move += right;
    if (GetAsyncKeyState('A') & 0x8000) move -= right;
    if (GetAsyncKeyState('E') & 0x8000) move += up;
    if (GetAsyncKeyState('Q') & 0x8000) move -= up;

    XMVECTOR pos = XMLoadFloat3(&m_pos);
    pos += move * speed;
    XMStoreFloat3(&m_pos, pos);
}

void Camera::BeginRotate(int x, int y)
{
    m_rotating = true;
    m_lastMouse = { x, y };
}

void Camera::EndRotate()
{
    m_rotating = false;
}

void Camera::OnMouseMove(int x, int y)
{
    if (!m_rotating) return;

    const float sensitivity = 0.004f;              // 픽셀당 라디안
    m_yaw += (x - m_lastMouse.x) * sensitivity;  // 오른쪽 드래그 → 오른쪽 회전
    m_pitch -= (y - m_lastMouse.y) * sensitivity;  // 아래 드래그 → 아래 보기

    // 90도 넘어가면 뒤집히므로 제한
    if (m_pitch > 1.5f) m_pitch = 1.5f;
    if (m_pitch < -1.5f) m_pitch = -1.5f;

    m_lastMouse = { x, y };
}

XMMATRIX Camera::GetView() const
{
    return XMMatrixLookToLH(XMLoadFloat3(&m_pos), Forward(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

XMMATRIX Camera::GetProj(float aspect) const
{
    return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fovDeg), aspect, 0.1f, 100.0f);
}