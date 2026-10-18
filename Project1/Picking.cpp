// =============================================================================
// Picking.cpp
// =============================================================================
#include "Picking.h"
#include "Camera.h"
#include "Scene.h"
#include <cmath>
#include <cfloat>   // FLT_MAX
#include <utility>  // std::swap

using namespace DirectX;

Ray ScreenPointToRay(int x, int y, UINT width, UINT height, const Camera& camera)
{
    const float ndcX = 2.0f * (x + 0.5f) / width - 1.0f;
    const float ndcY = 1.0f - 2.0f * (y + 0.5f) / height;

    const float aspect = (float)width / (float)height;
    XMMATRIX viewProj = camera.GetView() * camera.GetProj(aspect);
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

    XMVECTOR nearPt = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
    XMVECTOR farPt = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);

    Ray ray;
    XMStoreFloat3(&ray.origin, nearPt);
    XMStoreFloat3(&ray.dir, XMVector3Normalize(farPt - nearPt));
    return ray;
}

bool RayHitBoard(const Ray& ray, XMFLOAT2& outUV)
{
    if (fabsf(ray.dir.z) < 1e-6f) return false;
    const float t = -ray.origin.z / ray.dir.z;
    if (t < 0.0f) return false;

    const float hx = ray.origin.x + ray.dir.x * t;
    const float hy = ray.origin.y + ray.dir.y * t;

    const float left = -kBoardWidth * 0.5f;
    const float top = kBoardCenterY + kBoardHeight * 0.5f;
    const float u = (hx - left) / kBoardWidth;
    const float v = (top - hy) / kBoardHeight;

    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return false;
    outUV = { u, v };
    return true;
}

// -----------------------------------------------------------------------------
// 광선 vs AABB: 슬랩(Slab) 방식
// 박스 = x, y, z 각 축의 "두 평면 사이 구간(슬랩)" 3개가 겹친 영역.
// 축마다 광선이 슬랩에 들어가는 t(tEnter)와 나오는 t(tExit)를 구하고,
// 세 축 구간의 교집합 [가장 늦은 진입, 가장 이른 탈출] 이 비어있지 않으면 충돌
// -----------------------------------------------------------------------------
bool RayHitAABB(const Ray& ray, const XMFLOAT3& center, const XMFLOAT3& halfExtents, float& outT)
{
    // 축별로 반복하려고 배열로 펼침
    const float o[3] = { ray.origin.x, ray.origin.y, ray.origin.z };
    const float d[3] = { ray.dir.x,    ray.dir.y,    ray.dir.z };
    const float c[3] = { center.x,     center.y,     center.z };
    const float e[3] = { halfExtents.x, halfExtents.y, halfExtents.z };

    float tMin = 0.0f;     // 광선 시작점보다 뒤는 무시
    float tMax = FLT_MAX;

    for (int i = 0; i < 3; ++i)
    {
        const float minB = c[i] - e[i]; // 이 축의 박스 최소 경계
        const float maxB = c[i] + e[i]; // 최대 경계

        if (fabsf(d[i]) < 1e-8f)
        {
            // 광선이 이 축과 평행: 시작점이 슬랩 안에 있어야만 가능
            if (o[i] < minB || o[i] > maxB) return false;
        }
        else
        {
            const float inv = 1.0f / d[i];
            float t1 = (minB - o[i]) * inv;  // 최소 경계 평면과 만나는 t
            float t2 = (maxB - o[i]) * inv;  // 최대 경계 평면과 만나는 t
            if (t1 > t2) std::swap(t1, t2);  // 방향이 음수면 순서가 뒤집히므로 정렬

            if (t1 > tMin) tMin = t1;        // 진입은 가장 늦은 것
            if (t2 < tMax) tMax = t2;        // 탈출은 가장 이른 것
            if (tMin > tMax) return false;   // 구간이 비면 빗나감
        }
    }

    outT = tMin; // 처음 박스 표면에 닿는 거리
    return true;
}