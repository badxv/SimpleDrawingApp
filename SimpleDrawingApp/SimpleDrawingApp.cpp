#include <windows.h>
#include "framework.h"
#include "SimpleDrawingApp.h"
#include "AppFeatureFlags.h"
#include "BrushEngine.h"
#include "PenInput.h"
#include "AppDocument.h"
#include "AppViewport.h"
#include "UiChromeRender.h"
#include "CaptionBar.h"
#include "UiPaletteFloat.h"
#include "UiShapeFlyout.h"
#include "EventBus.h"
#include "AtelierFonts.h"
#include "AtelierControls.h"
#include "AtelierArtwork.h"
#include "AtelierPalette.h"
#include "Resource.h"

#include <gdiplus.h>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Dwmapi.lib")

using namespace Gdiplus;

namespace {
ULONG_PTR gdiplusToken = 0;

void ShutdownBeforeGdiplus() {
    ShutdownBrushEngine();
    AtelierArtwork_Shutdown();
    AtelierFonts_Shutdown();
    GdiplusShutdown(gdiplusToken);
}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    LoadFeatureFlags();
    InitPenInput();
    InitBrushEngine();
    LoadBrushSettings();
    LoadSessionState();
    AtelierFonts_Init();
    AtelierArtwork_Init();
    AtelierControls_SetTheme(&gTheme);
    if (!AtelierControls_Register()) {
        ShutdownBeforeGdiplus();
        return 0;
    }
    AtelierPalette_SetTheme(&gTheme);
    if (!AtelierPalette_Register()) {
        ShutdownBeforeGdiplus();
        return 0;
    }

    if (!RegisterViewportClass(hInstance)) {
        ShutdownBeforeGdiplus();
        return 0;
    }
    if (!RegisterBrandClass(hInstance)) {
        ShutdownBeforeGdiplus();
        return 0;
    }
    if (!RegisterCaptionBtnClass(hInstance)) {
        ShutdownBeforeGdiplus();
        return 0;
    }
    if (!RegisterShapeFlyoutClass(hInstance)) {
        ShutdownBeforeGdiplus();
        return 0;
    }
    if (!RegisterPaletteFloatClass(hInstance)) {
        ShutdownBeforeGdiplus();
        return 0;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kAppMainWindowClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // painted manually
    wc.lpszMenuName = MAKEINTRESOURCEW(IDC_SIMPLEDRAWINGAPP);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_SIMPLEDRAWINGAPP));
    wc.style = CS_DBLCLKS;

    if (!RegisterClassW(&wc)) {
        ShutdownBeforeGdiplus();
        return 0;
    }

    HWND hwnd = CreateWindowExW(0, kAppMainWindowClassName, L"Simple Drawing App",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        ShutdownBeforeGdiplus();
        return 0;
    }

    // Move classic menubar into the top chrome (Firefox-like); keep HMENU for popups.
    gAppMenu = GetMenu(hwnd);
    SetMenu(hwnd, NULL);
    DrawMenuBar(hwnd);
    SyncFeatureFlagMenuItems();

    gAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDC_SIMPLEDRAWINGAPP));

    InitAppEventHandlers();

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        // Tab toggles the tools rail (don't let it cycle child focus).
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && !IsTypingInEdit()
            && (GetKeyState(VK_CONTROL) & 0x8000) == 0
            && (GetKeyState(VK_MENU) & 0x8000) == 0) {
            SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IDC_TOGGLE_RAIL, 0), 0);
            continue;
        }
        const bool keyMsg = (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN);
        const bool typing = keyMsg && IsTypingInEdit();
        const bool ctrlOrAlt = (GetKeyState(VK_CONTROL) & 0x8000) != 0
            || (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool tryAccel = !typing || ctrlOrAlt;
        if (tryAccel && TranslateAcceleratorW(hwnd, gAccel, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    SaveFeatureFlags();
    ShutdownBeforeGdiplus();
    return static_cast<int>(msg.wParam);
}
