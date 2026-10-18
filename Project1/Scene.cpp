// =============================================================================
// Scene.cpp - 칠판 앞면은 z = 0 평면, 카메라는 -Z 쪽에서 +Z 를 바라봄
// =============================================================================
#include "Scene.h"

using namespace DirectX;

std::vector<SceneObject> BuildBlackboardScene()
{
    std::vector<SceneObject> objects;

    const XMFLOAT4 wood = { 0.84f, 0.67f, 0.42f, 1.0f };
    const XMFLOAT4 woodDark = { 0.70f, 0.52f, 0.30f, 1.0f };
    const XMFLOAT4 board = { 0.23f, 0.36f, 0.22f, 1.0f };
    const XMFLOAT4 yellowE = { 0.98f, 0.92f, 0.42f, 1.0f };
    const XMFLOAT4 greenE = { 0.30f, 0.50f, 0.15f, 1.0f };
    const XMFLOAT4 paper = { 0.90f, 0.88f, 0.82f, 1.0f }; // ★ [5단계] 팔레트 받침

    // ★ [5단계] 도구 정보(tool, toolIndex)까지 받도록 확장. 뒤쪽 인자는 기본값 있음
    auto add = [&objects](XMFLOAT3 pos, XMFLOAT3 scale, XMFLOAT4 color,
        ObjectType type = ObjectType::Default,
        ToolKind tool = ToolKind::None, int toolIndex = 0)
        {
            SceneObject obj;
            obj.position = pos;
            obj.scale = scale;
            obj.color = color;
            obj.type = type;
            obj.tool = tool;
            obj.toolIndex = toolIndex;
            objects.push_back(obj);
        };

    const float w = kBoardWidth, h = kBoardHeight, y = kBoardCenterY;
    const float t = 0.12f;

    // 칠판 면
    add({ 0.0f, y, 0.03f }, { w, h, 0.06f }, board, ObjectType::Board);

    // 테두리
    add({ 0.0f, y + h * 0.5f + t * 0.5f, 0.0f }, { w + t * 2.0f, t, t }, wood);
    add({ 0.0f, y - h * 0.5f - t * 0.5f, 0.0f }, { w + t * 2.0f, t, t }, wood);
    add({ -w * 0.5f - t * 0.5f, y, 0.0f }, { t, h, t }, wood);
    add({ w * 0.5f + t * 0.5f, y, 0.0f }, { t, h, t }, wood);

    // 선반 (x: -1.8 ~ 1.8)
    const float trayTop = y - h * 0.5f - t;
    add({ 0.0f, trayTop - 0.02f, -0.12f }, { 3.6f, 0.04f, 0.30f }, woodDark);

    // ★ [5단계] 지우개: 박스 2개 모두 Eraser 로 표시 (어느 쪽을 클릭해도 선택)
    add({ -1.2f, trayTop + 0.04f, -0.14f }, { 0.30f, 0.08f, 0.12f }, yellowE,
        ObjectType::Default, ToolKind::Eraser, 0);
    add({ -1.2f, trayTop + 0.09f, -0.14f }, { 0.30f, 0.02f, 0.12f }, greenE,
        ObjectType::Default, ToolKind::Eraser, 0);

    // ★ [5단계] 분필 4개: 간격을 0.4m로 줄여서 팔레트 자리 확보 (x: -0.6 ~ 0.6)
    const XMFLOAT3 chalkSize = { 0.18f, 0.03f, 0.03f };
    for (int i = 0; i < kChalkCount; ++i)
        add({ -0.6f + 0.4f * i, trayTop + 0.015f, -0.14f }, chalkSize, kChalkColors[i],
            ObjectType::Default, ToolKind::Chalk, i);

    // ★ [5단계] 팔레트: 받침판 + 색 블록 8개 (x: 0.95 ~ 1.7)
    add({ 1.325f, trayTop + 0.005f, -0.14f }, { 0.70f, 0.01f, 0.16f }, paper);

    const float cube = 0.06f; // 색 블록 한 변
    for (int i = 0; i < kPaletteCount; ++i)
        add({ 1.01f + 0.085f * i, trayTop + 0.01f + cube * 0.5f, -0.14f },
            { cube, cube, cube }, kPaletteColors[i],
            ObjectType::Default, ToolKind::Palette, i);

    // ★ [7-2단계] 파일 버튼 패널 (칠판 오른쪽)
    // 칠판 오른쪽 테두리 바깥 = w/2 + t = 2.12 → 조금 띄워서 패널 중심 x = 2.56
    const float panelX = w * 0.5f + t + 0.44f;
    add({ panelX, 1.62f, 0.04f }, { 0.62f, 1.02f, 0.04f }, wood); // 받침 패널 (뒤쪽)

    // 아틀라스 전체 높이 (셀 4칸)
    const float atlasH = (float)(kLabelCellHeight * kButtonCount);

    for (int i = 0; i < kButtonCount; ++i)
    {
        // 위에서부터 0.24m 간격, 앞면이 패널보다 튀어나오게 z = 0
        add({ panelX, 1.98f - 0.24f * i, 0.0f }, { 0.48f, 0.18f, 0.04f }, woodDark,
            ObjectType::Label, ToolKind::Button, i);

        // 방금 추가한 버튼이 아틀라스의 몇 번째 칸을 쓸지 지정
        // 위아래 칸 색이 번져 보이지 않게 2픽셀씩 안쪽만 사용
        SceneObject& button = objects.back();
        button.uvRect = { 2.0f / kLabelCellWidth,
                          (i * kLabelCellHeight + 2.0f) / atlasH,
                          (kLabelCellWidth - 4.0f) / kLabelCellWidth,
                          (kLabelCellHeight - 4.0f) / atlasH };
    }

    return objects;
}