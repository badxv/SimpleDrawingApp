#pragma once

// Runtime feature flags for modern vs legacy behavior.
// Persisted to features.ini beside the executable (see AppFeatureFlags.cpp).

enum class AppFeature {
    EventBusDirtyUi,       // Dirty/clean updates via event bus (modern) vs direct UI calls.
    WarnCanvasShrink,      // Confirm before shrinking canvas dimensions.
    PasteAtViewOrigin,     // Paste floating selection at viewport origin (modern) vs doc (0,0).
    SelectionExteriorVeil, // Dim area outside marquee when selecting.
    Count
};

bool IsFeatureEnabled(AppFeature feature);
void SetFeatureEnabled(AppFeature feature, bool enabled);
bool FeatureDefaultEnabled(AppFeature feature);

void LoadFeatureFlags();
void SaveFeatureFlags();
void SyncFeatureFlagMenuItems();

// Toggles a flag, persists, and refreshes menu checkmarks. Returns new state.
bool ToggleFeatureFlag(AppFeature feature);
