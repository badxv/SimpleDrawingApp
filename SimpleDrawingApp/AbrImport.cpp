#include "AbrImport.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <memory>
#include <vector>

namespace {

class AbrReader {
public:
    explicit AbrReader(const char* path) {
        if (path && path[0]) {
            file_.reset(fopen(path, "rb"));
        }
    }

    explicit operator bool() const { return file_ != nullptr; }

    bool readU8(std::uint8_t& v) {
        return fread(&v, 1, 1, file_.get()) == 1;
    }

    bool readI16(std::int16_t& v) {
        std::uint8_t b[2];
        if (fread(b, 1, 2, file_.get()) != 2) return false;
        v = static_cast<std::int16_t>((b[0] << 8) | b[1]);
        return true;
    }

    bool readI32(std::int32_t& v) {
        std::uint8_t b[4];
        if (fread(b, 1, 4, file_.get()) != 4) return false;
        v = (static_cast<std::int32_t>(b[0]) << 24)
            | (static_cast<std::int32_t>(b[1]) << 16)
            | (static_cast<std::int32_t>(b[2]) << 8)
            | static_cast<std::int32_t>(b[3]);
        return true;
    }

    bool readBytes(void* dst, size_t n) {
        return fread(dst, 1, n, file_.get()) == n;
    }

    bool skip(size_t n) {
        return fseek(file_.get(), static_cast<long>(n), SEEK_CUR) == 0;
    }

    long tell() const { return ftell(file_.get()); }

    bool seek(long pos) {
        return fseek(file_.get(), pos, SEEK_SET) == 0;
    }

private:
    struct FileCloser {
        void operator()(FILE* f) const {
            if (f) fclose(f);
        }
    };
    std::unique_ptr<FILE, FileCloser> file_;
};

constexpr int kMaxBrushDim = 2048;

bool ReadUcs2String(AbrReader& in, std::string& out) {
    out.clear();
    std::int32_t len = 0;
    if (!in.readI32(len) || len < 0 || len > 256) return false;
    if (len == 0) return true;
    std::vector<std::uint8_t> raw(static_cast<size_t>(len) * 2);
    if (!in.readBytes(raw.data(), raw.size())) return false;
    out.reserve(static_cast<size_t>(len));
    for (std::int32_t i = 0; i < len; ++i) {
        const wchar_t ch = static_cast<wchar_t>(raw[static_cast<size_t>(i) * 2]
            | (raw[static_cast<size_t>(i) * 2 + 1] << 8));
        if (ch == 0) break;
        if (ch < 128) out.push_back(static_cast<char>(ch));
        else out.push_back('?');
    }
    return true;
}

bool RleDecode(AbrReader& in, std::uint8_t* dst, int width, int height) {
    if (width < 1 || height < 1) return false;
    for (int y = 0; y < height; ++y) {
        int x = 0;
        while (x < width) {
            std::uint8_t header = 0;
            if (!in.readU8(header)) return false;
            if (header <= 127) {
                const int count = static_cast<int>(header) + 1;
                for (int i = 0; i < count; ++i) {
                    if (x >= width) return false;
                    if (!in.readU8(dst[y * width + x])) return false;
                    ++x;
                }
            } else {
                const int count = 257 - static_cast<int>(header);
                std::uint8_t value = 0;
                if (!in.readU8(value)) return false;
                for (int i = 0; i < count; ++i) {
                    if (x >= width) return false;
                    dst[y * width + x] = value;
                    ++x;
                }
            }
        }
    }
    return true;
}

bool MaskToSample(const std::uint8_t* mask, int width, int height, int spacing,
    const std::string& name, AbrSampledBrush& out) {
    if (!mask || width < 1 || height < 1) return false;
    out.name = name;
    out.spacing = spacing;
    out.width = width;
    out.height = height;
    out.mask.assign(mask, mask + static_cast<size_t>(width) * static_cast<size_t>(height));
    return true;
}

bool LoadSampledV12(AbrReader& in, int version, int index, const char* path,
    AbrSampledBrush& out) {
    std::int16_t type = 0;
    std::int32_t blockSize = 0;
    if (!in.readI16(type) || !in.readI32(blockSize)) return false;
    if (type != 2) {
        if (blockSize > 0) in.skip(static_cast<size_t>(blockSize));
        return false;
    }

    std::int32_t misc = 0;
    std::int16_t spacing = 25;
    if (!in.readI32(misc) || !in.readI16(spacing)) return false;

    std::string sampleName;
    if (version == 2) {
        if (!ReadUcs2String(in, sampleName)) return false;
    }

    std::uint8_t antialias = 0;
    if (!in.readU8(antialias)) return false;
    (void)antialias;

    for (int i = 0; i < 4; ++i) {
        std::int16_t ignored = 0;
        if (!in.readI16(ignored)) return false;
    }

    std::int32_t top = 0, left = 0, bottom = 0, right = 0;
    if (!in.readI32(top) || !in.readI32(left) || !in.readI32(bottom) || !in.readI32(right)) {
        return false;
    }

    std::int16_t depthBits = 0;
    if (!in.readI16(depthBits)) return false;
    if ((depthBits >> 3) != 1) return false;

    const int height = bottom - top;
    const int width = right - left;
    if (width < 1 || height < 1 || width > kMaxBrushDim || height > kMaxBrushDim) return false;
    if (height > 16384) return false;

    std::uint8_t compress = 0;
    if (!in.readU8(compress)) return false;

    std::vector<std::uint8_t> mask(static_cast<size_t>(width) * static_cast<size_t>(height));
    if (!compress) {
        if (!in.readBytes(mask.data(), mask.size())) return false;
    } else if (!RleDecode(in, mask.data(), width, height)) {
        return false;
    }

    std::string name;
    if (!sampleName.empty()) name = sampleName;
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "abr-%03d", index + 1);
        name = buf;
    }
    const char* slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    if (slash && slash[1]) {
        name = std::string(slash + 1) + "-" + name;
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos) name.erase(dot);
    }
    return MaskToSample(mask.data(), width, height, spacing, name, out);
}

bool Reach8BimSection(AbrReader& in, const char* tagName) {
    while (true) {
        char tag[4];
        char name[4];
        if (!in.readBytes(tag, 4) || !in.readBytes(name, 4)) return false;
        if (strncmp(tag, "8BIM", 4) != 0) return false;
        if (strncmp(name, tagName, 4) == 0) return true;
        std::int32_t sectionSize = 0;
        if (!in.readI32(sectionSize) || sectionSize < 0) return false;
        if (!in.skip(static_cast<size_t>(sectionSize))) return false;
    }
}

bool LoadSampledV6(AbrReader& in, int subVersion, int index, const char* path,
    AbrSampledBrush& out) {
    std::int32_t brushSize = 0;
    if (!in.readI32(brushSize) || brushSize < 0) return false;

    long brushEnd = in.tell() + brushSize;
    while (brushEnd % 4 != 0) ++brushEnd;

    const size_t skipBytes = (subVersion == 1) ? 47u : 301u;
    if (!in.skip(skipBytes)) return false;

    std::int32_t top = 0, left = 0, bottom = 0, right = 0;
    std::int16_t depthBits = 0;
    std::uint8_t compress = 0;
    if (!in.readI32(top) || !in.readI32(left) || !in.readI32(bottom) || !in.readI32(right)
        || !in.readI16(depthBits) || !in.readU8(compress)) {
        return false;
    }

    const int depth = depthBits >> 3;
    const int width = right - left;
    const int height = bottom - top;
    if (width < 1 || height < 1 || width > kMaxBrushDim || height > kMaxBrushDim) return false;
    if (compress > 1) return false;
    if (compress && depth != 1) return false;
    if (!compress && (depth < 1 || depth > 2)) return false;

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<std::uint8_t> mask(pixelCount);

    if (!compress) {
        if (depth == 1) {
            if (!in.readBytes(mask.data(), mask.size())) return false;
        } else {
            std::vector<std::uint8_t> raw(pixelCount * 2);
            if (!in.readBytes(raw.data(), raw.size())) return false;
            for (size_t i = 0; i < pixelCount; ++i) {
                const std::uint16_t v = static_cast<std::uint16_t>(
                    (raw[i * 2] << 8) | raw[i * 2 + 1]);
                mask[i] = static_cast<std::uint8_t>((v * 255u) / 65535u);
            }
        }
    } else if (!RleDecode(in, mask.data(), width, height)) {
        return false;
    }

    if (in.tell() < brushEnd) in.seek(brushEnd);

    char buf[64];
    snprintf(buf, sizeof(buf), "abr-%03d", index + 1);
    std::string name = buf;
    const char* slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    if (slash && slash[1]) {
        name = std::string(slash + 1) + "-" + name;
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos) name.erase(dot);
    }
    return MaskToSample(mask.data(), width, height, 25, name, out);
}

} // namespace

bool LoadAbrSampledBrushes(const char* path, std::vector<AbrSampledBrush>& out, std::string& error) {
    out.clear();
    error.clear();
    if (!path || !path[0]) {
        error = "Empty path.";
        return false;
    }

    AbrReader in(path);
    if (!in) {
        error = "Could not open file.";
        return false;
    }

    std::int16_t version = 0;
    std::int16_t secondField = 0;
    if (!in.readI16(version) || !in.readI16(secondField)) {
        error = "Truncated ABR header.";
        return false;
    }

    if (version == 1 || version == 2) {
        const int count = secondField;
        if (count < 1 || count > 512) {
            error = "Unsupported brush count.";
            return false;
        }
        for (int i = 0; i < count; ++i) {
            AbrSampledBrush sample;
            if (LoadSampledV12(in, version, i, path, sample)) {
                out.push_back(std::move(sample));
            }
        }
    } else if (version == 6 || version == 10) {
        const int subVersion = secondField;
        if (subVersion != 1 && subVersion != 2) {
            error = "Unsupported ABR sub-version.";
            return false;
        }
        if (!Reach8BimSection(in, "samp")) {
            error = "No sampled brush section.";
            return false;
        }
        std::int32_t sectionSize = 0;
        if (!in.readI32(sectionSize) || sectionSize < 0) {
            error = "Invalid samp section.";
            return false;
        }
        const long sectionEnd = in.tell() + sectionSize;
        int index = 0;
        while (in.tell() < sectionEnd) {
            AbrSampledBrush sample;
            if (LoadSampledV6(in, subVersion, index, path, sample)) {
                out.push_back(std::move(sample));
            }
            ++index;
            if (index > 512) break;
        }
    } else {
        error = "Unsupported ABR version.";
        return false;
    }

    if (out.empty()) {
        error = "No sampled brushes found.";
        return false;
    }
    return true;
}
