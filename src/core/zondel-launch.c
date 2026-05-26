/* License: GPL-2.0-or-later. Copyright (c) 2026 Zondel. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "zondel-launch.h"

/* Probe order: HKLM uninstall key for Zondel install path → zondel:// URI →
 * https://zondel.net/download as the final fallback. */

static int try_launch_installed(void) {
    static const char *roots[] = {
        "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    };
    for (int i = 0; i < 2; i++) {
        HKEY hRoot;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, roots[i], 0, KEY_READ, &hRoot) != ERROR_SUCCESS)
            continue;
        char subKey[256];
        DWORD subKeyLen, idx = 0;
        while (subKeyLen = sizeof(subKey), RegEnumKeyExA(hRoot, idx++, subKey, &subKeyLen,
                                                         NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            HKEY hSub;
            if (RegOpenKeyExA(hRoot, subKey, 0, KEY_READ, &hSub) != ERROR_SUCCESS) continue;
            char dispName[256] = {0};
            DWORD dispLen = sizeof(dispName), type = 0;
            if (RegQueryValueExA(hSub, "DisplayName", NULL, &type,
                                 (LPBYTE)dispName, &dispLen) == ERROR_SUCCESS
                && type == REG_SZ
                && strstr(dispName, "Zondel")) {
                char installLoc[512] = {0};
                DWORD locLen = sizeof(installLoc);
                if (RegQueryValueExA(hSub, "InstallLocation", NULL, &type,
                                     (LPBYTE)installLoc, &locLen) == ERROR_SUCCESS
                    && type == REG_SZ && installLoc[0]) {
                    char exePath[768];
                    snprintf(exePath, sizeof(exePath), "%s\\Zondel.App.exe", installLoc);
                    if (GetFileAttributesA(exePath) != INVALID_FILE_ATTRIBUTES) {
                        HINSTANCE rc = ShellExecuteA(NULL, "open", exePath, NULL, NULL, SW_SHOWNORMAL);
                        RegCloseKey(hSub); RegCloseKey(hRoot);
                        return (INT_PTR)rc > 32;
                    }
                }
            }
            RegCloseKey(hSub);
        }
        RegCloseKey(hRoot);
    }
    return 0;
}

static int try_launch_uri(void) {
    HINSTANCE rc = ShellExecuteA(NULL, "open", "zondel://", NULL, NULL, SW_SHOWNORMAL);
    return (INT_PTR)rc > 32;
}

static void launch_browser_fallback(void) {
    ShellExecuteA(NULL, "open", "https://zondel.net/download", NULL, NULL, SW_SHOWNORMAL);
}

void zondel_launch_open_app(void) {
    if (try_launch_installed()) return;
    if (try_launch_uri()) return;
    launch_browser_fallback();
}
