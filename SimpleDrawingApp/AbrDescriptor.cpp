#include "AbrDescriptor.h"
#include "AbrComputed.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class DescReader {
public:
    DescReader(const std::uint8_t* data, size_t len) : data_(data), len_(len) {}

    bool readU8(std::uint8_t& v) {
        if (pos_ >= len_) return false;
        v = data_[pos_++];
        return true;
    }

    bool readBytes(void* dst, size_t n) {
        if (pos_ + n > len_) return false;
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    bool skip(size_t n) {
        if (pos_ + n > len_) return false;
        pos_ += n;
        return true;
    }

    bool readI32(std::int32_t& v) {
        std::uint8_t b[4];
        if (!readBytes(b, 4)) return false;
        v = (static_cast<std::int32_t>(b[0]) << 24) | (static_cast<std::int32_t>(b[1]) << 16)
            | (static_cast<std::int32_t>(b[2]) << 8) | static_cast<std::int32_t>(b[3]);
        return true;
    }

    bool readDouble(double& v) {
        std::uint8_t b[8];
        if (!readBytes(b, 8)) return false;
        std::uint64_t bits = (static_cast<std::uint64_t>(b[0]) << 56) | (static_cast<std::uint64_t>(b[1]) << 48)
            | (static_cast<std::uint64_t>(b[2]) << 40) | (static_cast<std::uint64_t>(b[3]) << 32)
            | (static_cast<std::uint64_t>(b[4]) << 24) | (static_cast<std::uint64_t>(b[5]) << 16)
            | (static_cast<std::uint64_t>(b[6]) << 8) | static_cast<std::uint64_t>(b[7]);
        std::memcpy(&v, &bits, sizeof(v));
        return true;
    }

    bool readFloat(float& v) {
        std::uint8_t b[4];
        if (!readBytes(b, 4)) return false;
        std::uint32_t bits = (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16)
            | (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
        std::memcpy(&v, &bits, sizeof(v));
        return true;
    }

    bool readKey(std::string& out) {
        out.clear();
        std::int32_t length = 0;
        if (!readI32(length)) return false;
        if (length == 0) {
            char sig[4];
            if (!readBytes(sig, 4)) return false;
            out.assign(sig, 4);
            return true;
        }
        if (length < 0 || static_cast<size_t>(length) > 256) return false;
        out.resize(static_cast<size_t>(length));
        if (!readBytes(out.data(), out.size())) return false;
        const size_t pad = (4 - (out.size() % 4)) % 4;
        return skip(pad);
    }

    bool readUnicodeString(std::string& out) {
        out.clear();
        std::int32_t length = 0;
        if (!readI32(length) || length < 0 || length > 4096) return false;
        if (length == 0) return true;
        std::vector<std::uint8_t> raw(static_cast<size_t>(length) * 2);
        if (!readBytes(raw.data(), raw.size())) return false;
        out.reserve(static_cast<size_t>(length));
        for (std::int32_t i = 0; i < length; ++i) {
            const wchar_t ch = static_cast<wchar_t>(raw[static_cast<size_t>(i) * 2]
                | (raw[static_cast<size_t>(i) * 2 + 1] << 8));
            if (ch == 0) break;
            if (ch < 128) out.push_back(static_cast<char>(ch));
            else out.push_back('?');
        }
        const size_t pad = (4 - (raw.size() % 4)) % 4;
        return skip(pad);
    }

private:
    const std::uint8_t* data_;
    size_t len_;
    size_t pos_ = 0;
};

struct DescNode;

struct DescValue {
    enum class Kind {
        None, Double, Unit, Text, Bool, Int, Object, List, Enum, ClassId
    } kind = Kind::None;

    double number = 0.0;
    std::string unit;
    std::string text;
    bool boolean = false;
    std::int32_t integer = 0;
    std::unique_ptr<DescNode> object;
    std::vector<DescValue> list;
    std::string enumType;
    std::string enumValue;
    std::string classId;
};

struct DescNode {
    std::string name;
    std::string classId;
    std::vector<std::pair<std::string, DescValue>> items;

    const DescValue* get(const char* key) const {
        for (const auto& item : items) {
            if (item.first == key) return &item.second;
        }
        return nullptr;
    }
};

bool ReadClassId(DescReader& in, std::string& out) {
    return in.readKey(out);
}

bool ReadDescriptorValue(DescReader& in, const char* osType, DescValue& out);

bool ReadDescriptor(DescReader& in, DescNode& out) {
    out = {};
    if (!in.readUnicodeString(out.name)) return false;
    if (!ReadClassId(in, out.classId)) return false;
    std::int32_t count = 0;
    if (!in.readI32(count) || count < 0 || count > 4096) return false;
    out.items.reserve(static_cast<size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        std::string key;
        char osType[5] = {};
        DescValue val;
        if (!in.readKey(key) || !in.readBytes(osType, 4)) return false;
        osType[4] = '\0';
        if (!ReadDescriptorValue(in, osType, val)) return false;
        out.items.emplace_back(std::move(key), std::move(val));
    }
    return true;
}

bool ReadDescriptorValue(DescReader& in, const char* osType, DescValue& out) {
    out = {};
    if (std::strncmp(osType, "doub", 4) == 0) {
        out.kind = DescValue::Kind::Double;
        return in.readDouble(out.number);
    }
    if (std::strncmp(osType, "long", 4) == 0) {
        out.kind = DescValue::Kind::Int;
        return in.readI32(out.integer);
    }
    if (std::strncmp(osType, "bool", 4) == 0) {
        out.kind = DescValue::Kind::Bool;
        std::uint8_t b = 0;
        if (!in.readU8(b)) return false;
        out.boolean = b != 0;
        return true;
    }
    if (std::strncmp(osType, "TEXT", 4) == 0) {
        out.kind = DescValue::Kind::Text;
        return in.readUnicodeString(out.text);
    }
    if (std::strncmp(osType, "UntF", 4) == 0 || std::strncmp(osType, "UnFl", 4) == 0) {
        out.kind = DescValue::Kind::Unit;
        char unitSig[5] = {};
        if (!in.readBytes(unitSig, 4)) return false;
        unitSig[4] = '\0';
        out.unit = unitSig;
        if (std::strncmp(osType, "UnFl", 4) == 0) {
            float f = 0.0f;
            if (!in.readFloat(f)) return false;
            out.number = f;
        } else if (!in.readDouble(out.number)) {
            return false;
        }
        return true;
    }
    if (std::strncmp(osType, "enum", 4) == 0) {
        out.kind = DescValue::Kind::Enum;
        if (!ReadClassId(in, out.enumType) || !ReadClassId(in, out.enumValue)) return false;
        return true;
    }
    if (std::strncmp(osType, "type", 4) == 0 || std::strncmp(osType, "GlbC", 4) == 0) {
        out.kind = DescValue::Kind::ClassId;
        std::string ignored;
        if (!in.readUnicodeString(ignored) || !ReadClassId(in, out.classId)) return false;
        return true;
    }
    if (std::strncmp(osType, "Objc", 4) == 0 || std::strncmp(osType, "GlbO", 4) == 0) {
        out.kind = DescValue::Kind::Object;
        out.object = std::make_unique<DescNode>();
        return ReadDescriptor(in, *out.object);
    }
    if (std::strncmp(osType, "VlLs", 4) == 0) {
        out.kind = DescValue::Kind::List;
        std::int32_t count = 0;
        if (!in.readI32(count) || count < 0 || count > 4096) return false;
        out.list.resize(static_cast<size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            char itemType[5] = {};
            if (!in.readBytes(itemType, 4)) return false;
            itemType[4] = '\0';
            if (!ReadDescriptorValue(in, itemType, out.list[static_cast<size_t>(i)])) return false;
        }
        return true;
    }
    if (std::strncmp(osType, "alis", 4) == 0) {
        out.kind = DescValue::Kind::Text;
        std::int32_t len = 0;
        if (!in.readI32(len) || len < 0 || len > 65536) return false;
        out.text.resize(static_cast<size_t>(len));
        if (len > 0 && !in.readBytes(out.text.data(), out.text.size())) return false;
        const size_t pad = (4 - (out.text.size() % 4)) % 4;
        return in.skip(pad);
    }
    if (std::strncmp(osType, "tdta", 4) == 0) {
        std::int32_t len = 0;
        if (!in.readI32(len) || len < 0) return false;
        return in.skip(static_cast<size_t>(len));
    }
    return false;
}

double AsNumber(const DescValue* v, double fallback = 0.0) {
    if (!v) return fallback;
    if (v->kind == DescValue::Kind::Double) return v->number;
    if (v->kind == DescValue::Kind::Int) return static_cast<double>(v->integer);
    if (v->kind == DescValue::Kind::Unit) return v->number;
    return fallback;
}

bool AsBool(const DescValue* v, bool fallback = false) {
    if (!v) return fallback;
    if (v->kind == DescValue::Kind::Bool) return v->boolean;
    return fallback;
}

std::string AsText(const DescValue* v) {
    if (!v || v->kind != DescValue::Kind::Text) return {};
    return v->text;
}

double ParsePercent(const DescValue* v, double fallback = 1.0) {
    if (!v || v->kind != DescValue::Kind::Unit) return fallback;
    if (v->unit == "#Prc" || v->unit == "Prc ") return v->number / 100.0;
    return v->number / 100.0;
}

double ParsePixels(const DescValue* v, double fallback = 32.0) {
    if (!v) return fallback;
    if (v->kind == DescValue::Kind::Unit) {
        if (v->unit == "#Pxl" || v->unit == "Pxl " || v->unit == "#Rlt") return v->number;
        return v->number;
    }
    if (v->kind == DescValue::Kind::Double) return v->number;
    if (v->kind == DescValue::Kind::Int) return static_cast<double>(v->integer);
    return fallback;
}

double ParseAngle(const DescValue* v, double fallback = 0.0) {
    if (!v || v->kind != DescValue::Kind::Unit) return fallback;
    if (v->unit == "#Ang" || v->unit == "Ang ") return v->number;
    return v->number;
}

const DescNode* AsObject(const DescValue* v) {
    if (!v || v->kind != DescValue::Kind::Object || !v->object) return nullptr;
    return v->object.get();
}

bool BrushFromComputedShape(const DescNode& shape, const std::string& brushName, AbrSampledBrush& out) {
    if (shape.classId != "computedBrush") return false;

    AbrComputedParams params = {};
    params.diameter = static_cast<float>(ParsePixels(shape.get("Dmtr"), 32.0));
    params.hardness = static_cast<float>(ParsePercent(shape.get("Hrdn"), 1.0));
    params.roundness = static_cast<float>(ParsePercent(shape.get("Rndn"), 1.0));
    params.angleDeg = static_cast<float>(ParseAngle(shape.get("Angl"), 0.0));
    if (AsBool(shape.get("Intr"), true)) {
        params.spacing = static_cast<int>(ParsePercent(shape.get("Spcn"), 0.25) * 100.0 + 0.5);
    } else {
        params.spacing = 25;
    }
    if (params.spacing < 1) params.spacing = 1;
    if (params.spacing > 1000) params.spacing = 1000;

    int maskSize = static_cast<int>(params.diameter + 0.5);
    if (maskSize < 16) maskSize = 16;
    if (maskSize > 128) maskSize = 128;

    if (!RasterizeComputedMask(params, maskSize, out.mask)) return false;
    out.width = maskSize;
    out.height = maskSize;
    out.spacing = params.spacing;
    out.name = brushName.empty() ? "Computed" : brushName;
    return true;
}

bool AppendComputedFromBrushDesc(const DescNode& brushDesc, std::vector<AbrSampledBrush>& out) {
    const DescNode* shape = AsObject(brushDesc.get("Brsh"));
    if (!shape) return false;
    if (shape->classId != "computedBrush") return false;

    std::string name = AsText(brushDesc.get("Nm  "));
    AbrSampledBrush sample;
    if (!BrushFromComputedShape(*shape, name, sample)) return false;
    if (sample.spacing <= 0) {
        sample.spacing = static_cast<int>(ParsePercent(brushDesc.get("Spcn"), 0.25) * 100.0 + 0.5);
    }
    out.push_back(std::move(sample));
    return true;
}

} // namespace

bool ParseDescComputedBrushes(const std::uint8_t* data, size_t len, std::vector<AbrSampledBrush>& out) {
    if (!data || len < 8) return false;
    DescReader reader(data, len);
    std::int32_t version = 0;
    if (!reader.readI32(version)) return false;
    (void)version;

    DescNode root;
    if (!ReadDescriptor(reader, root)) return false;

    const DescValue* brsh = root.get("Brsh");
    if (!brsh || brsh->kind != DescValue::Kind::List) return false;

    bool any = false;
    for (const DescValue& item : brsh->list) {
        const DescNode* brushDesc = AsObject(&item);
        if (!brushDesc) continue;
        if (AppendComputedFromBrushDesc(*brushDesc, out)) any = true;
    }
    return any;
}
