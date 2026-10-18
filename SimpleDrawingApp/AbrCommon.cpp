#include "AbrCommon.h"

#include <cstring>
#include <string>

std::string LeafNameFromPath(const char* path, const char* fallback) {
    if (!path || !path[0]) return fallback ? fallback : "Imported";
    const char* slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    if (!slash || !slash[1]) return fallback ? fallback : "Imported";
    std::string name = slash + 1;
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos) name.erase(dot);
    return name.empty() ? (fallback ? fallback : "Imported") : name;
}

std::string MakeAbrBrushName(const char* path, const std::string& leafName) {
    std::string name = leafName;
    const char* slash = path ? strrchr(path, '\\') : nullptr;
    if (!slash) slash = path ? strrchr(path, '/') : nullptr;
    if (slash && slash[1]) {
        name = std::string(slash + 1) + "-" + leafName;
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos) name.erase(dot);
    }
    return name;
}
