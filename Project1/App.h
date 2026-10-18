#pragma once
// =============================================================================
// App.h - 창 + 메시지 처리 + 메인 루프
// =============================================================================
#include <windows.h>
#include <vector>
#include <string>
#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"

class App
{
public:
    void Init(HINSTANCE hInst, UINT width, UINT height);
    int  Run();
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void PaintAt(int x, int y);
    int  PickObject(int x, int y);          // ★ [7-2단계] 클릭 가능한 물체 인덱스 (-1 = 없음)
    bool TryClickObject(int x, int y);      // ★ [7-2단계] 도구 선택 또는 버튼 누르기
    void SelectTool(ToolKind kind, int index);
    void ChangeBrushSize(float delta);
    void UpdateCursor();                    // ★ [7-2단계] hover 하이라이트 포함
    void UpdateTitle();

    // ★ [7-2단계] 버튼
    void PressButton(int buttonId);         // 눌림 애니메이션 시작
    void UpdateButtonAnimation(float dt);   // 애니메이션 진행, 끝나면 기능 실행
    void ExecuteButton(int buttonId);       // 버튼 → 파일 기능 연결

    HMENU CreateMenuBar();
    void  OnNew();
    void  OnOpen();
    bool  OnSave();
    bool  OnSaveAs();
    bool  SaveToPath(const std::wstring& path);
    bool  ConfirmDiscardChanges();
    void  CancelMouseActions();
    void  MarkDirty();

private:
    HWND m_hwnd = nullptr;
    bool m_ready = false;
    bool m_minimized = false;

    bool        m_drawing = false;
    Brush       m_brush;
    BrushCursor m_cursor;
    ToolKind    m_selKind = ToolKind::Chalk;
    int         m_selIndex = 0;

    std::wstring m_currentPath;
    bool         m_dirty = false;

    // ★ [7-2단계]
    int   m_hoverIndex = -1;     // 마우스가 올라가 있는 물체 (m_objects 인덱스)
    int   m_pressedButton = -1;  // 눌리는 중인 버튼 (ButtonId, -1 = 없음)
    float m_pressTimer = 0.0f;   // 눌림 애니메이션 남은 시간

    Renderer                 m_renderer;
    Camera                   m_camera;
    std::vector<SceneObject> m_objects;
};