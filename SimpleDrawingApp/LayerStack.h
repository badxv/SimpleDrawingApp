#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>

struct Layer {
    std::string name;
    Gdiplus::Bitmap* bitmap = nullptr;
    Gdiplus::Graphics* graphics = nullptr;
    bool visible = true;
    int opacity = 100; // 1-100
    bool isBackground = false;
};

// Bottom-to-top layer stack. Index 0 is the bottom (background).
class LayerStack {
public:
    static constexpr int kMaxLayers = 16;

    LayerStack() = default;
    ~LayerStack();

    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;

    void Reset(int width, int height, COLORREF background);
    void Destroy();

    int Width() const { return width_; }
    int Height() const { return height_; }
    int Count() const { return static_cast<int>(layers_.size()); }
    int ActiveIndex() const { return active_; }
    void SetActiveIndex(int index);

    Layer* ActiveLayer();
    const Layer* ActiveLayer() const;
    Layer* At(int index);
    const Layer* At(int index) const;

    Gdiplus::Bitmap* ActiveBitmap();
    Gdiplus::Graphics* ActiveGraphics();

    bool AddLayer();
    bool DeleteActiveLayer();
    bool MoveActiveUp();
    bool MoveActiveDown();
    bool RenameActive(const char* name);
    static bool NormalizeLayerName(const char* name, std::string& out);
    void SetActiveVisible(bool visible);
    void SetActiveOpacity(int opacity);

    // Returns false if allocation fails; existing layers unchanged.
    bool Resize(int width, int height, COLORREF backgroundPad);
    void ClearAllContent(COLORREF background);
    // Replaces the stack with a single background layer cloned from image.
    bool ReplaceWithImage(Gdiplus::Bitmap* image);

    void CompositeTo(Gdiplus::Graphics* dest) const;
    Gdiplus::Bitmap* CreateComposite() const;

    LayerStack* Clone() const;
    // Steal layers from other (other becomes empty).
    void TakeFrom(LayerStack* other);

private:
    int width_ = 0;
    int height_ = 0;
    int active_ = 0;
    std::vector<Layer> layers_;

    static void FreeLayer(Layer& layer);
    static void Configure(Gdiplus::Graphics* g);
    static Layer CreateLayer(int w, int h, const char* name, bool background, COLORREF bg);
    static Gdiplus::Bitmap* CloneBitmap(Gdiplus::Bitmap* source);
};
