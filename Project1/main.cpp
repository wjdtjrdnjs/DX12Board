#include "App.h"
#include <objbase.h> // CoInitializeEx

// COM 초기화/해제를 생성자/소멸자에 묶은 RAII 객체
struct ComInitializer
{
    HRESULT hr;
    ComInitializer() { hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); }
    ~ComInitializer() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    ComInitializer com; // try 보다 먼저 생성 → App 보다 나중에 소멸 (App 이 COM 을 다 쓴 뒤 해제)

    try
    {
        App app;
        app.Init(hInst, 1280, 720);
        return app.Run();
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);
        return -1;
    }
}