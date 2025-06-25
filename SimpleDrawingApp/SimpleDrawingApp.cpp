#include <windows.h>
#include "framework.h"
#include "SimpleDrawingApp.h"
#include "FileManager.h"
#include "ColorPicker.h"

#include <commctrl.h>
#include <objidl.h>
#include <commdlg.h> 
#include <gdiplus.h>
#include <shobjidl.h>
#include <string>
#include <cstdio>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;

const char CLASS_NAME[] = "SimpleDrawingAppWindowClass";

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

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES };
        InitCommonControlsEx(&icex);

        hwndSlider = CreateWindowEx(0, TRACKBAR_CLASS, NULL,
            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
            10, 10, 200, 30,
            hwnd, NULL, GetModuleHandle(NULL), NULL);

        SendMessage(hwndSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 50));
        SendMessage(hwndSlider, TBM_SETPOS, TRUE, penWidth);

        hwndPenWidthBox = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
            220, 10, 50, 25,
            hwnd, NULL, GetModuleHandle(NULL), NULL);

        hwndSaveButton = CreateWindow("BUTTON", "Save",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            300, 10, 80, 25,
            hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);

        hwndLoadButton = CreateWindow("BUTTON", "Load",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            390, 10, 80, 25,
            hwnd, (HMENU)2, GetModuleHandle(NULL), NULL);

        hwndColorButton = CreateWindow("BUTTON", "Color",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            480, 10, 80, 25,
            hwnd, (HMENU)3, GetModuleHandle(NULL), NULL);

        UpdatePenWidthDisplay();
        break;
    }
    case WM_HSCROLL: {
        if ((HWND)lParam == hwndSlider) {
            penWidth = static_cast<int>(SendMessage(hwndSlider, TBM_GETPOS, 0, 0));
            UpdatePenWidthDisplay();
        }
        break;
    }
    case WM_COMMAND: {
        if ((HWND)lParam == hwndPenWidthBox && HIWORD(wParam) == EN_CHANGE) {
            char buf[16];
            GetWindowTextA(hwndPenWidthBox, buf, sizeof(buf));
            int val = atoi(buf);
            if (val >= 1 && val <= 50) {
                penWidth = val;
                SendMessage(hwndSlider, TBM_SETPOS, TRUE, val);
            }
        }
        else if (LOWORD(wParam) == 1) {
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
                SaveCanvasToFile(canvasBitmap, filePath);
            }
        }
        else if (LOWORD(wParam) == 2) {
            char filePath[MAX_PATH] = "";
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.bmp\0";
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameA(&ofn)) {
                LoadImageFromFile(filePath, canvasBitmap, canvasGraphics);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        else if (LOWORD(wParam) == 3) {
            COLORREF newColor = ColorPicker::PickColor(hwnd, penColor);
            if (newColor != penColor) {
                penColor = newColor;
            }
        }
        break;
    }
    case WM_LBUTTONDOWN:
        isDrawing = true;
        lastPoint.x = LOWORD(lParam);
        lastPoint.y = HIWORD(lParam);
        break;
    case WM_MOUSEMOVE:
        if (isDrawing && canvasGraphics) {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);

            Pen pen(Color(255, GetRValue(penColor), GetGValue(penColor), GetBValue(penColor)), (REAL)penWidth);
            canvasGraphics->DrawLine(&pen, lastPoint.x, lastPoint.y, x, y);

            lastPoint.x = x;
            lastPoint.y = y;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_LBUTTONUP:
        isDrawing = false;
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        if (!canvasBitmap) {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            canvasBitmap = new Bitmap(clientRect.right, clientRect.bottom, PixelFormat32bppARGB);
            canvasGraphics = Graphics::FromImage(canvasBitmap);
            canvasGraphics->Clear(Color(255, 255, 255, 255));
        }

        Graphics g(hdc);
        g.DrawImage(canvasBitmap, 0, 0);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DESTROY:
        delete canvasGraphics;
        delete canvasBitmap;
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, "Simple Drawing App",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}
