#include "AppPaths.h"

#include <windows.h>
#include <cstring>

void GetFeaturesIniPath(char* path, size_t pathChars) {
    if (!path || pathChars == 0) return;
    path[0] = '\0';
    DWORD len = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(pathChars));
    if (len == 0 || len >= pathChars) return;
    for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
        if (path[i] == '\\' || path[i] == '/') {
            path[i + 1] = '\0';
            break;
        }
    }
    if (strlen(path) + 13 > pathChars) {
        path[0] = '\0';
        return;
    }
    strcat_s(path, pathChars, "features.ini");
}
