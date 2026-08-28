#include "LayerStack.h"
#include "DrawingTools.h"
#include <cstdio>
#include <cstring>
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

    Layer bg = CreateLayer(width, height, "Background", true, background);
    if (!bg.bitmap || !bg.graphics) {
        FreeLayer(bg);
        return; // leave empty so EnsureCanvas can retry
    }
    Layer content = CreateLayer(width, height, "Layer 1", false, RGB(0, 0, 0));
    if (!content.bitmap || !content.graphics) {
        FreeLayer(content);
        FreeLayer(bg);
        return;
    }

    width_ = width;
    height_ = height;
    layers_.push_back(bg);
    layers_.push_back(content);
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

    struct NextLayer {
        Bitmap* bmp = nullptr;
        Graphics* g = nullptr;
    };
    std::vector<NextLayer> next;
    next.reserve(layers_.size());

    auto freeNext = [&]() {
        for (NextLayer& n : next) {
            delete n.g;
            delete n.bmp;
            n.g = nullptr;
            n.bmp = nullptr;
        }
        next.clear();
    };

    for (Layer& layer : layers_) {
        NextLayer n;
        n.bmp = new Bitmap(width, height, PixelFormat32bppARGB);
        if (!n.bmp || n.bmp->GetLastStatus() != Ok) {
            delete n.bmp;
            freeNext();
            return false;
        }
        n.g = Graphics::FromImage(n.bmp);
        if (!n.g || n.g->GetLastStatus() != Ok) {
            delete n.g;
            delete n.bmp;
            freeNext();
            return false;
        }
        Configure(n.g);
        if (layer.isBackground) {
            n.g->Clear(GdiplusFromColor(backgroundPad));
        }
        else {
            n.g->Clear(Color(0, 0, 0, 0));
        }
        if (layer.bitmap) {
            n.g->DrawImage(layer.bitmap, 0, 0);
        }
        next.push_back(n);
    }

    for (size_t i = 0; i < layers_.size(); ++i) {
        FreeLayer(layers_[i]);
        layers_[i].bitmap = next[i].bmp;
        layers_[i].graphics = next[i].g;
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
        FreeLayer(bg);
        return false;
    }
    bg.graphics->DrawImage(image, 0, 0);

    Layer content = CreateLayer(w, h, "Layer 1", false, RGB(0, 0, 0));
    if (!content.bitmap || !content.graphics) {
        FreeLayer(content);
        FreeLayer(bg);
        return false;
    }

    Destroy();
    width_ = w;
    height_ = h;
    layers_.push_back(bg);
    layers_.push_back(content);
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
    Rotate90Cw
};

bool TransformLayerBitmap(Bitmap* src, int outW, int outH, GeomTransform kind,
    bool isBackground, Bitmap*& outBmp, Graphics*& outG) {
    outBmp = nullptr;
    outG = nullptr;
    if (!src || outW < 1 || outH < 1) return false;

    outBmp = new Bitmap(outW, outH, PixelFormat32bppARGB);
    if (!outBmp || outBmp->GetLastStatus() != Ok) {
        delete outBmp;
        outBmp = nullptr;
        return false;
    }
    outG = Graphics::FromImage(outBmp);
    if (!outG || outG->GetLastStatus() != Ok) {
        delete outG;
        delete outBmp;
        outG = nullptr;
        outBmp = nullptr;
        return false;
    }
    outG->SetSmoothingMode(SmoothingModeNone);
    outG->SetInterpolationMode(InterpolationModeNearestNeighbor);
    outG->SetPixelOffsetMode(PixelOffsetModeHalf);
    outG->SetCompositingMode(CompositingModeSourceCopy);

    if (isBackground) {
        outG->Clear(Color(255, 255, 255, 255));
    } else {
        outG->Clear(Color(0, 0, 0, 0));
    }

    const int srcW = static_cast<int>(src->GetWidth());
    const int srcH = static_cast<int>(src->GetHeight());

    switch (kind) {
    case GeomTransform::FlipH:
        outG->TranslateTransform(static_cast<REAL>(outW), 0.0f);
        outG->ScaleTransform(-1.0f, 1.0f);
        break;
    case GeomTransform::FlipV:
        outG->TranslateTransform(0.0f, static_cast<REAL>(outH));
        outG->ScaleTransform(1.0f, -1.0f);
        break;
    case GeomTransform::Rotate90Cw:
        outG->TranslateTransform(static_cast<REAL>(outW), 0.0f);
        outG->RotateTransform(90.0f);
        break;
    }
    outG->DrawImage(src, 0, 0, srcW, srcH);
    outG->ResetTransform();
    outG->SetCompositingMode(CompositingModeSourceOver);
    outG->SetSmoothingMode(SmoothingModeAntiAlias);
    return true;
}

} // namespace

bool LayerStack::FlipHorizontal() {
    if (layers_.empty() || width_ < 1 || height_ < 1) return false;

    struct NextLayer { Bitmap* bmp = nullptr; Graphics* g = nullptr; };
    std::vector<NextLayer> next;
    next.reserve(layers_.size());
    auto freeNext = [&]() {
        for (NextLayer& n : next) {
            delete n.g;
            delete n.bmp;
        }
        next.clear();
    };

    for (Layer& layer : layers_) {
        NextLayer n;
        if (!layer.bitmap
            || !TransformLayerBitmap(layer.bitmap, width_, height_, GeomTransform::FlipH,
                layer.isBackground, n.bmp, n.g)) {
            freeNext();
            return false;
        }
        next.push_back(n);
    }

    for (size_t i = 0; i < layers_.size(); ++i) {
        FreeLayer(layers_[i]);
        layers_[i].bitmap = next[i].bmp;
        layers_[i].graphics = next[i].g;
    }
    return true;
}

bool LayerStack::FlipVertical() {
    if (layers_.empty() || width_ < 1 || height_ < 1) return false;

    struct NextLayer { Bitmap* bmp = nullptr; Graphics* g = nullptr; };
    std::vector<NextLayer> next;
    next.reserve(layers_.size());
    auto freeNext = [&]() {
        for (NextLayer& n : next) {
            delete n.g;
            delete n.bmp;
        }
        next.clear();
    };

    for (Layer& layer : layers_) {
        NextLayer n;
        if (!layer.bitmap
            || !TransformLayerBitmap(layer.bitmap, width_, height_, GeomTransform::FlipV,
                layer.isBackground, n.bmp, n.g)) {
            freeNext();
            return false;
        }
        next.push_back(n);
    }

    for (size_t i = 0; i < layers_.size(); ++i) {
        FreeLayer(layers_[i]);
        layers_[i].bitmap = next[i].bmp;
        layers_[i].graphics = next[i].g;
    }
    return true;
}

bool LayerStack::Rotate90Clockwise() {
    if (layers_.empty() || width_ < 1 || height_ < 1) return false;
    const int newW = height_;
    const int newH = width_;

    struct NextLayer { Bitmap* bmp = nullptr; Graphics* g = nullptr; };
    std::vector<NextLayer> next;
    next.reserve(layers_.size());
    auto freeNext = [&]() {
        for (NextLayer& n : next) {
            delete n.g;
            delete n.bmp;
        }
        next.clear();
    };

    for (Layer& layer : layers_) {
        NextLayer n;
        if (!layer.bitmap
            || !TransformLayerBitmap(layer.bitmap, newW, newH, GeomTransform::Rotate90Cw,
                layer.isBackground, n.bmp, n.g)) {
            freeNext();
            return false;
        }
        next.push_back(n);
    }

    for (size_t i = 0; i < layers_.size(); ++i) {
        FreeLayer(layers_[i]);
        layers_[i].bitmap = next[i].bmp;
        layers_[i].graphics = next[i].g;
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
    Layer layer = CreateLayer(width_, height_, name, false, RGB(0, 0, 0));
    if (!layer.bitmap || !layer.graphics) {
        FreeLayer(layer);
        return false;
    }
    layers_.push_back(layer);
    active_ = Count() - 1;
    return true;
}

bool LayerStack::DuplicateActiveLayer() {
    if (Count() >= kMaxLayers || width_ < 1 || height_ < 1) return false;
    const Layer* src = ActiveLayer();
    if (!src || !src->bitmap) return false;

    Bitmap* bmpCopy = CloneBitmap(src->bitmap);
    if (!bmpCopy) return false;

    Layer dup;
    dup.name = src->name + " copy";
    dup.visible = src->visible;
    dup.opacity = src->opacity;
    dup.isBackground = false;
    dup.bitmap = bmpCopy;
    dup.graphics = Graphics::FromImage(dup.bitmap);
    if (!dup.graphics || dup.graphics->GetLastStatus() != Ok) {
        delete dup.graphics;
        delete dup.bitmap;
        return false;
    }
    Configure(dup.graphics);

    const size_t insertAt = static_cast<size_t>(active_) + 1;
    layers_.insert(layers_.begin() + static_cast<ptrdiff_t>(insertAt), dup);
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

    Bitmap* mergedBmp = CloneBitmap(lower.bitmap);
    if (!mergedBmp) return false;
    Graphics* mergedG = Graphics::FromImage(mergedBmp);
    if (!mergedG || mergedG->GetLastStatus() != Ok) {
        delete mergedG;
        delete mergedBmp;
        return false;
    }
    Configure(mergedG);

    if (upper.visible && upper.bitmap) {
        int opacity = upper.opacity;
        if (opacity > 100) opacity = 100;
        if (opacity >= 100) {
            mergedG->DrawImage(upper.bitmap, 0, 0);
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
            mergedG->DrawImage(upper.bitmap, Rect(0, 0, w, h), 0, 0, w, h, UnitPixel, &attrs);
        }
    }

    delete lower.graphics;
    delete lower.bitmap;
    lower.bitmap = mergedBmp;
    lower.graphics = mergedG;

    FreeLayer(upper);
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
        }
        else {
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
