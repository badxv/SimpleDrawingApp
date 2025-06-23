#include "ColorPicker.h"
#include <commdlg.h>  // ChooseColor API

COLORREF ColorPicker::PickColor(HWND owner, COLORREF initialColor) {
    CHOOSECOLOR cc = { sizeof(cc) };
    static COLORREF customColors[16] = { 0 };

    cc.hwndOwner = owner;
    cc.lpCustColors = customColors;
    cc.rgbResult = initialColor;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;

    if (ChooseColor(&cc)) {
        return cc.rgbResult;
    }

    return initialColor; 
}
