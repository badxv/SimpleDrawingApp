#include "LayerStack.h"
#include "DrawingTools.h"
#include <cstdio>
#include <utility>

using namespace Gdiplus;

void LayerStack::Configure(Graphics* g) {
    if (!g) return;
    g->SetSmoothingMode(SmoothingModeAntiAlias);
    g->SetCompositingMode(CompositingModeSourceOver);
}

void LayerStack::FreeLayer(Layer& layer) {
    delete layer.graphics;
    delete layer.bitmap;
    layer.graphics = nullptr;
    layer.bitmap = nullptr;
}

Bitmap* LayerStack::CloneBitmap(Bitmap* source) {
    if (!source) return nullptr;
    Bitmap* copy = source->Clone(0, 0, source->GetWidth(), source->GetHeight(), PixelFormat32bppARGB);
    if (!copy || copy->GetLastStatus() != Ok) {
        delete copy;
        return nullptr;
    }
    return copy;
}

Layer LayerStack::CreateLayer(int w, int h, const char* name, bool background, COLORREF bg) {
    Layer layer;
    layer.name = name ? name : "Layer";
    layer.isBackground = background;
    layer.visible = true;
    layer.opacity = 100;
    layer.bitmap = new Bitmap(w, h, PixelFormat32bppARGB);
    if (!layer.bitmap || layer.bitmap->GetLastStatus() != Ok) {
        delete layer.bitmap;
        layer.bitmap = nullptr;
        return layer;
    }
    layer.graphics = Graphics::FromImage(layer.bitmap);
    if (!layer.graphics || layer.graphics->GetLastStatus() != Ok) {
        delete layer.graphics;
        delete layer.bitmap;
        layer.graphics = nullptr;
        layer.bitmap = nullptr;
        return layer;
    }
    Configure(layer.graphics);
    if (background) {
        layer.graphics->Clear(GdiplusFromColor(bg));
    }
    else {
        layer.graphics->Clear(Color(0, 0, 0, 0));
    }
    return layer;
}

LayerStack::~LayerStack() {
    Destroy();
}

void LayerStack::Destroy() {
    for (Layer& layer : layers_) {
        FreeLayer(layer);
    }
    layers_.clear();
    width_ = 0;
    height_ = 0;
    active_ = 0;
}

void LayerStack::Reset(int width, int height, COLORREF background) {
    Destroy();
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    width_ = width;
    height_ = height;
    layers_.push_back(CreateLayer(width_, height_, "Background", true, background));
    // Start drawing on a transparent content layer above Background.
    layers_.push_back(CreateLayer(width_, height_, "Layer 1", false, RGB(0, 0, 0)));
    active_ = 1;
}

void LayerStack::SetActiveIndex(int index) {
    if (index < 0 || index >= Count()) return;
    active_ = index;
}

Layer* LayerStack::ActiveLayer() {
    return At(active_);
}

const Layer* LayerStack::ActiveLayer() const {
    return At(active_);
}

Layer* LayerStack::At(int index) {
    if (index < 0 || index >= Count()) return nullptr;
    return &layers_[static_cast<size_t>(index)];
}

const Layer* LayerStack::At(int index) const {
    if (index < 0 || index >= Count()) return nullptr;
    return &layers_[static_cast<size_t>(index)];
}

Bitmap* LayerStack::ActiveBitmap() {
    Layer* layer = ActiveLayer();
    return layer ? layer->bitmap : nullptr;
}

Graphics* LayerStack::ActiveGraphics() {
    Layer* layer = ActiveLayer();
    return layer ? layer->graphics : nullptr;
}

bool LayerStack::AddLayer() {
    if (Count() >= kMaxLayers || width_ < 1 || height_ < 1) return false;
    char name[32];
    sprintf_s(name, "Layer %d", Count());
    layers_.push_back(CreateLayer(width_, height_, name, false, RGB(0, 0, 0)));
    active_ = Count() - 1;
    return true;
}

bool LayerStack::DeleteActiveLayer() {
    if (Count() <= 1) return false;
    // Keep the special Background layer; delete content layers only.
    if (layers_[static_cast<size_t>(active_)].isBackground) {
        return false;
    }
    FreeLayer(layers_[static_cast<size_t>(active_)]);
    layers_.erase(layers_.begin() + active_);
    if (active_ >= Count()) {
        active_ = Count() - 1;
    }
    return true;
}

bool LayerStack::MoveActiveUp() {
    if (active_ < 0 || active_ >= Count() - 1) return false;
    // Background stays pinned at the bottom (index 0), like Photoshop.
    if (layers_[static_cast<size_t>(active_)].isBackground) {
        return false;
    }
    std::swap(layers_[static_cast<size_t>(active_)], layers_[static_cast<size_t>(active_ + 1)]);
    ++active_;
    return true;
}

bool LayerStack::MoveActiveDown() {
    if (active_ <= 0 || active_ >= Count()) return false;
    // Do not move a layer under the Background, and do not move Background itself.
    if (layers_[static_cast<size_t>(active_)].isBackground) {
        return false;
    }
    if (layers_[static_cast<size_t>(active_ - 1)].isBackground) {
        return false;
    }
    std::swap(layers_[static_cast<size_t>(active_)], layers_[static_cast<size_t>(active_ - 1)]);
    --active_;
    return true;
}

void LayerStack::SetActiveVisible(bool visible) {
    if (Layer* layer = ActiveLayer()) {
        layer->visible = visible;
    }
}

void LayerStack::SetActiveOpacity(int opacity) {
    if (opacity < 1) opacity = 1;
    if (opacity > 100) opacity = 100;
    if (Layer* layer = ActiveLayer()) {
        layer->opacity = opacity;
    }
}

void LayerStack::Resize(int width, int height, COLORREF backgroundPad) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    for (Layer& layer : layers_) {
        Bitmap* nextBmp = new Bitmap(width, height, PixelFormat32bppARGB);
        if (!nextBmp || nextBmp->GetLastStatus() != Ok) {
            delete nextBmp;
            continue; // keep previous bitmap for this layer
        }
        Graphics* nextG = Graphics::FromImage(nextBmp);
        if (!nextG || nextG->GetLastStatus() != Ok) {
            delete nextG;
            delete nextBmp;
            continue;
        }
        Configure(nextG);
        if (layer.isBackground) {
            nextG->Clear(GdiplusFromColor(backgroundPad));
        }
        else {
            nextG->Clear(Color(0, 0, 0, 0));
        }
        if (layer.bitmap) {
            nextG->DrawImage(layer.bitmap, 0, 0);
        }
        FreeLayer(layer);
        layer.bitmap = nextBmp;
        layer.graphics = nextG;
    }

    width_ = width;
    height_ = height;
}

void LayerStack::ClearAllContent(COLORREF background) {
    for (Layer& layer : layers_) {
        if (!layer.graphics) continue;
        if (layer.isBackground) {
            layer.graphics->Clear(GdiplusFromColor(background));
        }
        else {
            layer.graphics->Clear(Color(0, 0, 0, 0));
        }
    }
}

bool LayerStack::ReplaceWithImage(Bitmap* image) {
    if (!image) return false;
    const int w = static_cast<int>(image->GetWidth());
    const int h = static_cast<int>(image->GetHeight());
    if (w < 1 || h < 1) return false;

    Destroy();
    width_ = w;
    height_ = h;
    Layer layer = CreateLayer(w, h, "Background", true, RGB(255, 255, 255));
    if (!layer.bitmap || !layer.graphics) {
        FreeLayer(layer);
        width_ = 0;
        height_ = 0;
        return false;
    }
    layer.graphics->DrawImage(image, 0, 0);
    layers_.push_back(layer);
    layers_.push_back(CreateLayer(w, h, "Layer 1", false, RGB(0, 0, 0)));
    active_ = 1;
    return true;
}

void LayerStack::CompositeTo(Graphics* dest) const {
    if (!dest) return;
    for (const Layer& layer : layers_) {
        if (!layer.visible || !layer.bitmap) continue;
        int opacity = layer.opacity;
        if (opacity < 1) continue;
        if (opacity > 100) opacity = 100;

        if (opacity >= 100) {
            dest->DrawImage(layer.bitmap, 0, 0);
            continue;
        }

        const REAL alpha = static_cast<REAL>(opacity) / 100.0f;
        ColorMatrix matrix = {
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, alpha, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 1.0f
        };
        ImageAttributes attrs;
        attrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
        const int w = static_cast<int>(layer.bitmap->GetWidth());
        const int h = static_cast<int>(layer.bitmap->GetHeight());
        dest->DrawImage(layer.bitmap, Rect(0, 0, w, h), 0, 0, w, h, UnitPixel, &attrs);
    }
}

Bitmap* LayerStack::CreateComposite() const {
    if (width_ < 1 || height_ < 1) return nullptr;
    Bitmap* out = new Bitmap(width_, height_, PixelFormat32bppARGB);
    if (!out || out->GetLastStatus() != Ok) {
        delete out;
        return nullptr;
    }
    Graphics g(out);
    // Transparent clear: the pinned Background layer (or workspace chrome) provides
    // the visible white. Content layers stay PNG-like with real alpha.
    g.Clear(Color(0, 0, 0, 0));
    Configure(&g);
    CompositeTo(&g);
    return out;
}

LayerStack* LayerStack::Clone() const {
    LayerStack* copy = new LayerStack();
    copy->width_ = width_;
    copy->height_ = height_;
    copy->active_ = active_;
    copy->layers_.reserve(layers_.size());
    for (const Layer& src : layers_) {
        Layer dst;
        dst.name = src.name;
        dst.visible = src.visible;
        dst.opacity = src.opacity;
        dst.isBackground = src.isBackground;
        dst.bitmap = CloneBitmap(src.bitmap);
        if (!dst.bitmap) {
            delete copy;
            return nullptr;
        }
        dst.graphics = Graphics::FromImage(dst.bitmap);
        if (!dst.graphics || dst.graphics->GetLastStatus() != Ok) {
            delete dst.graphics;
            delete dst.bitmap;
            delete copy;
            return nullptr;
        }
        Configure(dst.graphics);
        copy->layers_.push_back(dst);
    }
    return copy;
}

void LayerStack::TakeFrom(LayerStack* other) {
    if (!other) return;
    Destroy();
    width_ = other->width_;
    height_ = other->height_;
    active_ = other->active_;
    layers_ = std::move(other->layers_);
    other->width_ = 0;
    other->height_ = 0;
    other->active_ = 0;
    other->layers_.clear();
}
