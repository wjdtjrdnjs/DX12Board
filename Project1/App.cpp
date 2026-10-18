// =============================================================================
// App.cpp - 창, 입력, 메인 루프, 도구 선택, 파일 기능(메뉴 / 버튼 / 단축키)
// =============================================================================
#include "App.h"
#include "Picking.h"
#include "ImageIO.h"
#include <windowsx.h>    // GET_X_LPARAM
#include <commdlg.h>     // 열기/저장 대화상자
#include <cwchar>        // swprintf_s, wcsncpy_s, _wcsicmp
#include <cfloat>        // FLT_MAX

#pragma comment(lib, "comdlg32.lib")

using namespace DirectX;

// =============================================================================
// 이 파일 안에서만 쓰는 것들
// =============================================================================
namespace
{
    // 메뉴 항목 ID (WM_COMMAND 의 LOWORD(wParam) 로 들어옴)
    enum MenuId : UINT
    {
        kMenuNew = 1001,
        kMenuOpen,       // 1002
        kMenuSave,       // 1003
        kMenuSaveAs,     // 1004
        kMenuExit,       // 1005
    };

    // 열기/저장 대화상자. 파일을 고르면 true + outPath
    bool ShowFileDialog(HWND owner, bool save, const std::wstring& initialPath, std::wstring& outPath)
    {
        wchar_t buffer[MAX_PATH] = L"";
        if (!initialPath.empty())
            wcsncpy_s(buffer, initialPath.c_str(), _TRUNCATE); // 저장 시 현재 이름 미리 채움

        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = MAX_PATH;

        if (save)
        {
            ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0";
            ofn.lpstrDefExt = L"png";                          // 확장자 생략 시 자동 .png
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (!GetSaveFileNameW(&ofn)) return false;         // 취소
        }
        else
        {
            ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0"
                L"All Files (*.*)\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (!GetOpenFileNameW(&ofn)) return false;
        }

        outPath = buffer;
        return true;
    }

    // 확장자가 .png 인지 (대소문자 무시)
    bool IsPngPath(const std::wstring& path)
    {
        const size_t dot = path.find_last_of(L'.');
        return dot != std::wstring::npos && _wcsicmp(path.c_str() + dot, L".png") == 0;
    }
}

// =============================================================================
// Win32 콜백 → App::MsgProc 전달
// =============================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto app = reinterpret_cast<App*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (app) return app->MsgProc(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// =============================================================================
// 초기화
// =============================================================================
void App::Init(HINSTANCE hInst, UINT width, UINT height)
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DX12BlackboardClass";
    RegisterClassEx(&wc);

    // 메뉴가 있으니 세 번째 인자 TRUE (메뉴 높이만큼 창을 키워 클라이언트 영역 유지)
    RECT rc = { 0, 0, (LONG)width, (LONG)height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

    m_hwnd = CreateWindow(wc.lpszClassName, L"DX12 Blackboard",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, CreateMenuBar(), hInst, this);   // 메뉴 핸들 전달
    if (!m_hwnd) throw std::runtime_error("CreateWindow failed");

    m_renderer.Init(m_hwnd, width, height);
    m_objects = BuildBlackboardScene();

    SelectTool(ToolKind::Chalk, 0); // 시작 도구 = 흰 분필

    m_ready = true;
    ShowWindow(m_hwnd, SW_SHOW);
}

// 리소스 파일(.rc) 없이 코드로 메뉴 만들기
// '&' 뒤 글자 = Alt 단축 글자, '\t' 뒤 = 오른쪽 정렬 단축키 표시(표시만 함)
HMENU App::CreateMenuBar()
{
    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, kMenuNew, L"&New\tCtrl+N");
    AppendMenuW(fileMenu, MF_STRING, kMenuOpen, L"&Open...\tCtrl+O");
    AppendMenuW(fileMenu, MF_STRING, kMenuSave, L"&Save\tCtrl+S");
    AppendMenuW(fileMenu, MF_STRING, kMenuSaveAs, L"Save &As...\tCtrl+Shift+S");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, kMenuExit, L"E&xit\tEsc");

    HMENU menuBar = CreateMenu();
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)fileMenu, L"&File");
    return menuBar;
}

// =============================================================================
// 그리기 / 피킹 / 도구
// =============================================================================

// 마우스 위치 → 광선 → 칠판 UV → 선 이어 그리기
void App::PaintAt(int x, int y)
{
    const UINT w = m_renderer.Width();
    const UINT h = m_renderer.Height();
    if (w == 0 || h == 0) return;

    Board& board = m_renderer.GetBoard();
    Ray ray = ScreenPointToRay(x, y, w, h, m_camera);

    XMFLOAT2 uv;
    if (RayHitBoard(ray, uv))
    {
        board.StrokeTo(uv, m_brush);
        MarkDirty();
    }
    else
    {
        board.EndStroke(); // 칠판 밖으로 나가면 선 끊기
    }
}

// 광선이 맞는 클릭 가능한 물체 중 가장 가까운 것의 인덱스 (-1 = 없음)
int App::PickObject(int x, int y)
{
    const UINT w = m_renderer.Width();
    const UINT h = m_renderer.Height();
    if (w == 0 || h == 0) return -1;

    Ray ray = ScreenPointToRay(x, y, w, h, m_camera);

    float bestT = FLT_MAX;
    int   best = -1;

    for (int i = 0; i < (int)m_objects.size(); ++i)
    {
        const SceneObject& obj = m_objects[i];
        if (obj.tool == ToolKind::None) continue;

        // 화면에 보이는 위치(lift, push 포함) 기준 박스
        const XMFLOAT3 center = { obj.position.x, obj.position.y + obj.lift, obj.position.z + obj.push };
        const XMFLOAT3 half = { obj.scale.x * 0.5f, obj.scale.y * 0.5f, obj.scale.z * 0.5f };

        float t;
        if (RayHitAABB(ray, center, half, t) && t < bestT)
        {
            bestT = t;
            best = i;
        }
    }
    return best;
}

// 클릭: 버튼이면 누르기, 도구면 선택
bool App::TryClickObject(int x, int y)
{
    const int index = PickObject(x, y);
    if (index < 0) return false;

    const SceneObject& obj = m_objects[index];
    if (obj.tool == ToolKind::Button)
        PressButton(obj.toolIndex);
    else
        SelectTool(obj.tool, obj.toolIndex); // 값 복사로 넘기므로 안에서 m_objects 수정해도 안전
    return true;
}

// 도구 선택: 붓 설정 변경 + 선택된 물체만 들어올림
void App::SelectTool(ToolKind kind, int index)
{
    m_selKind = kind;
    m_selIndex = index;

    switch (kind)
    {
    case ToolKind::Chalk:   m_brush.color = kChalkColors[index];   m_brush.eraser = false; break;
    case ToolKind::Palette: m_brush.color = kPaletteColors[index]; m_brush.eraser = false; break;
    case ToolKind::Eraser:  m_brush.eraser = true; break;
    default: break;
    }

    for (SceneObject& obj : m_objects)
        obj.lift = (obj.tool == kind && obj.toolIndex == index) ? 0.04f : 0.0f;

    UpdateTitle();
}

// 붓 크기 변경 (2 ~ 40)
void App::ChangeBrushSize(float delta)
{
    float r = m_brush.radius + delta;
    if (r < 2.0f)  r = 2.0f;
    if (r > 40.0f) r = 40.0f;
    m_brush.radius = r;
    UpdateTitle();
}

// 매 프레임: hover 물체 → 하이라이트, 물체 위가 아니면 칠판 커서 원
void App::UpdateCursor()
{
    m_cursor.visible = false;
    m_hoverIndex = -1;

    POINT p = {};
    bool inside = false;
    if (GetForegroundWindow() == m_hwnd)
    {
        GetCursorPos(&p);
        ScreenToClient(m_hwnd, &p);
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        inside = (PtInRect(&rc, p) != FALSE);
    }

    if (inside)
    {
        // 그리는 중엔 물체 hover 안 함
        if (!m_drawing) m_hoverIndex = PickObject(p.x, p.y);

        // 물체 위가 아니면 칠판 커서 원
        if (m_hoverIndex < 0)
        {
            Ray ray = ScreenPointToRay(p.x, p.y, m_renderer.Width(), m_renderer.Height(), m_camera);
            XMFLOAT2 uv;
            if (RayHitBoard(ray, uv))
            {
                m_cursor.visible = true;
                m_cursor.uv = uv;
                m_cursor.radiusPx = m_brush.EffectiveRadius();
                m_cursor.color = m_brush.eraser ? XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f) : m_brush.color;
            }
        }
    }

    // hover 물체와 종류·번호가 같은 것 전부 하이라이트 (지우개 박스 2개 같이)
    ToolKind hoverKind = ToolKind::None;
    int      hoverIdx = -1;
    if (m_hoverIndex >= 0)
    {
        hoverKind = m_objects[m_hoverIndex].tool;
        hoverIdx = m_objects[m_hoverIndex].toolIndex;
    }
    for (SceneObject& obj : m_objects)
    {
        const bool hovered = (hoverKind != ToolKind::None &&
            obj.tool == hoverKind && obj.toolIndex == hoverIdx);
        obj.highlight = hovered ? 1.0f : 0.0f;
    }
}

// =============================================================================
// 칠판 옆 버튼
// =============================================================================

// 바로 실행하지 않고 0.12초 눌림 애니메이션 후 실행
void App::PressButton(int buttonId)
{
    if (m_pressedButton >= 0) return; // 이미 눌리는 중이면 무시
    m_pressedButton = buttonId;
    m_pressTimer = 0.12f;
}

void App::UpdateButtonAnimation(float dt)
{
    if (m_pressedButton < 0) return;

    m_pressTimer -= dt;
    const bool pressing = (m_pressTimer > 0.0f);

    for (SceneObject& obj : m_objects)
    {
        if (obj.tool != ToolKind::Button) continue;
        obj.push = (pressing && obj.toolIndex == m_pressedButton) ? 0.015f : 0.0f;
    }

    if (!pressing)
    {
        const int id = m_pressedButton;
        m_pressedButton = -1; // 실행 전에 먼저 해제
        ExecuteButton(id);
    }
}

// 버튼 번호 → 메뉴/단축키와 같은 함수
void App::ExecuteButton(int buttonId)
{
    switch ((ButtonId)buttonId)
    {
    case ButtonId::New:    OnNew();    break;
    case ButtonId::Open:   OnOpen();   break;
    case ButtonId::Save:   OnSave();   break;
    case ButtonId::SaveAs: OnSaveAs(); break;
    }
}

// =============================================================================
// 제목 표시줄: "파일이름* - DX12 Blackboard | 도구 | 크기"
// =============================================================================
void App::UpdateTitle()
{
    static const wchar_t* kChalkNames[kChalkCount] = { L"White", L"Yellow", L"Red", L"Blue" };
    static const wchar_t* kPaletteNames[kPaletteCount] =
    { L"Orange", L"Green", L"Purple", L"Pink", L"Cyan", L"Brown", L"Gray", L"Black" };

    const wchar_t* toolName = L"";
    switch (m_selKind)
    {
    case ToolKind::Chalk:   toolName = kChalkNames[m_selIndex];   break;
    case ToolKind::Palette: toolName = kPaletteNames[m_selIndex]; break;
    case ToolKind::Eraser:  toolName = L"Eraser";                 break;
    default: break;
    }

    // 경로에서 파일 이름만 (없으면 npos + 1 = 0 → 전체)
    std::wstring fileName = L"Untitled";
    if (!m_currentPath.empty())
        fileName = m_currentPath.substr(m_currentPath.find_last_of(L"\\/") + 1);

    wchar_t title[512];
    swprintf_s(title, L"%s%s - DX12 Blackboard  |  %s  |  Size %.0f",
        fileName.c_str(), m_dirty ? L"*" : L"", toolName, m_brush.radius);
    SetWindowText(m_hwnd, title);
}

// =============================================================================
// 파일 기능 (메뉴, 버튼, 단축키가 모두 여기로 모임)
// =============================================================================
void App::MarkDirty()
{
    if (m_dirty) return;
    m_dirty = true;
    UpdateTitle();
}

// 대화상자 뜨기 전 드래그 상태 정리
void App::CancelMouseActions()
{
    m_drawing = false;
    m_camera.EndRotate();
    m_renderer.GetBoard().EndStroke();
    if (GetCapture() == m_hwnd) ReleaseCapture();
}

// 수정 사항이 있으면 저장할지 물어봄. 취소하면 false (하려던 작업 중단)
bool App::ConfirmDiscardChanges()
{
    if (!m_dirty) return true;

    CancelMouseActions();
    const int result = MessageBoxW(m_hwnd, L"Save changes to the board?", L"DX12 Blackboard",
        MB_YESNOCANCEL | MB_ICONQUESTION);
    if (result == IDCANCEL) return false;
    if (result == IDYES)    return OnSave(); // 저장 대화상자에서 취소하면 false
    return true;                             // IDNO
}

void App::OnNew()
{
    if (!ConfirmDiscardChanges()) return;

    m_renderer.GetBoard().RequestClear();
    m_currentPath.clear();
    m_dirty = false;
    UpdateTitle();
}

void App::OnOpen()
{
    if (!ConfirmDiscardChanges()) return;
    CancelMouseActions();

    std::wstring path;
    if (!ShowFileDialog(m_hwnd, false, L"", path)) return;

    try
    {
        std::vector<uint8_t> pixels;
        LoadImageFitPremulRGBA(path.c_str(), kBoardTexWidth, kBoardTexHeight, pixels);
        m_renderer.WriteBoardPixels(pixels);

        // PNG 면 그 파일을 현재 문서로, 다른 형식이면 이름 없는 새 문서
        m_currentPath = IsPngPath(path) ? path : L"";
        m_dirty = false;
        UpdateTitle();
    }
    catch (const std::exception& e)
    {
        MessageBoxA(m_hwnd, e.what(), "Open failed", MB_OK | MB_ICONERROR);
    }
}

bool App::OnSave()
{
    if (m_currentPath.empty()) return OnSaveAs(); // 처음 저장이면 이름 묻기
    return SaveToPath(m_currentPath);
}

bool App::OnSaveAs()
{
    CancelMouseActions();

    std::wstring path;
    if (!ShowFileDialog(m_hwnd, true, m_currentPath, path)) return false;
    return SaveToPath(path);
}

// 칠판 텍스처(프리멀티플라이드) + 칠판색 배경 → 불투명 BGRA → PNG
bool App::SaveToPath(const std::wstring& path)
{
    try
    {
        std::vector<uint8_t> premul;
        m_renderer.ReadBoardPixels(premul);

        const size_t pixelCount = (size_t)kBoardTexWidth * kBoardTexHeight;
        std::vector<uint8_t> bgra(pixelCount * 4);

        // 0~1 float → 0~255 바이트 (반올림 + 범위 제한)
        auto toByte = [](float v) -> uint8_t
            {
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                return (uint8_t)(v * 255.0f + 0.5f);
            };

        const XMFLOAT4& bg = kBoardColor;
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const uint8_t* s = &premul[i * 4]; // R, G, B, A (프리멀티플라이드)
            uint8_t* d = &bgra[i * 4];   // B, G, R, A

            // 칠판 셰이더와 같은 합성: 바탕 * (1 - a) + 분필rgb
            const float a = s[3] / 255.0f;
            const float r = bg.x * (1.0f - a) + s[0] / 255.0f;
            const float g = bg.y * (1.0f - a) + s[1] / 255.0f;
            const float b = bg.z * (1.0f - a) + s[2] / 255.0f;

            d[0] = toByte(b);
            d[1] = toByte(g);
            d[2] = toByte(r);
            d[3] = 255;
        }

        SavePngBGRA(path.c_str(), kBoardTexWidth, kBoardTexHeight, bgra);

        m_currentPath = path;
        m_dirty = false;
        UpdateTitle();
        return true;
    }
    catch (const std::exception& e)
    {
        MessageBoxA(m_hwnd, e.what(), "Save failed", MB_OK | MB_ICONERROR);
        return false;
    }
}

// =============================================================================
// 메인 루프
// =============================================================================
int App::Run()
{
    LARGE_INTEGER freq, prev;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else if (!m_minimized)
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            float dt = (float)(now.QuadPart - prev.QuadPart) / (float)freq.QuadPart;
            prev = now;
            if (dt > 0.1f) dt = 0.1f;

            // Ctrl 누르고 있으면 카메라 이동 안 함 (Ctrl+S 때 S로 뒤로 가는 것 방지)
            const bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            m_camera.Update(dt, GetForegroundWindow() == m_hwnd && !ctrlDown);

            UpdateButtonAnimation(dt);                          // 버튼 눌림 (끝나면 기능 실행)
            UpdateCursor();                                     // hover + 커서 원
            m_renderer.Render(m_camera, m_objects, m_cursor);
        }
    }
    return (int)msg.wParam;
}

// =============================================================================
// 메시지 처리
// =============================================================================
LRESULT App::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        // 클릭 가능한 물체 위에선 손가락 커서
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && m_hoverIndex >= 0)
        {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        break; // 그 외엔 DefWindowProc 이 기본 커서

    case WM_SIZE:
        m_minimized = (wParam == SIZE_MINIMIZED);
        if (m_ready && !m_minimized) m_renderer.Resize(LOWORD(lParam), HIWORD(lParam));
        return 0;

        // 메뉴 클릭
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case kMenuNew:    OnNew();    break;
        case kMenuOpen:   OnOpen();   break;
        case kMenuSave:   OnSave();   break;
        case kMenuSaveAs: OnSaveAs(); break;
        case kMenuExit:   SendMessage(hwnd, WM_CLOSE, 0, 0); break;
        }
        return 0;

        // 창 닫기(X 버튼, Alt+F4, Exit, Esc) → 저장 확인 후 파괴
    case WM_CLOSE:
        if (ConfirmDiscardChanges()) DestroyWindow(hwnd);
        return 0;

    case WM_LBUTTONDOWN:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        // 도구/버튼을 클릭했으면 그리기는 시작 안 함
        if (TryClickObject(x, y)) return 0;

        m_drawing = true;
        if (GetCapture() != hwnd) SetCapture(hwnd);
        PaintAt(x, y);
        return 0;
    }

    case WM_LBUTTONUP:
        m_drawing = false;
        m_renderer.GetBoard().EndStroke();
        if (!(wParam & MK_RBUTTON)) ReleaseCapture();
        return 0;

    case WM_RBUTTONDOWN:
        m_camera.BeginRotate(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (GetCapture() != hwnd) SetCapture(hwnd);
        return 0;

    case WM_RBUTTONUP:
        m_camera.EndRotate();
        if (!(wParam & MK_LBUTTON)) ReleaseCapture();
        return 0;

    case WM_CAPTURECHANGED:
        if ((HWND)lParam != hwnd)
        {
            m_camera.EndRotate();
            m_drawing = false;
            m_renderer.GetBoard().EndStroke();
        }
        return 0;

    case WM_MOUSEMOVE:
        m_camera.OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (m_drawing) PaintAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_MOUSEWHEEL:
        ChangeBrushSize(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 2.0f : -2.0f);
        return 0;

    case WM_KEYDOWN:
    {
        // Ctrl 조합 단축키 먼저
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl)
        {
            switch (wParam)
            {
            case 'N': OnNew();  break;
            case 'O': OnOpen(); break;
            case 'S': if (shift) OnSaveAs(); else OnSave(); break;
            }
            return 0;
        }

        switch (wParam)
        {
        case VK_ESCAPE:
            SendMessage(hwnd, WM_CLOSE, 0, 0); // 바로 종료 대신 저장 확인 절차
            break;

        case '1': case '2': case '3': case '4':
            SelectTool(ToolKind::Chalk, (int)(wParam - '1'));
            break;

        case '5':
            SelectTool(ToolKind::Eraser, 0);
            break;

        case VK_OEM_4: ChangeBrushSize(-2.0f); break; // '['
        case VK_OEM_6: ChangeBrushSize(2.0f); break; // ']'

        case VK_DELETE:
            m_renderer.GetBoard().RequestClear();
            MarkDirty();
            break;
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}