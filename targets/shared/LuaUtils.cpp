/*
 * LuaUtils.cpp - Implementação dos utilitários Lua compartilhados
 *
 * Veja LuaUtils.h para documentação de cada função.
 */

#include "LuaUtils.h"
#include <cstdio>

namespace shared {

// ================================================================
// GetGameWindow
// ================================================================

HWND GetGameWindow()
{
    static HWND hwnd = NULL;
    if (hwnd) return hwnd;

    DWORD pid = GetCurrentProcessId();
    struct FindData { DWORD pid; HWND hwnd; };
    FindData fd = { pid, NULL };

    EnumWindows([](HWND wnd, LPARAM param) -> BOOL {
        auto* fd = reinterpret_cast<FindData*>(param);
        DWORD wndPid = 0;
        GetWindowThreadProcessId(wnd, &wndPid);
        if (wndPid != fd->pid || !IsWindowVisible(wnd))
            return TRUE;

        /* Ignora janelas auxiliares conhecidas */
        char cls[64] = {};
        GetClassNameA(wnd, cls, sizeof(cls));
        if (strcmp(cls, "ConsoleWindowClass") == 0) return TRUE; // console de debug
        if (strcmp(cls, "LuaForge_Overlay")        == 0) return TRUE; // nossa janela overlay

        fd->hwnd = wnd;
        return FALSE;
    }, (LPARAM)&fd);

    hwnd = fd.hwnd;
    return hwnd;
}

// ================================================================
// Funções de janela e cursor — manipulação direta do stack Lua
// ================================================================

int Lua_GetWindowSize(lua_State* L)
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return 0;

    RECT rect;
    GetClientRect(hwnd, &rect);
    int width  = rect.right  - rect.left;
    int height = rect.bottom - rect.top;

    DWORD* state_ptr = reinterpret_cast<DWORD*>(L);
    DWORD* top;

    top = reinterpret_cast<DWORD*>(state_ptr[5]);
    *reinterpret_cast<double*>(top) = (double)width;
    top[1] = 0xFFFFFFF3;
    state_ptr[5] += 8;

    top = reinterpret_cast<DWORD*>(state_ptr[5]);
    *reinterpret_cast<double*>(top) = (double)height;
    top[1] = 0xFFFFFFF3;
    state_ptr[5] += 8;

    return 2;
}

int Lua_GetWindowPos(lua_State* L)
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return 0;

    RECT rect;
    GetWindowRect(hwnd, &rect);

    DWORD* state_ptr = reinterpret_cast<DWORD*>(L);
    DWORD* top;

    top = reinterpret_cast<DWORD*>(state_ptr[5]);
    *reinterpret_cast<double*>(top) = (double)rect.left;
    top[1] = 0xFFFFFFF3;
    state_ptr[5] += 8;

    top = reinterpret_cast<DWORD*>(state_ptr[5]);
    *reinterpret_cast<double*>(top) = (double)rect.top;
    top[1] = 0xFFFFFFF3;
    state_ptr[5] += 8;

    return 2;
}

int Lua_GetCursorPos(lua_State* L)
{
    POINT pt;
    GetCursorPos(&pt);
    std::printf("[cursor] x=%ld y=%ld\n", pt.x, pt.y);

    DWORD* state_ptr = reinterpret_cast<DWORD*>(L);
    DWORD* top;

    top = reinterpret_cast<DWORD*>(state_ptr[5]);
    *reinterpret_cast<double*>(top) = (double)pt.x;
    top[1] = 0xFFFFFFF3;
    state_ptr[5] += 8;

    top = reinterpret_cast<DWORD*>(state_ptr[5]);
    *reinterpret_cast<double*>(top) = (double)pt.y;
    top[1] = 0xFFFFFFF3;
    state_ptr[5] += 8;

    return 2;
}

} // namespace shared
