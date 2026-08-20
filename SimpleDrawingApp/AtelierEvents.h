#pragma once

#include <windows.h>
#include "DrawingTools.h"

enum class AtelierEvent {
    DocumentDirtyChanged = 0,
    ActiveToolChanged,
    SelectionChanged,
    LayerListChanged,
    ChromeRebuildRequested,
    Count
};

struct EventPayload {
    AtelierEvent type = AtelierEvent::DocumentDirtyChanged;
    HWND hwnd = nullptr;
    bool documentDirty = false;
    DrawTool tool = DrawTool::Pen;
    bool hasSelection = false;
    int activeLayerIndex = -1;
    int layerCount = 0;
};
