#include "viewer.h"
#include <fstream>
#include <string>
#include <streambuf>

extern AppContext g_ctx;

static std::wstring GetSettingsPath() {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    PathAppendW(exePath, L"settings.json");
    return exePath;
}

void ReadSettings() {
    std::ifstream file(GetSettingsPath());
    if (file.is_open()) {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (content.find("\"startFullScreen\": true") != std::string::npos) {
            g_ctx.startFullScreen = true;
        }
        else {
            g_ctx.startFullScreen = false;
        }
    }
    else {
        g_ctx.startFullScreen = false;
    }
}

void WriteSettings() {
    std::ofstream file(GetSettingsPath());
    if (file.is_open()) {
        if (g_ctx.startFullScreen) {
            file << "{\n  \"startFullScreen\": true\n}\n";
        }
        else {
            file << "{\n  \"startFullScreen\": false\n}\n";
        }
    }
}