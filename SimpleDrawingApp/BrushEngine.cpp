#include "BrushEngine.h"
#include "AbrImport.h"
#include "AppMetrics.h"
#include "DrawingTools.h"
#include "Resource.h"
#include "AppState.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace Gdiplus;

namespace {

constexpr int kTipSize = 64;
constexpr int kMaxCustomBrushes = 8;
constexpr int kMaxTotalBrushes = 24;

std::vector<BrushPreset> gPresets;
int gActiveBrush = 0;
float gStampCarry = 0.0f;

std::wstring PathToWide(const char* path) {
    if (!path || !path[0]) return std::wstring();
    int len = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, path, -1, &out[0], len);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

BrushPreset* ActivePresetMutable() {
    if (gActiveBrush < 0 || gActiveBrush >= static_cast<int>(gPresets.size())) return nullptr;
    return &gPresets[static_cast<size_t>(gActiveBrush)];
}

const BrushPreset* ActivePreset() {
    return ActivePresetMutable();
}

void GetFeaturesIniPath(char* path, size_t pathChars) {
    if (!path || pathChars == 0) return;
    path[0] = '\0';
    DWORD len = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(pathChars));
    if (len == 0 || len >= pathChars) return;
    for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
        if (path[i] == '\\' || path[i] == '/') {
            path[i + 1] = '\0';
            break;
        }
    }
    if (strlen(path) + 13 > pathChars) {
        path[0] = '\0';
        return;
    }
    strcat_s(path, pathChars, "features.ini");
}

void SetTipPixel(Bitmap* bmp, int x, int y, BYTE alpha) {
    if (!bmp || x < 0 || y < 0 || x >= static_cast<int>(bmp->GetWidth()) || y >= static_cast<int>(bmp->GetHeight())) {
        return;
    }
    bmp->SetPixel(x, y, Color(alpha, 255, 255, 255));
}

float RadialAlpha(int x, int y, int cx, int cy, int rx, int ry, float hardness) {
    const float dx = static_cast<float>(x - cx) / static_cast<float>(MaxInt(1, rx));
    const float dy = static_cast<float>(y - cy) / static_cast<float>(MaxInt(1, ry));
    const float d = sqrtf(dx * dx + dy * dy);
    if (d >= 1.0f) return 0.0f;
    const float h = hardness;
    if (d <= h) return 1.0f;
    return 1.0f - (d - h) / MaxFloat(0.001f, 1.0f - h);
}

Bitmap* CreateTipFromAlpha(const float* alpha, int size) {
    Bitmap* bmp = new Bitmap(size, size, PixelFormat32bppARGB);
    if (!bmp || bmp->GetLastStatus() != Ok) {
        delete bmp;
        return nullptr;
    }
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float a = alpha[y * size + x];
            const BYTE ba = static_cast<BYTE>(MaxInt(0, (a * 255.0f + 0.5f) > 255.0f ? 255 : static_cast<int>(a * 255.0f + 0.5f)));
            SetTipPixel(bmp, x, y, ba);
        }
    }
    return bmp;
}

Bitmap* MakeRoundTip(float hardness, float rxScale = 1.0f, float ryScale = 1.0f) {
    float alpha[kTipSize * kTipSize];
    const int cx = kTipSize / 2;
    const int cy = kTipSize / 2;
    const int rx = static_cast<int>((kTipSize / 2 - 1) * rxScale);
    const int ry = static_cast<int>((kTipSize / 2 - 1) * ryScale);
    for (int y = 0; y < kTipSize; ++y) {
        for (int x = 0; x < kTipSize; ++x) {
            alpha[y * kTipSize + x] = RadialAlpha(x, y, cx, cy, rx, ry, hardness);
        }
    }
    return CreateTipFromAlpha(alpha, kTipSize);
}

Bitmap* MakeCharcoalTip() {
    float alpha[kTipSize * kTipSize];
    const int cx = kTipSize / 2;
    const int cy = kTipSize / 2;
    for (int y = 0; y < kTipSize; ++y) {
        for (int x = 0; x < kTipSize; ++x) {
            float base = RadialAlpha(x, y, cx, cy, kTipSize / 2 - 2, kTipSize / 2 - 2, 0.35f);
            const unsigned noise = (static_cast<unsigned>(x) * 73856093u) ^ (static_cast<unsigned>(y) * 19349663u);
            const float grain = 0.65f + 0.35f * (static_cast<float>(noise & 255) / 255.0f);
            alpha[y * kTipSize + x] = base * grain;
        }
    }
    return CreateTipFromAlpha(alpha, kTipSize);
}

Bitmap* MakeConceptTip() {
    float alpha[kTipSize * kTipSize];
    const int cx = kTipSize / 2;
    const int cy = kTipSize / 2;
    for (int y = 0; y < kTipSize; ++y) {
        for (int x = 0; x < kTipSize; ++x) {
            float base = RadialAlpha(x, y, cx, cy, kTipSize / 2 - 1, kTipSize / 2 - 1, 0.55f);
            const unsigned n = ((static_cast<unsigned>(x + y * 3)) * 2654435761u) & 255u;
            const float tex = 0.82f + 0.18f * (static_cast<float>(n) / 255.0f);
            alpha[y * kTipSize + x] = base * tex;
        }
    }
    return CreateTipFromAlpha(alpha, kTipSize);
}

Bitmap* MakePencilTip() {
    float alpha[kTipSize * kTipSize];
    const int cx = kTipSize / 2;
    const int cy = kTipSize / 2;
    for (int y = 0; y < kTipSize; ++y) {
        for (int x = 0; x < kTipSize; ++x) {
            float base = RadialAlpha(x, y, cx, cy, kTipSize / 4, kTipSize / 4, 0.75f);
            const unsigned n = (static_cast<unsigned>(x) * 92837111u) ^ (static_cast<unsigned>(y) * 689287499u);
            const float grain = 0.55f + 0.45f * (static_cast<float>(n & 255) / 255.0f);
            alpha[y * kTipSize + x] = base * grain;
        }
    }
    return CreateTipFromAlpha(alpha, kTipSize);
}

void AddBuiltinPreset(const char* name, Bitmap* tip, float spacing, int defaultSize) {
    if (!tip) return;
    BrushPreset preset;
    preset.name = name;
    preset.tip.reset(tip);
    preset.spacing = spacing;
    preset.defaultSize = defaultSize;
    preset.builtin = true;
    gPresets.push_back(std::move(preset));
}

Bitmap* NormalizeImportedTip(Bitmap* source) {
    if (!source) return nullptr;
    const int w = static_cast<int>(source->GetWidth());
    const int h = static_cast<int>(source->GetHeight());
    if (w < 1 || h < 1) return nullptr;

    Bitmap* out = new Bitmap(kTipSize, kTipSize, PixelFormat32bppARGB);
    if (!out || out->GetLastStatus() != Ok) {
        delete out;
        return nullptr;
    }
    Graphics g(out);
    g.Clear(Color(0, 0, 0, 0));
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(source, Rect(0, 0, kTipSize, kTipSize));

    BitmapData data = {};
    Rect lock(0, 0, kTipSize, kTipSize);
    if (out->LockBits(&lock, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) {
        delete out;
        return nullptr;
    }
    auto* px = static_cast<BYTE*>(data.Scan0);
    for (int y = 0; y < kTipSize; ++y) {
        BYTE* row = px + y * data.Stride;
        for (int x = 0; x < kTipSize; ++x) {
            BYTE* p = row + x * 4;
            const BYTE b = p[0];
            const BYTE gch = p[1];
            const BYTE r = p[2];
            const BYTE a = p[3];
            const int lum = (r * 77 + gch * 150 + b * 29) >> 8;
            const BYTE alpha = static_cast<BYTE>((lum * a + 127) / 255);
            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            p[3] = alpha;
        }
    }
    out->UnlockBits(&data);
    return out;
}

Bitmap* TipFromSampleMask(const std::uint8_t* mask, int width, int height) {
    if (!mask || width < 1 || height < 1) return nullptr;
    Bitmap* bmp = new Bitmap(kTipSize, kTipSize, PixelFormat32bppARGB);
    if (!bmp || bmp->GetLastStatus() != Ok) {
        delete bmp;
        return nullptr;
    }
    BitmapData data = {};
    Rect lock(0, 0, kTipSize, kTipSize);
    if (bmp->LockBits(&lock, ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) {
        delete bmp;
        return nullptr;
    }
    auto* px = static_cast<BYTE*>(data.Scan0);
    for (int y = 0; y < kTipSize; ++y) {
        BYTE* row = px + y * data.Stride;
        for (int x = 0; x < kTipSize; ++x) {
            const int sx = x * width / kTipSize;
            const int sy = y * height / kTipSize;
            const BYTE lum = mask[sy * width + sx];
            BYTE* p = row + x * 4;
            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            p[3] = lum;
        }
    }
    bmp->UnlockBits(&data);
    return bmp;
}

int CustomBrushCount() {
    int count = 0;
    for (const BrushPreset& p : gPresets) {
        if (!p.builtin) ++count;
    }
    return count;
}

bool AddCustomPreset(const char* name, Bitmap* tip, float spacing, int defaultSize) {
    if (!tip) return false;
    if (CustomBrushCount() >= kMaxCustomBrushes || static_cast<int>(gPresets.size()) >= kMaxTotalBrushes) {
        return false;
    }
    BrushPreset preset;
    preset.name = name ? name : "Imported";
    preset.tip.reset(tip);
    preset.spacing = spacing;
    preset.defaultSize = defaultSize;
    preset.builtin = false;
    gPresets.push_back(std::move(preset));
    return true;
}

REAL StampAlphaScale() {
    int flow = brushFlow;
    int hard = brushHardness;
    if (flow < 1) flow = 1;
    if (flow > 100) flow = 100;
    if (hard < 1) hard = 1;
    if (hard > 100) hard = 100;
    return static_cast<REAL>(flow) / 100.0f * (0.45f + 0.55f * static_cast<REAL>(hard) / 100.0f);
}

void DrawRoundLine(Graphics* target, int x0, int y0, int x1, int y1, COLORREF color, int width, bool eraseTransparent) {
    if (!target) return;
    if (eraseTransparent) {
        target->SetCompositingMode(CompositingModeSourceOver);
        Pen pen(Color(255, 255, 255, 255), static_cast<REAL>(width));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        target->DrawLine(&pen, x0, y0, x1, y1);
        return;
    }
    Pen pen(GdiplusFromColor(color, 255), static_cast<REAL>(width));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    target->DrawLine(&pen, x0, y0, x1, y1);
}

void DrawStamp(Graphics* target, Bitmap* tip, int cx, int cy, COLORREF color, int size, bool eraseTransparent) {
    if (!target || !tip || size < 1) return;

    const REAL half = static_cast<REAL>(size) * 0.5f;
    const RectF dest(static_cast<REAL>(cx) - half, static_cast<REAL>(cy) - half,
        static_cast<REAL>(size), static_cast<REAL>(size));

    if (eraseTransparent) {
        ColorMatrix matrix = {
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 0.0f, 1.0f
        };
        ImageAttributes attrs;
        attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        const int w = static_cast<int>(tip->GetWidth());
        const int h = static_cast<int>(tip->GetHeight());
        target->SetCompositingMode(CompositingModeSourceOver);
        target->DrawImage(tip, dest, 0.0f, 0.0f, static_cast<REAL>(w), static_cast<REAL>(h), UnitPixel, &attrs);
        return;
    }

    const REAL r = static_cast<REAL>(GetRValue(color)) / 255.0f;
    const REAL g = static_cast<REAL>(GetGValue(color)) / 255.0f;
    const REAL b = static_cast<REAL>(GetBValue(color)) / 255.0f;
    const REAL alphaScale = StampAlphaScale();
    ColorMatrix matrix = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, alphaScale, 0.0f,
        r, g, b, 0.0f, 1.0f
    };
    ImageAttributes attrs;
    attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    const int w = static_cast<int>(tip->GetWidth());
    const int h = static_cast<int>(tip->GetHeight());
    target->DrawImage(tip, dest, 0.0f, 0.0f, static_cast<REAL>(w), static_cast<REAL>(h), UnitPixel, &attrs);
}

} // namespace

void InitBrushEngine() {
    if (!gPresets.empty()) return;
    AddBuiltinPreset("Round Soft", MakeRoundTip(0.15f), 0.18f, 8);
    AddBuiltinPreset("Round Hard", MakeRoundTip(0.85f), 0.12f, 6);
    AddBuiltinPreset("Charcoal", MakeCharcoalTip(), 0.10f, 14);
    AddBuiltinPreset("Flat Oil", MakeRoundTip(0.35f, 1.0f, 0.35f), 0.16f, 16);
    AddBuiltinPreset("Concept Soft", MakeConceptTip(), 0.20f, 20);
    AddBuiltinPreset("Pencil", MakePencilTip(), 0.08f, 4);
    LoadBrushSettings();
}

void ShutdownBrushEngine() {
    gPresets.clear();
    gActiveBrush = 0;
    gStampCarry = 0.0f;
}

int BrushPresetCount() {
    return static_cast<int>(gPresets.size());
}

const BrushPreset* GetBrushPreset(int index) {
    if (index < 0 || index >= static_cast<int>(gPresets.size())) return nullptr;
    return &gPresets[static_cast<size_t>(index)];
}

int GetActiveBrushIndex() {
    return gActiveBrush;
}

void SetActiveBrushIndex(int index) {
    if (index < 0 || index >= static_cast<int>(gPresets.size())) return;
    gActiveBrush = index;
    SaveBrushSettings();
    SyncBrushMenuItems();
}

const char* GetActiveBrushName() {
    const BrushPreset* preset = ActivePreset();
    return preset ? preset->name.c_str() : "Round Soft";
}

void ResetBrushStrokeState() {
    gStampCarry = 0.0f;
}

void DrawBrushStrokeSegment(Graphics* target, int x0, int y0, int x1, int y1,
    COLORREF color, int width, bool eraseTransparent, bool eraseOpaque) {
    if (!target) return;

    if (eraseOpaque) {
        DrawRoundLine(target, x0, y0, x1, y1, gTheme.canvasBg, width, false);
        return;
    }

    const BrushPreset* preset = ActivePreset();
    if (!preset || !preset->tip || currentTool != DrawTool::Pen) {
        DrawRoundLine(target, x0, y0, x1, y1, color, width, eraseTransparent);
        return;
    }

    const float dx = static_cast<float>(x1 - x0);
    const float dy = static_cast<float>(y1 - y0);
    const float segLen = sqrtf(dx * dx + dy * dy);
    const float spacing = MaxFloat(1.0f, preset->spacing * static_cast<float>(MaxInt(1, width)));

    if (segLen < 0.001f) {
        DrawStamp(target, preset->tip.get(), x1, y1, color, width, eraseTransparent);
        return;
    }

    float dist = gStampCarry;
    while (dist <= segLen) {
        const float t = dist / segLen;
        const int sx = x0 + static_cast<int>(dx * t + (dx >= 0.0f ? 0.5f : -0.5f));
        const int sy = y0 + static_cast<int>(dy * t + (dy >= 0.0f ? 0.5f : -0.5f));
        DrawStamp(target, preset->tip.get(), sx, sy, color, width, eraseTransparent);
        dist += spacing;
    }
    gStampCarry = dist - segLen;
}

bool ImportBrushTipFromFile(HWND owner, const char* path) {
    if (!path || !path[0]) return false;
    if (CustomBrushCount() >= kMaxCustomBrushes || static_cast<int>(gPresets.size()) >= kMaxTotalBrushes) {
        MessageBoxA(owner, "Custom brush limit reached.", "Import Brush", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    Bitmap loaded(PathToWide(path).c_str());
    if (loaded.GetLastStatus() != Ok) return false;
    Bitmap* tip = NormalizeImportedTip(&loaded);
    if (!tip) return false;

    std::string name = "Imported";
    const char* slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    if (slash && slash[1]) {
        name = slash + 1;
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos) name.erase(dot);
    }
    if (!AddCustomPreset(name.c_str(), tip, 0.15f, 12)) {
        delete tip;
        return false;
    }
    gActiveBrush = static_cast<int>(gPresets.size()) - 1;
    SaveBrushSettings();
    SyncBrushMenuItems();
    return true;
}

bool PromptImportBrushTip(HWND owner) {
    char filePath[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "Brush Tip Images\0*.png;*.bmp;*.jpg;*.jpeg\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (gLastBrowseDir[0]) ofn.lpstrInitialDir = gLastBrowseDir;
    if (!GetOpenFileNameA(&ofn)) return false;
    if (!ImportBrushTipFromFile(owner, filePath)) {
        MessageBoxA(owner, "Could not load brush tip image.", "Import Brush", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

struct AbrImportPickerState {
    const std::vector<AbrSampledBrush>* samples = nullptr;
    std::vector<int> selected;
};

static void AbrListSelectAll(HWND list, int count, bool select) {
    for (int i = 0; i < count; ++i) {
        SendMessageA(list, LB_SETSEL, select ? TRUE : FALSE, i);
    }
}

static INT_PTR CALLBACK AbrImportDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    AbrImportPickerState* state = reinterpret_cast<AbrImportPickerState*>(
        GetWindowLongPtrA(hDlg, GWLP_USERDATA));
    switch (message) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<AbrImportPickerState*>(lParam);
        SetWindowLongPtrA(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state || !state->samples) return TRUE;
        HWND list = GetDlgItem(hDlg, IDC_ABR_LIST);
        if (!list) return TRUE;
        const auto& samples = *state->samples;
        for (const AbrSampledBrush& sample : samples) {
            char line[128];
            snprintf(line, sizeof(line), "%s (%dx%d)", sample.name.c_str(), sample.width, sample.height);
            SendMessageA(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
        }
        AbrListSelectAll(list, static_cast<int>(samples.size()), true);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_ABR_SELECT_ALL:
            if (state && state->samples) {
                if (HWND list = GetDlgItem(hDlg, IDC_ABR_LIST)) {
                    AbrListSelectAll(list, static_cast<int>(state->samples->size()), true);
                }
            }
            return TRUE;
        case IDC_ABR_CLEAR_ALL:
            if (state && state->samples) {
                if (HWND list = GetDlgItem(hDlg, IDC_ABR_LIST)) {
                    AbrListSelectAll(list, static_cast<int>(state->samples->size()), false);
                }
            }
            return TRUE;
        case IDOK:
            if (state && state->samples) {
                HWND list = GetDlgItem(hDlg, IDC_ABR_LIST);
                if (!list) {
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
                }
                const int count = static_cast<int>(SendMessageA(list, LB_GETSELCOUNT, 0, 0));
                if (count <= 0) {
                    MessageBoxA(hDlg, "Select at least one brush.", "Import ABR", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                state->selected.resize(static_cast<size_t>(count));
                SendMessageA(list, LB_GETSELITEMS, count, reinterpret_cast<LPARAM>(state->selected.data()));
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

bool PromptAbrBrushSelection(HWND owner, const std::vector<AbrSampledBrush>& samples,
    std::vector<int>& outIndices) {
    outIndices.clear();
    if (samples.empty()) return false;
    if (samples.size() == 1) {
        outIndices.push_back(0);
        return true;
    }
    AbrImportPickerState state = {};
    state.samples = &samples;
    if (DialogBoxParamA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDD_ABR_IMPORT), owner,
            AbrImportDlgProc, reinterpret_cast<LPARAM>(&state)) != IDOK) {
        return false;
    }
    outIndices = std::move(state.selected);
    return !outIndices.empty();
}

bool ImportAbrBrushesFromFile(HWND owner, const char* path) {
    if (!path || !path[0]) return false;
    std::vector<AbrSampledBrush> samples;
    std::string error;
    if (!LoadAbrSampledBrushes(path, samples, error)) {
        if (owner && !error.empty()) {
            MessageBoxA(owner, error.c_str(), "Import ABR", MB_OK | MB_ICONERROR);
        }
        return false;
    }

    std::vector<int> pickIndices;
    if (!PromptAbrBrushSelection(owner, samples, pickIndices)) {
        return false;
    }

    int imported = 0;
    int firstIndex = -1;
    for (int sampleIdx : pickIndices) {
        if (sampleIdx < 0 || sampleIdx >= static_cast<int>(samples.size())) continue;
        if (CustomBrushCount() >= kMaxCustomBrushes || static_cast<int>(gPresets.size()) >= kMaxTotalBrushes) {
            break;
        }
        const AbrSampledBrush& sample = samples[static_cast<size_t>(sampleIdx)];
        Bitmap* tip = TipFromSampleMask(sample.mask.data(), sample.width, sample.height);
        if (!tip) continue;
        const float spacing = MaxFloat(0.05f, static_cast<float>(sample.spacing) / 100.0f);
        int defaultSize = MaxInt(sample.width, sample.height) / 4;
        if (defaultSize < 4) defaultSize = 4;
        if (defaultSize > 32) defaultSize = 32;
        if (!AddCustomPreset(sample.name.c_str(), tip, spacing, defaultSize)) {
            delete tip;
            continue;
        }
        if (firstIndex < 0) firstIndex = static_cast<int>(gPresets.size()) - 1;
        ++imported;
    }

    if (imported <= 0) {
        MessageBoxA(owner, "No brushes could be imported from this ABR file.", "Import ABR", MB_OK | MB_ICONERROR);
        return false;
    }

    gActiveBrush = firstIndex;
    SaveBrushSettings();
    SyncBrushMenuItems();

    char msg[128];
    sprintf_s(msg, "Imported %d brush(es). Active: %s", imported, GetActiveBrushName());
    MessageBoxA(owner, msg, "Import ABR", MB_OK | MB_ICONINFORMATION);
    return true;
}

bool PromptImportAbrBrushes(HWND owner) {
    char filePath[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "Photoshop Brushes\0*.abr\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (gLastBrowseDir[0]) ofn.lpstrInitialDir = gLastBrowseDir;
    if (!GetOpenFileNameA(&ofn)) return false;
    return ImportAbrBrushesFromFile(owner, filePath);
}

int GetBrushFlow() {
    return brushFlow;
}

int GetBrushHardness() {
    return brushHardness;
}

void SetBrushFlow(int flow) {
    if (flow < 1) flow = 1;
    if (flow > 100) flow = 100;
    brushFlow = flow;
    SaveBrushSettings();
    SyncBrushMenuItems();
}

void SetBrushHardness(int hardness) {
    if (hardness < 1) hardness = 1;
    if (hardness > 100) hardness = 100;
    brushHardness = hardness;
    SaveBrushSettings();
    SyncBrushMenuItems();
}

void LoadBrushSettings() {
    char iniPath[MAX_PATH] = {};
    GetFeaturesIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;
    const int idx = GetPrivateProfileIntA("Features", "ActiveBrush", 0, iniPath);
    if (idx >= 0 && idx < static_cast<int>(gPresets.size())) {
        gActiveBrush = idx;
    }
    brushFlow = GetPrivateProfileIntA("Features", "BrushFlow", 100, iniPath);
    brushHardness = GetPrivateProfileIntA("Features", "BrushHardness", 100, iniPath);
    if (brushFlow < 1) brushFlow = 1;
    if (brushFlow > 100) brushFlow = 100;
    if (brushHardness < 1) brushHardness = 1;
    if (brushHardness > 100) brushHardness = 100;
}

void SaveBrushSettings() {
    char iniPath[MAX_PATH] = {};
    GetFeaturesIniPath(iniPath, MAX_PATH);
    if (!iniPath[0]) return;
    char buf[16];
    sprintf_s(buf, "%d", gActiveBrush);
    WritePrivateProfileStringA("Features", "ActiveBrush", buf, iniPath);
    sprintf_s(buf, "%d", brushFlow);
    WritePrivateProfileStringA("Features", "BrushFlow", buf, iniPath);
    sprintf_s(buf, "%d", brushHardness);
    WritePrivateProfileStringA("Features", "BrushHardness", buf, iniPath);
}

void SyncBrushMenuItems() {
    if (!gAppMenu) return;
    HMENU toolsMenu = GetSubMenu(gAppMenu, 4);
    if (!toolsMenu) return;
    HMENU presetMenu = GetSubMenu(toolsMenu, 8);
    if (presetMenu) {
        for (int i = IDM_BRUSH_ROUND; i <= IDM_BRUSH_PENCIL; ++i) {
            CheckMenuItem(presetMenu, i, MF_BYCOMMAND | MF_UNCHECKED);
        }
        if (gActiveBrush >= 0 && gActiveBrush <= IDM_BRUSH_PENCIL - IDM_BRUSH_ROUND) {
            CheckMenuItem(presetMenu, IDM_BRUSH_ROUND + gActiveBrush, MF_BYCOMMAND | MF_CHECKED);
        }
    }

    HMENU dynamicsMenu = GetSubMenu(toolsMenu, 9);
    if (!dynamicsMenu) return;
    CheckMenuItem(dynamicsMenu, IDM_FLOW_LOW, MF_BYCOMMAND | (brushFlow == 25 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_FLOW_MED, MF_BYCOMMAND | (brushFlow == 50 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_FLOW_FULL, MF_BYCOMMAND | (brushFlow == 100 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_HARD_SOFT, MF_BYCOMMAND | (brushHardness == 35 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_HARD_MED, MF_BYCOMMAND | (brushHardness == 65 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(dynamicsMenu, IDM_HARD_HARD, MF_BYCOMMAND | (brushHardness == 100 ? MF_CHECKED : MF_UNCHECKED));
}
