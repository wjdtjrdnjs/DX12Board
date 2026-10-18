#pragma once
// =============================================================================
// Picking.h - 화면 좌표 → 3D 광선, 광선과 물체 충돌 판정
// =============================================================================
#include <windows.h>
#include <DirectXMath.h>

class Camera;

struct Ray
{
    DirectX::XMFLOAT3 origin;
    DirectX::XMFLOAT3 dir;
};

Ray  ScreenPointToRay(int x, int y, UINT width, UINT height, const Camera& camera);
bool RayHitBoard(const Ray& ray, DirectX::XMFLOAT2& outUV);

// 광선 vs 축 정렬 박스(AABB). 맞으면 true, outT = 광선 시작점부터 거리
bool RayHitAABB(const Ray& ray, const DirectX::XMFLOAT3& center,
    const DirectX::XMFLOAT3& halfExtents, float& outT);