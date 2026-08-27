#pragma once

// User-facing View preferences (persisted to features.ini beside the exe).
// Infrastructure always uses the modern event-bus path for dirty UI sync.

enum class AppFeature {
    WarnCanvasShrink,      // Confirm before shrinking canvas dimensions.
    PasteAtViewOrigin,     // Paste floating selection at viewport origin vs doc (0,0).
    SelectionExteriorVeil, // Dim area outside marquee when selecting.
    ReopenLastDocument,    // Reload last saved/opened file on startup.
    AutosaveRecovery,      // Periodic dirty-canvas snapshot + crash recovery prompt.
    CanvasGrid,            // Draw document grid overlay in the viewport.
    SnapToGrid,            // Snap pointer document coords to GRID_SPACING.
    Count
};

bool IsFeatureEnabled(AppFeature feature);
void SetFeatureEnabled(AppFeature feature, bool enabled);
bool FeatureDefaultEnabled(AppFeature feature);

void LoadFeatureFlags();
void SaveFeatureFlags();
void SyncFeatureFlagMenuItems();

// Toggles a preference, persists, and refreshes menu checkmarks. Returns new state.
bool ToggleFeatureFlag(AppFeature feature);
