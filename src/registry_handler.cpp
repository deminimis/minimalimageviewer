#include "viewer.h"

static const wchar_t* g_progId = L"MinimalImageViewer.AssocFile.1";
static const wchar_t* g_appName = L"MinimalImageViewer";
static const wchar_t* g_fileExtensions[] = { L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".ico", L".tif", L".tiff" };

bool IsAppRegistered() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.png", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buffer[256];
        DWORD size = sizeof(buffer);
        if (RegQueryValueExW(hKey, L"", nullptr, nullptr, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (_wcsicmp(buffer, g_progId) == 0);
        }
        RegCloseKey(hKey);
    }
    return false;
}

void RegisterApp() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    HKEY hKey;
    RegCreateKeyExW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + g_progId).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, L"", 0, REG_SZ, (const BYTE*)L"Image File", (wcslen(L"Image File") + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);

    RegCreateKeyExW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + g_progId + L"\\DefaultIcon").c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    std::wstring iconPath = std::wstring(exePath) + L",0";
    RegSetValueExW(hKey, L"", 0, REG_SZ, (const BYTE*)iconPath.c_str(), (iconPath.length() + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);

    RegCreateKeyExW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + g_progId + L"\\shell\\open\\command").c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    std::wstring command = L"\"" + std::wstring(exePath) + L"\" \"%1\"";
    RegSetValueExW(hKey, L"", 0, REG_SZ, (const BYTE*)command.c_str(), (command.length() + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);

    for (const auto& ext : g_fileExtensions) {
        RegCreateKeyExW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + ext).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
        RegSetValueExW(hKey, L"", 0, REG_SZ, (const BYTE*)g_progId, (wcslen(g_progId) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    MessageBoxW(nullptr, L"Application has been set as the default viewer for common image types.", L"Registration Successful", MB_OK | MB_ICONINFORMATION);
}

void UnregisterApp() {
    for (const auto& ext : g_fileExtensions) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + ext).c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t buffer[256];
            DWORD size = sizeof(buffer);
            if (RegQueryValueExW(hKey, L"", nullptr, nullptr, (LPBYTE)buffer, &size) == ERROR_SUCCESS && _wcsicmp(buffer, g_progId) == 0) {
                RegDeleteKeyW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + ext).c_str());
            }
            RegCloseKey(hKey);
        }
    }

    SHDeleteKeyW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + g_progId).c_str());

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    MessageBoxW(nullptr, L"Application has been unregistered as the default viewer.", L"Unregistration Successful", MB_OK | MB_ICONINFORMATION);
}