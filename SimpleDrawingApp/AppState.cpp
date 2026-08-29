#include "AppState.h"
#include "SimpleDrawingApp.h"
#include "AppMetrics.h"

AppTheme gTheme;
LayerStack gLayers;
LayerHistory gHistory;
HFONT gUiFont = nullptr;
HFONT gBrandFont = nullptr;
HBRUSH gChromeBrush = nullptr;
HBRUSH gChromeDeepBrush = nullptr;
HBRUSH gChromeElevatedBrush = nullptr;
HACCEL gAccel = nullptr;
HWND hwndTooltip = nullptr;
HWND hwndBrand = nullptr;
float gUiPulse = 0.0f;
float gUiCompassAngle = -18.0f;
float gToolFlash = 0.0f;
float gIdlePhase = 0.0f;
float gSelAntOffset = 0.0f;
bool gUiSizing = false;
Atelier::GdiplusBitmapPtr gChromeCache;
int gChromeCacheW = 0;
int gChromeCacheH = 0;
int gChromeCacheStatusH = -1;
int gChromeCacheRailW = -1;
int gChromeCacheLayerW = -1;
Atelier::GdiplusBitmapPtr gBrandStrip;
Atelier::WinBitmapHandle gBrandStripHbmp;
int gBrandStripH = 0;

HWND hwndViewport = nullptr;
HWND hwndScrollH = nullptr;
HWND hwndScrollV = nullptr;
HWND hwndScrollCorner = nullptr;
HWND hwndSlider = nullptr;
HWND hwndPenWidthBox = nullptr;
HWND hwndOpacitySlider = nullptr;
HWND hwndOpacityBox = nullptr;
HWND hwndSizeLabel = nullptr;
HWND hwndOpacityLabel = nullptr;
HWND hwndStatus = nullptr;
HWND hwndLayerList = nullptr;
HWND hwndLayerAdd = nullptr;
HWND hwndLayerDel = nullptr;
HWND hwndLayerUp = nullptr;
HWND hwndLayerDown = nullptr;
HWND hwndLayerDup = nullptr;
HWND hwndLayerVisible = nullptr;
HWND hwndLayerOpacity = nullptr;
HWND hwndToolButtons[kToolButtonCount] = {};
HWND hwndPalette = nullptr;
HWND hwndActionButtons[7] = {};
HWND hwndBgButton = nullptr;
HWND hwndSwapColors = nullptr;
HWND hwndShapeFlyout = nullptr;
HWND hwndShapeButtons[6] = {};
HWND hwndShapeModeButtons[3] = {};
HWND hwndToggleRail = nullptr;
HWND hwndToggleLayers = nullptr;
HWND hwndPaletteFloat = nullptr;
HWND hwndMenuButtons[6] = {};
HWND hwndCaptionMin = nullptr;
HWND hwndCaptionMax = nullptr;
HWND hwndCaptionClose = nullptr;
HWND gCaptionHot = nullptr;
HMENU gAppMenu = nullptr;

bool gRailOpen = true;
bool gLayersOpen = true;
bool gBottomOpen = true;
bool gPaletteFloating = false;
POINT gPaletteFloatPos = { 80, 120 };
bool gFloatDragging = false;
POINT gFloatDragHot = {};

POINT lastPoint = {};
POINT shapeStart = {};
bool isDrawing = false;
bool suppressEditNotify = false;
bool suppressLayerNotify = false;

int docWidth = DEFAULT_DOC_WIDTH;
int docHeight = DEFAULT_DOC_HEIGHT;
int scrollX = 0;
int scrollY = 0;
float zoomFactor = 1.0f;

SelectionState gSel;
Atelier::GdiplusBitmapPtr gClipboardBmp;
Atelier::GdiplusBitmapPtr compositeCache;
bool compositeDirty = true;
Atelier::GdiplusBitmapPtr strokeLayer;
Atelier::GdiplusGraphicsPtr strokeGraphics;

COLORREF penColor = RGB(0, 0, 0);
COLORREF backColor = RGB(255, 255, 255);
int penWidth = 5;
int penOpacity = 100;
int brushFlow = 100;
int brushHardness = 100;
bool penPressureEnabled = true;
float lastPenPressure = 1.0f;
DrawTool currentTool = DrawTool::Pen;
ShapeKind currentShape = ShapeKind::Rectangle;
ShapePaintMode shapePaintMode = ShapePaintMode::Stroke;
bool documentDirty = false;
char gDocumentPath[MAX_PATH] = "";
char gLastBrowseDir[MAX_PATH] = "";
char gLastDocumentPath[MAX_PATH] = "";
char gRecentDocuments[kMaxRecentDocuments][MAX_PATH] = {};
int gRecentDocumentCount = 0;

int gCanvasDlgWidth = 0;
int gCanvasDlgHeight = 0;
int gStatusHoverDocX = -1;
int gStatusHoverDocY = -1;
int gGridSpacing = kDefaultGridSpacing;
