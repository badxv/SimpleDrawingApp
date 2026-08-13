#include <windows.h>
#include <windowsx.h>
#include "framework.h"
#include "SimpleDrawingApp.h"
#include "FileManager.h"
#include "ColorPicker.h"
#include "Resource.h"

#include <commctrl.h>
#include <objidl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <shobjidl.h>
#include <string>
#include <cstdio>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;

const char CLASS_NAME[] = "SimpleDrawingAppWindowClass";
constexpr int TOOLBAR_HEIGHT = 50;

HWND hwndSlider;
HWND hwndPenWidthBox;
HWND hwndSaveButton, hwndLoadButton;
HWND hwndColorButton;

COLORREF penColor = RGB(0, 0, 0);
int penWidth = 5;
POINT lastPoint;
bool isDrawing = false;

Bitmap* canvasBitmap = nullptr;
Graphics* canvasGraphics = nullptr;

ULONG_PTR gdiplusToken;

void UpdatePenWidthDisplay() {
    char buf[16];
    sprintf_s(buf, "%d", penWidth);
    SetWindowTextA(hwndPenWidthBox, buf);
}

static void GetCanvasSize(HWND hwnd, int& width, int& height) {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    width = clientRect.right - clientRect.left;
    height = clientRect.bottom - clientRect.top - TOOLBAR_HEIGHT;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
}

static void EnsureCanvas(HWND hwnd) {
    if (canvasBitmap) return;

    int width = 0, height = 0;
    GetCanvasSize(hwnd, width, height);

    canvasBitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    canvasGraphics = Graphics::FromImage(canvasBitmap);
    canvasGraphics->Clear(Color(255, 255, 255, 255));
    canvasGraphics->SetSmoothingMode(SmoothingModeAntiAlias);
}

static void ResizeCanvas(HWND hwnd) {
    int width = 0, height = 0;
    GetCanvasSize(hwnd, width, height);

    if (canvasBitmap &&
        canvasBitmap->GetWidth() == static_cast<UINT>(width) &&
        canvasBitmap->GetHeight() == static_cast<UINT>(height)) {
        return;
    }

    Bitmap* newBitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics* newGraphics = Graphics::FromImage(newBitmap);
    newGraphics->Clear(Color(255, 255, 255, 255));
    newGraphics->SetSmoothingMode(SmoothingModeAntiAlias);

    if (canvasBitmap) {
        newGraphics->DrawImage(canvasBitmap, 0, 0);
    }

    delete canvasGraphics;
    delete canvasBitmap;
    canvasBitmap = newBitmap;
    canvasGraphics = newGraphics;
}

static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static bool ClientToCanvas(int clientX, int clientY, int& canvasX, int& canvasY) {
    if (clientY < TOOLBAR_HEIGHT) return false;
    canvasX = clientX;
    canvasY = clientY - TOOLBAR_HEIGHT;
    return true;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES };
        InitCommonControlsEx(&icex);

        hwndSlider = CreateWindowExA(0, TRACKBAR_CLASSA, NULL,
            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
            10, 10, 200, 30,
            hwnd, NULL, GetModuleHandle(NULL), NULL);

        SendMessage(hwndSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 50));
        SendMessage(hwndSlider, TBM_SETPOS, TRUE, penWidth);

        hwndPenWidthBox = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
            220, 10, 50, 25,
            hwnd, NULL, GetModuleHandle(NULL), NULL);

        hwndSaveButton = CreateWindowA("BUTTON", "Save",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            300, 10, 80, 25,
            hwnd, (HMENU)(INT_PTR)IDC_SAVE_BUTTON, GetModuleHandle(NULL), NULL);

        hwndLoadButton = CreateWindowA("BUTTON", "Load",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            390, 10, 80, 25,
            hwnd, (HMENU)(INT_PTR)IDC_LOAD_BUTTON, GetModuleHandle(NULL), NULL);

        hwndColorButton = CreateWindowA("BUTTON", "Color",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            480, 10, 80, 25,
            hwnd, (HMENU)(INT_PTR)IDC_COLOR_BUTTON, GetModuleHandle(NULL), NULL);

        UpdatePenWidthDisplay();
        break;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            ResizeCanvas(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_HSCROLL: {
        if ((HWND)lParam == hwndSlider) {
            penWidth = static_cast<int>(SendMessage(hwndSlider, TBM_GETPOS, 0, 0));
            UpdatePenWidthDisplay();
        }
        break;
    }
    case WM_COMMAND: {
        const int cmdId = LOWORD(wParam);
        const int notifyCode = HIWORD(wParam);

        if ((HWND)lParam == hwndPenWidthBox && notifyCode == EN_CHANGE) {
            char buf[16];
            GetWindowTextA(hwndPenWidthBox, buf, sizeof(buf));
            int val = atoi(buf);
            if (val >= 1 && val <= 50) {
                penWidth = val;
                SendMessage(hwndSlider, TBM_SETPOS, TRUE, val);
            }
        }
        else if (cmdId == IDC_SAVE_BUTTON) {
            EnsureCanvas(hwnd);
            char filePath[MAX_PATH] = "";
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "PNG Files\0*.png\0JPG Files\0*.jpg\0BMP Files\0*.bmp\0";
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            ofn.lpstrDefExt = "png";

            if (GetSaveFileNameA(&ofn)) {
                if (!SaveCanvasToFile(canvasBitmap, filePath)) {
                    MessageBoxA(hwnd, "Failed to save image.", "Error", MB_OK | MB_ICONERROR);
                }
            }
        }
        else if (cmdId == IDC_LOAD_BUTTON) {
            char filePath[MAX_PATH] = "";
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.bmp\0";
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameA(&ofn)) {
                if (LoadImageFromFile(filePath, canvasBitmap, canvasGraphics)) {
                    if (canvasGraphics) {
                        canvasGraphics->SetSmoothingMode(SmoothingModeAntiAlias);
                    }
                    ResizeCanvas(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);
                }
                else {
                    MessageBoxA(hwnd, "Failed to load image.", "Error", MB_OK | MB_ICONERROR);
                }
            }
        }
        else if (cmdId == IDC_COLOR_BUTTON) {
            COLORREF newColor = ColorPicker::PickColor(hwnd, penColor);
            penColor = newColor;
        }
        else if (cmdId == IDM_ABOUT) {
            DialogBoxA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_ABOUTBOX), hwnd, AboutDlgProc);
        }
        else if (cmdId == IDM_EXIT) {
            DestroyWindow(hwnd);
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        EnsureCanvas(hwnd);
        int canvasX = 0, canvasY = 0;
        if (ClientToCanvas(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), canvasX, canvasY)) {
            isDrawing = true;
            lastPoint.x = canvasX;
            lastPoint.y = canvasY;
            SetCapture(hwnd);
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (isDrawing && canvasGraphics) {
            int canvasX = 0, canvasY = 0;
            if (ClientToCanvas(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), canvasX, canvasY)) {
                Pen pen(Color(255, GetRValue(penColor), GetGValue(penColor), GetBValue(penColor)), (REAL)penWidth);
                pen.SetStartCap(LineCapRound);
                pen.SetEndCap(LineCapRound);
                pen.SetLineJoin(LineJoinRound);
                canvasGraphics->DrawLine(&pen, lastPoint.x, lastPoint.y, canvasX, canvasY);

                lastPoint.x = canvasX;
                lastPoint.y = canvasY;

                RECT invalidate;
                GetClientRect(hwnd, &invalidate);
                invalidate.top = TOOLBAR_HEIGHT;
                InvalidateRect(hwnd, &invalidate, FALSE);
            }
        }
        break;
    case WM_LBUTTONUP:
        if (isDrawing) {
            isDrawing = false;
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
        }
        break;
    case WM_CAPTURECHANGED:
        isDrawing = false;
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        EnsureCanvas(hwnd);

        Graphics g(hdc);
        g.DrawImage(canvasBitmap, 0, TOOLBAR_HEIGHT);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DESTROY:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        delete canvasGraphics;
        delete canvasBitmap;
        canvasGraphics = nullptr;
        canvasBitmap = nullptr;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCEA(IDC_SIMPLEDRAWINGAPP);
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_SIMPLEDRAWINGAPP));

    if (!RegisterClassA(&wc)) {
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    HWND hwnd = CreateWindowExA(0, CLASS_NAME, "Simple Drawing App",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
