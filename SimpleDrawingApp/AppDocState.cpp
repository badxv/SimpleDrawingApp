#include "AppDocState.h"
#include "SimpleDrawingApp.h"

AppTheme gTheme;
LayerStack gLayers;
LayerHistory gHistory;

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
wchar_t gDocumentPath[MAX_PATH] = L"";
wchar_t gLastBrowseDir[MAX_PATH] = L"";
wchar_t gLastDocumentPath[MAX_PATH] = L"";
wchar_t gRecentDocuments[kMaxRecentDocuments][MAX_PATH] = {};
int gRecentDocumentCount = 0;

int gCanvasDlgWidth = 0;
int gCanvasDlgHeight = 0;
int gStatusHoverDocX = -1;
int gStatusHoverDocY = -1;
int gGridSpacing = kDefaultGridSpacing;
