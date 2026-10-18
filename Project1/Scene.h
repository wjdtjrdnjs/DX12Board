#pragma once
// =============================================================================
// Scene.h - 씬 배치 데이터
// =============================================================================
#include <DirectXMath.h>
#include <vector>

constexpr float        kBoardWidth = 4.0f;
constexpr float        kBoardHeight = 2.2f;
constexpr float        kBoardCenterY = 1.6f;
constexpr unsigned int kBoardTexWidth = 2048;
constexpr unsigned int kBoardTexHeight = 1126;

inline const DirectX::XMFLOAT4 kBoardColor = { 0.23f, 0.36f, 0.22f, 1.0f };

constexpr int kChalkCount = 4;
inline const DirectX::XMFLOAT4 kChalkColors[kChalkCount] =
{
    { 0.95f, 0.95f, 0.95f, 1.0f },
    { 0.98f, 0.88f, 0.30f, 1.0f },
    { 0.95f, 0.45f, 0.45f, 1.0f },
    { 0.45f, 0.70f, 0.95f, 1.0f },
};

constexpr int kPaletteCount = 8;
inline const DirectX::XMFLOAT4 kPaletteColors[kPaletteCount] =
{
    { 1.00f, 0.60f, 0.20f, 1.0f },
    { 0.55f, 0.85f, 0.40f, 1.0f },
    { 0.70f, 0.50f, 0.90f, 1.0f },
    { 0.98f, 0.60f, 0.80f, 1.0f },
    { 0.40f, 0.90f, 0.90f, 1.0f },
    { 0.60f, 0.40f, 0.25f, 1.0f },
    { 0.60f, 0.60f, 0.60f, 1.0f },
    { 0.08f, 0.08f, 0.08f, 1.0f },
};

// ★ [7-2단계] 파일 버튼. 순서 = 라벨 아틀라스에서 위에서부터의 순서
enum class ButtonId
{
    New = 0,  // 새로 만들기
    Open,     // 불러오기
    Save,     // 저장
    SaveAs,   // 다른 이름으로 저장
};
constexpr int          kButtonCount = 4;
constexpr unsigned int kLabelCellWidth = 512;  // 라벨 한 칸 크기 (버튼 가로:세로 = 0.48:0.18 비율)
constexpr unsigned int kLabelCellHeight = 192;

enum class ObjectType
{
    Default,
    Board,
    Label,    // ★ [7-2단계] 앞면에 라벨 텍스처가 붙는 버튼
};

enum class ToolKind
{
    None,
    Chalk,
    Eraser,
    Palette,
    Button,   // ★ [7-2단계] toolIndex = ButtonId
};

struct SceneObject
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 scale;
    DirectX::XMFLOAT4 color;
    ObjectType        type = ObjectType::Default;
    ToolKind          tool = ToolKind::None;
    int               toolIndex = 0;
    float             lift = 0.0f;
    DirectX::XMFLOAT4 uvRect = { 0.0f, 0.0f, 1.0f, 1.0f }; // ★ [7-2단계] 아틀라스 영역 (xy 시작, zw 크기)
    float             push = 0.0f;      // ★ [7-2단계] 눌렸을 때 안쪽(+Z)으로 들어가는 거리
    float             highlight = 0.0f; // ★ [7-2단계] 마우스 올렸을 때 밝기 (0~1)
};

std::vector<SceneObject> BuildBlackboardScene();