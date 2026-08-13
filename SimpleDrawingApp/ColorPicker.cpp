#include "ColorPicker.h"
#include <commdlg.h>

#pragma comment(lib, "Comdlg32.lib")

COLORREF ColorPicker::PickColor(HWND owner, COLORREF initialColor) {
    CHOOSECOLORA cc = { sizeof(cc) };
    static COLORREF customColors[16] = { 0 };

    cc.hwndOwner = owner;
    cc.lpCustColors = customColors;
    cc.rgbResult = initialColor;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;

    if (ChooseColorA(&cc)) {
        return cc.rgbResult;
    }

    return initialColor;
}
