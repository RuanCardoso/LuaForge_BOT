/*
 * OTP_Lua.cpp - Funções Lua expostas ao ambiente de script do OT Pokemon
 *
 * Idêntico em funcionalidade ao PxG_Lua.cpp, porém usando convenção __cdecl
 * e tipos de retorno adaptados ao interpretador Lua do OT Pokemon.
 *
 * Mapa de funções disponíveis no script:
 *   Teclado:      ckey(vk)
 *   Mouse real:   cmove(x,y)  | cclick(btn)
 *   Mouse v1:     mmove(x,y)  | mclick(btn)    [PostMessage  — segundo plano]
 *   Mouse v2:     mmove2(x,y) | mclick2(btn)   [SendMessage  — síncrono    ]
 *   Mouse v3:     mmove3(x,y) | mclick3(btn)   [SendInput absoluto         ]
 *   Memória R:    rmem(addr)  | rmemf(addr) | rmems(addr,len)
 *   Memória W:    wmem(addr,v)| wmemf(addr,v)| wmems(addr,str)
 *   Janela:       wsize() | wpos() | cpos()
 *   Debug:        clog(str)
 */

#include "OTP.h"
#include "../shared/LuaUtils.h"

namespace otp {

// ================================================================
// Utilitários internos
// ================================================================

/**
 * @brief Retorna true se a thread atual é a thread que executa Lua.
 */
bool IsLuaThread()
{
    return g_ctx.luaThreadId != 0 &&
           GetCurrentThreadId() == g_ctx.luaThreadId;
}

// ================================================================
// Controle de teclado
// ================================================================

/**
 * @brief ckey(vk) — Pressiona e solta uma tecla via PostMessage.
 * @param vk Código de tecla virtual Windows
 */
int __cdecl Lua_PressKey(lua_State* L)
{
    int vk = g_ctx.fnToInteger(L, 1);

    HWND hwnd = shared::GetGameWindow();
    if (hwnd) {
        PostMessageA(hwnd, WM_KEYDOWN, vk, 0);
        PostMessageA(hwnd, WM_KEYUP,   vk, 0);
    }
    return 0;
}

// ================================================================
// Controle de mouse — modo real (primeiro plano)
// ================================================================

/**
 * @brief cmove(x, y) — Move o cursor do sistema para coordenadas absolutas.
 */
int __cdecl Lua_MoveCursor(lua_State* L)
{
    int x = g_ctx.fnToInteger(L, 1);
    int y = g_ctx.fnToInteger(L, 2);
    SetCursorPos(x, y);
    return 0;
}

/**
 * @brief cclick(btn) — Clica com SendInput (afeta foco real do sistema).
 * @param btn 1=esquerdo, 2=direito, 3=meio
 */
int __cdecl Lua_SendClick(lua_State* L)
{
    int button = g_ctx.fnToInteger(L, 1);

    INPUT input[2] = {};
    switch (button) {
    case 1:
        input[0].type = INPUT_MOUSE; input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        input[1].type = INPUT_MOUSE; input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        break;
    case 2:
        input[0].type = INPUT_MOUSE; input[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        input[1].type = INPUT_MOUSE; input[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        break;
    case 3:
        input[0].type = INPUT_MOUSE; input[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
        input[1].type = INPUT_MOUSE; input[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
        break;
    }
    SendInput(2, input, sizeof(INPUT));
    return 0;
}

// ================================================================
// Controle de mouse — modo virtual v1 (PostMessage, segundo plano)
// ================================================================

/**
 * @brief mmove(x, y) — Move o mouse via PostMessage sem exigir foco.
 */
int __cdecl Lua_MoveMouse(lua_State* L)
{
    g_mouseX = g_ctx.fnToInteger(L, 1);
    g_mouseY = g_ctx.fnToInteger(L, 2);

    HWND hwnd = shared::GetGameWindow();
    if (!hwnd) return 0;

    POINT pt = { g_mouseX, g_mouseY };
    ScreenToClient(hwnd, &pt);
    PostMessageA(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
    return 0;
}

/**
 * @brief mclick(btn) — Clica via PostMessage na posição definida por mmove().
 */
int __cdecl Lua_ClickMouse(lua_State* L)
{
    int button = g_ctx.fnToInteger(L, 1);

    HWND hwnd = shared::GetGameWindow();
    if (!hwnd) return 0;

    POINT pt = { g_mouseX, g_mouseY };
    ScreenToClient(hwnd, &pt);
    LPARAM pos = MAKELPARAM(pt.x, pt.y);

    switch (button) {
    case 1:
        PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        PostMessageA(hwnd, WM_LBUTTONUP,   0,           pos);
        break;
    case 2:
        PostMessageA(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, pos);
        PostMessageA(hwnd, WM_RBUTTONUP,   0,           pos);
        break;
    case 3:
        PostMessageA(hwnd, WM_MBUTTONDOWN, MK_MBUTTON, pos);
        PostMessageA(hwnd, WM_MBUTTONUP,   0,           pos);
        break;
    }
    return 0;
}

// ================================================================
// Controle de mouse — modo virtual v2 (SendMessage, síncrono)
// ================================================================

/**
 * @brief mmove2(x, y) — Move o mouse via SendMessage (síncrono).
 */
int __cdecl Lua_MoveMouse2(lua_State* L)
{
    g_mouseX = g_ctx.fnToInteger(L, 1);
    g_mouseY = g_ctx.fnToInteger(L, 2);

    HWND hwnd = shared::GetGameWindow();
    if (!hwnd) return 0;

    POINT pt = { g_mouseX, g_mouseY };
    ScreenToClient(hwnd, &pt);
    SendMessageA(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
    return 0;
}

/**
 * @brief mclick2(btn) — Clica via SendMessage (síncrono).
 */
int __cdecl Lua_ClickMouse2(lua_State* L)
{
    int button = g_ctx.fnToInteger(L, 1);

    HWND hwnd = shared::GetGameWindow();
    if (!hwnd) return 0;

    POINT pt = { g_mouseX, g_mouseY };
    ScreenToClient(hwnd, &pt);
    LPARAM pos = MAKELPARAM(pt.x, pt.y);

    switch (button) {
    case 1:
        SendMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        SendMessageA(hwnd, WM_LBUTTONUP,   0,           pos);
        break;
    case 2:
        SendMessageA(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, pos);
        SendMessageA(hwnd, WM_RBUTTONUP,   0,           pos);
        break;
    case 3:
        SendMessageA(hwnd, WM_MBUTTONDOWN, MK_MBUTTON, pos);
        SendMessageA(hwnd, WM_MBUTTONUP,   0,           pos);
        break;
    }
    return 0;
}

// ================================================================
// Controle de mouse — modo virtual v3 (SendInput absoluto)
// ================================================================

/**
 * @brief mmove3(x, y) — Move o mouse via SendInput com coordenadas absolutas.
 */
int __cdecl Lua_MoveMouse3(lua_State* L)
{
    g_mouseX = g_ctx.fnToInteger(L, 1);
    g_mouseY = g_ctx.fnToInteger(L, 2);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    INPUT input       = {};
    input.type        = INPUT_MOUSE;
    input.mi.dx       = (g_mouseX * 65535) / screenW;
    input.mi.dy       = (g_mouseY * 65535) / screenH;
    input.mi.dwFlags  = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &input, sizeof(INPUT));
    return 0;
}

/**
 * @brief mclick3(btn) — Clica via PostMessage após posicionamento com mmove3().
 */
int __cdecl Lua_ClickMouse3(lua_State* L)
{
    int button = g_ctx.fnToInteger(L, 1);

    HWND hwnd = shared::GetGameWindow();
    if (!hwnd) return 0;

    POINT pt = { g_mouseX, g_mouseY };
    ScreenToClient(hwnd, &pt);
    LPARAM pos = MAKELPARAM(pt.x, pt.y);

    switch (button) {
    case 1:
        PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        PostMessageA(hwnd, WM_LBUTTONUP,   0,           pos);
        break;
    case 2:
        PostMessageA(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, pos);
        PostMessageA(hwnd, WM_RBUTTONUP,   0,           pos);
        break;
    case 3:
        PostMessageA(hwnd, WM_MBUTTONDOWN, MK_MBUTTON, pos);
        PostMessageA(hwnd, WM_MBUTTONUP,   0,           pos);
        break;
    }
    return 0;
}

// ================================================================
// Log de debug
// ================================================================

/**
 * @brief clog(str) — Imprime uma string no console de debug.
 */
int __cdecl Lua_Print(lua_State* L)
{
    const char* str = g_ctx.fnToLString(L, 1, nullptr);
    if (str)
        std::printf("[Lua] %s\n", str);
    return 0;
}

// ================================================================
// Leitura de memória
// ================================================================

/**
 * @brief rmem(addr) — Lê um inteiro de 4 bytes via SEH.
 */
int __cdecl Lua_ReadInt(lua_State* L)
{
    uintptr_t address = (uintptr_t)g_ctx.fnToInteger(L, 1);

    __try {
        int value = *reinterpret_cast<int*>(address);
        DWORD* state_ptr = reinterpret_cast<DWORD*>(L);
        DWORD* top       = reinterpret_cast<DWORD*>(state_ptr[5]);
        *reinterpret_cast<double*>(top) = (double)value;
        top[1] = 0xFFFFFFF3;
        state_ptr[5] += 8;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        std::printf("[-] rmem: endereco invalido 0x%llX\n", (unsigned long long)address);
        return 0;
    }
    return 1;
}

/**
 * @brief rmemf(addr) — Lê um float de 4 bytes via SEH.
 */
int __cdecl Lua_ReadFloat(lua_State* L)
{
    uintptr_t address = (uintptr_t)g_ctx.fnToInteger(L, 1);

    __try {
        float value = *reinterpret_cast<float*>(address);
        DWORD* state_ptr = reinterpret_cast<DWORD*>(L);
        DWORD* top       = reinterpret_cast<DWORD*>(state_ptr[5]);
        *reinterpret_cast<double*>(top) = (double)value;
        top[1] = 0xFFFFFFF3;
        state_ptr[5] += 8;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        std::printf("[-] rmemf: endereco invalido 0x%llX\n", (unsigned long long)address);
        return 0;
    }
    return 1;
}

/**
 * @brief rmems(addr, len) — Lê e imprime uma string de até 255 bytes.
 */
int __cdecl Lua_ReadString(lua_State* L)
{
    uintptr_t address = (uintptr_t)g_ctx.fnToInteger(L, 1);
    int length        = g_ctx.fnToInteger(L, 2);

    __try {
        char buf[256] = {};
        int len = min(length > 0 ? length : 255, 255);
        memcpy(buf, reinterpret_cast<void*>(address), len);
        std::printf("[rmems] 0x%llX = %s\n", (unsigned long long)address, buf);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        std::printf("[-] rmems: endereco invalido 0x%llX\n", (unsigned long long)address);
    }
    return 0;
}

// ================================================================
// Escrita de memória
// ================================================================

/**
 * @brief wmem(addr, val) — Escreve um inteiro removendo proteção de página.
 */
int __cdecl Lua_WriteInt(lua_State* L)
{
    uintptr_t address = (uintptr_t)g_ctx.fnToInteger(L, 1);
    int value         = g_ctx.fnToInteger(L, 2);

    __try {
        DWORD oldProtect;
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtect);
        *reinterpret_cast<int*>(address) = value;
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(int), oldProtect, &oldProtect);
        std::printf("[wmem] 0x%llX = %d\n", (unsigned long long)address, value);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        std::printf("[-] wmem: endereco invalido 0x%llX\n", (unsigned long long)address);
    }
    return 0;
}

/**
 * @brief wmemf(addr, val) — Escreve um float removendo proteção de página.
 */
int __cdecl Lua_WriteFloat(lua_State* L)
{
    uintptr_t address = (uintptr_t)g_ctx.fnToInteger(L, 1);
    float value       = (float)g_ctx.fnToInteger(L, 2);

    __try {
        DWORD oldProtect;
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect);
        *reinterpret_cast<float*>(address) = value;
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(float), oldProtect, &oldProtect);
        std::printf("[wmemf] 0x%llX = %f\n", (unsigned long long)address, value);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        std::printf("[-] wmemf: endereco invalido 0x%llX\n", (unsigned long long)address);
    }
    return 0;
}

/**
 * @brief wmems(addr, str) — Escreve uma string (incluindo o terminador nulo).
 */
int __cdecl Lua_WriteString(lua_State* L)
{
    uintptr_t address = (uintptr_t)g_ctx.fnToInteger(L, 1);
    const char* str   = g_ctx.fnToLString(L, 2, nullptr);
    if (!str) return 0;

    __try {
        size_t len = strlen(str) + 1;
        DWORD oldProtect;
        VirtualProtect(reinterpret_cast<void*>(address), len, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(reinterpret_cast<void*>(address), str, len);
        VirtualProtect(reinterpret_cast<void*>(address), len, oldProtect, &oldProtect);
        std::printf("[wmems] 0x%llX = %s\n", (unsigned long long)address, str);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        std::printf("[-] wmems: endereco invalido 0x%llX\n", (unsigned long long)address);
    }
    return 0;
}

// ================================================================
// Informações da janela — delegam para shared::LuaUtils
// ================================================================

/** @brief wsize() — Retorna largura e altura da área cliente. */
int __cdecl Lua_GetWindowSize(lua_State* L) { return shared::Lua_GetWindowSize(L); }

/** @brief wpos() — Retorna a posição da janela na tela. */
int __cdecl Lua_GetWindowPos(lua_State* L)  { return shared::Lua_GetWindowPos(L); }

/** @brief cpos() — Retorna a posição atual do cursor. */
int __cdecl Lua_GetCursorPos(lua_State* L)  { return shared::Lua_GetCursorPos(L); }

} // namespace otp
