#include "LayerStack.h"
#include "DrawingTools.h"
#include <cstdio>
#include <cstring>
#include <utility>

using namespace Gdiplus;
using Atelier::GdiplusBitmapPtr;
using Atelier::GdiplusGraphicsPtr;

namespace {

struct OwnedSurface {
    GdiplusBitmapPtr bmp;
    GdiplusGraphicsPtr g;
};

void FreeOwned(std::vector<OwnedSurface>& next) {
    next.clear();
}

} // namespace

void LayerStack::Configure(Graphics* g) {
    if (!g) return;
    g->SetSmoothingMode(SmoothingModeAntiAlias);
    g->SetCompositingMode(CompositingModeSourceOver);
}

GdiplusBitmapPtr LayerStack::CloneBitmap(Bitmap* source) {
    if (!source) return GdiplusBitmapPtr();
    GdiplusBitmapPtr copy(source->Clone(0, 0, source->GetWidth(), source->GetHeight(), PixelFormat32bppARGB));
    if (!copy || copy->GetLastStatus() != Ok) {
        return GdiplusBitmapPtr();
    }
    return copy;
}

Layer LayerStack::CreateLayer(int w, int h, const char* name, bool background, COLORREF bg) {
    Layer layer;
    layer.name = name ? name : "Layer";
    layer.isBackground = background;
    layer.visible = true;
    layer.opacity = 100;
    layer.bitmap = Atelier::MakeBitmap(w, h, PixelFormat32bppARGB);
    if (!layer.bitmap) {
        return layer;
    }
    layer.graphics = Atelier::MakeGraphics(layer.bitmap.get());
    if (!layer.graphics) {
        layer.bitmap.reset();
        return layer;
    }
    Configure(layer.graphics.get());
    if (background) {
        layer.graphics->Clear(GdiplusFromColor(bg));
    } else {
        layer.graphics->Clear(Color(0, 0, 0, 0));
    }
    return layer;
}

LayerStack::~LayerStack() {
    Destroy();
}

void LayerStack::Destroy() {
    layers_.clear();
    width_ = 0;
    height_ = 0;
    active_ = 0;
}

void LayerStack::Reset(int width, int height, COLORREF background) {
    Destroy();
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    Layer bg = CreateLayer(width, height, "Background", true, background);
    if (!bg.bitmap || !bg.graphics) {
        return; // leave empty so EnsureCanvas can retry
    }
    Layer content = CreateLayer(width, height, "Layer 1", false, RGB(0, 0, 0));
    if (!content.bitmap || !content.graphics) {
        return;
    }

    width_ = width;
    height_ = height;
    layers_.push_back(std::move(bg));
    layers_.push_back(std::move(content));
    active_ = 1;
}

bool LayerStack::Resize(int width, int height, COLORREF backgroundPad) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (layers_.empty()) {
        width_ = width;
        height_ = height;
        return true;
    }

    std::vector<OwnedSurface> next;
    next.reserve(layers_.size());

    for (Layer& layer : layers_) {
        OwnedSurface n;
        n.bmp = Atelier::MakeBitmap(width, height, PixelFormat32bppARGB);
        if (!n.bmp) {
            FreeOwned(next);
            return false;
        }
        n.g = Atelier::MakeGraphics(n.bmp.get());
        if (!n.g) {
            FreeOwned(next);
            return false;
        }
        Configure(n.g.get());
        if (layer.isBackground) {
            n.g->Clear(GdiplusFromColor(backgroundPad));
        } else {
            n.g->Clear(Color(0, 0, 0, 0));
        }
        if (layer.bitmap) {
            n.g->DrawImage(layer.bitmap.get(), 0, 0);
        }
        next.push_back(std::move(n));
    }

    for (size_t i = 0; i < layers_.size(); ++i) {
        layers_[i].bitmap = std::move(next[i].bmp);
        layers_[i].graphics = std::move(next[i].g);
    }
    width_ = width;
    height_ = height;
    return true;
}

bool LayerStack::ReplaceWithImage(Bitmap* image) {
    if (!image) return false;
    const int w = static_cast<int>(image->GetWidth());
    const int h = static_cast<int>(image->GetHeight());
    if (w < 1 || h < 1) return false;

    // Build the replacement stack first so failure keeps the live document.
    Layer bg = CreateLayer(w, h, "Background", true, RGB(255, 255, 255));
    if (!bg.bitmap || !bg.graphics) {
        return false;
    }
    bg.graphics->DrawImage(image, 0, 0);

    Layer content = CreateLayer(w, h, "Layer 1", false, RGB(0, 0, 0));
    if (!content.bitmap || !content.graphics) {
        return false;
    }

    Destroy();
    width_ = w;
    height_ = h;
    layers_.push_back(std::move(bg));
    layers_.push_back(std::move(content));
    active_ = 1;
    return true;
}

bool LayerStack::FlattenVisible() {
    if (Count() < 2) return false;
    Bitmap* flat = CreateComposite();
    if (!flat) return false;
    const bool ok = ReplaceWithImage(flat);
    delete flat;
    return ok;
}

namespace {

enum class GeomTransform {
    FlipH,
    FlipV,
    Rotate90Cw,
    Rotate90Ccw
};

bool TransformLayerBitmap(Bitmap* src, int outW, int outH, GeomTransform kind,
    bool isBackground, OwnedSurface& out) {
    out.bmp.reset();
    out.g.reset();
    if (!src || outW < 1 || outH < 1) return false;

    out.bmp = Atelier::MakeBitmap(outW, outH, PixelFormat32bppARGB);
    if (!out.bmp) return false;
    out.g = Atelier::MakeGraphics(out.bmp.get());
    if (!out.g) {
        out.bmp.reset();
        return false;
    }
    out.g->SetSmoothingMode(SmoothingModeNone);
    out.g->SetInterpolationMode(InterpolationModeNearestNeighbor);
    out.g->SetPixelOffsetMode(PixelOffsetModeHalf);
    out.g->SetCompositingMode(CompositingModeSourceCopy);

    if (isBackground) {
        out.g->Clear(Color(255, 255, 255, 255));
    } else {
        out.g->Clear(Color(0, 0, 0, 0));
    }

    const int srcW = static_cast<int>(src->GetWidth());
    const int srcH = static_cast<int>(src->GetHeight());

    switch (kind) {
    case GeomTransform::FlipH:
        out.g->TranslateTransform(static_cast<REAL>(outW), 0.0f);
        out.g->ScaleTransform(-1.0f, 1.0f);
        break;
    case GeomTransform::FlipV:
        out.g->TranslateTransform(0.0f, static_cast<REAL>(outH));
        out.g->ScaleTransform(1.0f, -1.0f);
        break;
    case GeomTransform::Rotate90Cw:
        out.g->TranslateTransform(static_cast<REAL>(outW), 0.0f);
        out.g->RotateTransform(90.0f);
        break;
    case GeomTransform::Rotate90Ccw:
        out.g->TranslateTransform(0.0f, static_cast<REAL>(outH));
        out.g->RotateTransform(-90.0f);
        break;
    }
    out.g->DrawImage(src, 0, 0, srcW, srcH);
    out.g->ResetTransform();
    out.g->SetCompositingMode(CompositingModeSourceOver);
    out.g->SetSmoothingMode(SmoothingModeAntiAlias);
    return true;
}

bool ApplyGeomTransform(std::vector<Layer>& layers, int width, int height, GeomTransform kind,
    int* outW, int* outH) {
    if (layers.empty() || width < 1 || height < 1) return false;
    const int newW = (kind == GeomTransform::Rotate90Cw || kind == GeomTransform::Rotate90Ccw)
        ? height : width;
    const int newH = (kind == GeomTransform::Rotate90Cw || kind == GeomTransform::Rotate90Ccw)
        ? width : height;

    std::vector<OwnedSurface> next;
    next.reserve(layers.size());
    for (Layer& layer : layers) {
        OwnedSurface n;
        if (!layer.bitmap
            || !TransformLayerBitmap(layer.bitmap.get(), newW, newH, kind, layer.isBackground, n)) {
            FreeOwned(next);
            return false;
        }
        next.push_back(std::move(n));
    }

    for (size_t i = 0; i < layers.size(); ++i) {
        layers[i].bitmap = std::move(next[i].bmp);
        layers[i].graphics = std::move(next[i].g);
    }
    if (outW) *outW = newW;
    if (outH) *outH = newH;
    return true;
}

} // namespace

bool LayerStack::FlipHorizontal() {
    return ApplyGeomTransform(layers_, width_, height_, GeomTransform::FlipH, nullptr, nullptr);
}

bool LayerStack::FlipVertical() {
    return ApplyGeomTransform(layers_, width_, height_, GeomTransform::FlipV, nullptr, nullptr);
}

bool LayerStack::Rotate90Clockwise() {
    int newW = 0, newH = 0;
    if (!ApplyGeomTransform(layers_, width_, height_, GeomTransform::Rotate90Cw, &newW, &newH)) {
        return false;
    }
    width_ = newW;
    height_ = newH;
    return true;
}

bool LayerStack::Rotate90CounterClockwise() {
    int newW = 0, newH = 0;
    if (!ApplyGeomTransform(layers_, width_, height_, GeomTransform::Rotate90Ccw, &newW, &newH)) {
        return false;
    }
    width_ = newW;
    height_ = newH;
    return true;
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
    return layer ? layer->bitmap.get() : nullptr;
}

Graphics* LayerStack::ActiveGraphics() {
    Layer* layer = ActiveLayer();
    return layer ? layer->graphics.get() : nullptr;
}

bool LayerStack::AddLayer() {
    if (Count() >= kMaxLayers || width_ < 1 || height_ < 1) return false;
    char name[32];
    sprintf_s(name, "Layer %d", Count());
    Layer layer = CreateLayer(width_, height_, name, false, RGB(0, 0, 0));
    if (!layer.bitmap || !layer.graphics) {
        return false;
    }
    layers_.push_back(std::move(layer));
    active_ = Count() - 1;
    return true;
}

bool LayerStack::DuplicateActiveLayer() {
    if (Count() >= kMaxLayers || width_ < 1 || height_ < 1) return false;
    const Layer* src = ActiveLayer();
    if (!src || !src->bitmap) return false;

    GdiplusBitmapPtr bmpCopy = CloneBitmap(src->bitmap.get());
    if (!bmpCopy) return false;

    Layer dup;
    dup.name = src->name + " copy";
    dup.visible = src->visible;
    dup.opacity = src->opacity;
    dup.isBackground = false;
    dup.bitmap = std::move(bmpCopy);
    dup.graphics = Atelier::MakeGraphics(dup.bitmap.get());
    if (!dup.graphics) {
        return false;
    }
    Configure(dup.graphics.get());

    const size_t insertAt = static_cast<size_t>(active_) + 1;
    layers_.insert(layers_.begin() + static_cast<ptrdiff_t>(insertAt), std::move(dup));
    active_ = static_cast<int>(insertAt);
    return true;
}

bool LayerStack::MergeActiveDown() {
    if (active_ <= 0 || width_ < 1 || height_ < 1) return false;

    const size_t upperIdx = static_cast<size_t>(active_);
    const size_t lowerIdx = upperIdx - 1;
    Layer& upper = layers_[upperIdx];
    Layer& lower = layers_[lowerIdx];
    if (!upper.bitmap || !lower.bitmap) return false;

    GdiplusBitmapPtr mergedBmp = CloneBitmap(lower.bitmap.get());
    if (!mergedBmp) return false;
    GdiplusGraphicsPtr mergedG = Atelier::MakeGraphics(mergedBmp.get());
    if (!mergedG) {
        return false;
    }
    Configure(mergedG.get());

    if (upper.visible && upper.bitmap) {
        int opacity = upper.opacity;
        if (opacity > 100) opacity = 100;
        if (opacity >= 100) {
            mergedG->DrawImage(upper.bitmap.get(), 0, 0);
        } else if (opacity >= 1) {
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
            const int w = static_cast<int>(upper.bitmap->GetWidth());
            const int h = static_cast<int>(upper.bitmap->GetHeight());
            mergedG->DrawImage(upper.bitmap.get(), Rect(0, 0, w, h), 0, 0, w, h, UnitPixel, &attrs);
        }
    }

    lower.bitmap = std::move(mergedBmp);
    lower.graphics = std::move(mergedG);

    layers_.erase(layers_.begin() + static_cast<ptrdiff_t>(upperIdx));
    --active_;
    return true;
}

bool LayerStack::DeleteActiveLayer() {
    if (Count() <= 1) return false;
    // Keep the special Background layer; delete content layers only.
    if (layers_[static_cast<size_t>(active_)].isBackground) {
        return false;
    }
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

bool LayerStack::NormalizeLayerName(const char* name, std::string& out) {
    out.clear();
    if (!name) return false;
    while (*name == ' ' || *name == '\t') ++name;
    size_t len = strlen(name);
    while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\t')) --len;
    if (len == 0) return false;
    if (len > 64) len = 64;
    out.assign(name, len);
    return true;
}

bool LayerStack::RenameActive(const char* name) {
    Layer* layer = ActiveLayer();
    if (!layer) return false;

    std::string next;
    if (!NormalizeLayerName(name, next)) return false;
    if (next == layer->name) return false;
    layer->name = std::move(next);
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

void LayerStack::ClearAllContent(COLORREF background) {
    for (Layer& layer : layers_) {
        if (!layer.graphics) continue;
        if (layer.isBackground) {
            layer.graphics->Clear(GdiplusFromColor(background));
        } else {
            layer.graphics->Clear(Color(0, 0, 0, 0));
        }
    }
}

void LayerStack::CompositeTo(Graphics* dest) const {
    if (!dest) return;
    for (const Layer& layer : layers_) {
        if (!layer.visible || !layer.bitmap) continue;
        int opacity = layer.opacity;
        if (opacity < 1) continue;
        if (opacity > 100) opacity = 100;

        if (opacity >= 100) {
            dest->DrawImage(layer.bitmap.get(), 0, 0);
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
        dest->DrawImage(layer.bitmap.get(), Rect(0, 0, w, h), 0, 0, w, h, UnitPixel, &attrs);
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
        dst.bitmap = CloneBitmap(src.bitmap.get());
        if (!dst.bitmap) {
            delete copy;
            return nullptr;
        }
        dst.graphics = Atelier::MakeGraphics(dst.bitmap.get());
        if (!dst.graphics) {
            delete copy;
            return nullptr;
        }
        Configure(dst.graphics.get());
        copy->layers_.push_back(std::move(dst));
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
