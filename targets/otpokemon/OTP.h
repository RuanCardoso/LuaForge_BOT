#pragma once
/*
 * OTP.h - Tipos, padrões AOB e contexto global do alvo OT Pokemon
 *
 * Estrutura idêntica ao PxG.h, adaptada para o OT Pokemon:
 *   - Convenção de chamada __cdecl
 *   - Tipos de retorno diferentes (int, const char*, LARGE_INTEGER)
 *   - Padrões AOB específicos do executável do OT Pokemon
 */

#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <MinHook.h>
#include <cstdio>
#include <cstdint>
#include <string>

/* Estrutura opaca do estado Lua — declarada no escopo global para
   ser compatível com shared::LuaUtils e demais módulos */
struct lua_State;

namespace otp {

// Valor especial Lua: indica retorno múltiplo em lua_pcall
constexpr int LUA_MULTRET = -1;

// ================================================================
// Tipos de ponteiros de função Lua
// ================================================================

/* OT Pokemon usa convenção __cdecl no seu interpretador Lua */
using LuaCFunction   = int(__cdecl*)(lua_State* L);
using FnPushCClosure = unsigned int(__cdecl*)(lua_State* L, LuaCFunction fn, int n);
using FnSetField     = void(__cdecl*)(lua_State* L, int idx, const char* name);
using FnToInteger    = int(__cdecl*)(lua_State* L, int idx);
using FnToLString    = const char*(__cdecl*)(lua_State* L, int idx, unsigned int* len);
using FnLoadBuffer   = int(__cdecl*)(lua_State* L, const char* buff, size_t size, const char* name);
using FnPCall        = int(__cdecl*)(lua_State* L, int nargs, int nresults, int errfunc);
using FnTick         = LARGE_INTEGER(__cdecl*)();

// ================================================================
// Padrões AOB — usados pelo AobScan em InstallHooks()
// Os offsets abaixo são apenas referência histórica.
// ================================================================

// OFF_TOSTRING    ~ 0x40CA70 | 56 8B 74 24 ? 57 8B 7C 24 ? 56 57 E8 ? ? ? ? 83 C4 ? 8B 48 ? 83 F9 ? 75 ? ? ? EB ? 83 F9 ? 77 ? 8B 4F ? 8B 41 ? 3B 41 ? 72 ? 8B CF E8 ? ? ? ? 56 57 E8 ? ? ? ? 83 C4 ? 8B F0 8B D6 8B CF E8 ? ? ? ? ? ? C7 46 ? ? ? ? ? 8B 54 24 ? 85 D2 74 ? 8B 48 ? ? ? 5F 83 C0 ? 5E C3 8B 44 24
// OFF_CCLOSURE    ~ 0x40C1C0 | 56 8B 74 24 ? 8B 4E ? 8B 41 ? 3B 41 ? 72 ? 8B CE E8 ? ? ? ? 8B 46
// OFF_SETFIELD    ~ 0x40C6C0 | 55 8B EC 83 E4 ? 83 EC ? 56 8B 75 ? 57 FF 75 ? 56 E8 ? ? ? ? 8B 55 ? 8B F8 8B CA 83 C4 ? 8D 41 ? 89 44 24 ? ? ? 41 84 C0 75 ? 2B 4C 24 ? 51 52 56 E8 ? ? ? ? 89 44 24 ? 8D 44 24 ? 50 57 56 C7 44 24 ? ? ? ? ? E8 ? ? ? ? 8B 56
// OFF_TOINTEGER   ~ 0x40CA10 | 55 8B EC 83 E4 ? 83 EC ? FF 75 ? FF 75 ? E8 ? ? ? ? 83 C4 ? 8B 48 ? 83 F9 ? 73 ? ? ? ? ? F2 0F 2C C0 8B E5 5D C3 83 F9 ? 75 ? ? ? ? ? ? E8 ? ? ? ? 85 C0 74 ? ? ? ? ? ? F2 0F 2C C0 8B E5 5D C3 33 C0
// OFF_LOADBUFFER  ~ 0x13C1810 | scan via xref trampoline sub_13E8320 (E9 rel32 → sub_13C1810)
//                              | E9 ? ? ? ? F8 81 EC ? ? ? ? 8B 45 ? B9 ? ? ? ? 89 44 24 ? 8B 45 ? 89 44 24 ? 8D 44 24
// OFF_LOADFILE    ~ 0x4116B0 | 55 8B EC 83 E4 ? 81 EC ? ? ? ? A1 ? ? ? ? 33 C4 89 84 24 ? ? ? ? 56 8B 75 ? 57 8B 7D
// OFF_PCALL       ~ 0x40C120 | 8B 54 24 ? 8B 44 24 ? 53 56
// OFF_TICK        ~ 0x8218ED | 55 8B EC 51 51 8D 45 ? 50 FF 15

/* Índice da tabela global no ambiente Lua */
constexpr int LUA_GLOBALSINDEX = -10002;

// ================================================================
// Contexto global do módulo
// ================================================================

/**
 * @brief Estado vivo do módulo OTP durante a injeção.
 *
 * Todos os ponteiros de função são resolvidos via AobScan em InstallHooks().
 */
struct Context {
    uintptr_t      baseAddress   = 0;        // endereço base do módulo do jogo
    lua_State*     luaState      = nullptr;  // estado Lua capturado
    DWORD          luaThreadId   = 0;        // ID da thread que executa Lua
    std::string    pendingCode   = {};       // código Lua aguardando execução (via overlay)

    // Ponteiros de função — resolvidos via AOB em InstallHooks()
    FnPushCClosure fnPushClosure = nullptr;  // lua_pushcclosure
    FnSetField     fnSetField    = nullptr;  // lua_setfield
    FnToInteger    fnToInteger   = nullptr;  // lua_tointeger
    FnToLString    fnToLString   = nullptr;  // lua_tolstring
    FnLoadBuffer   fnLoadBuffer  = nullptr;  // luaL_loadbuffer (hookado)
    FnPCall        fnPCall       = nullptr;  // lua_pcall
    FnTick         fnTick        = nullptr;  // tick do jogo (hookado)
};

/* Instância única do contexto — definida em OTP_Hooks.cpp */
extern Context g_ctx;

/* Posição de mouse compartilhada entre mmove e mclick */
extern int g_mouseX;
extern int g_mouseY;

// ================================================================
// Utilitários (implementados em OTP_Lua.cpp)
// ================================================================

bool IsLuaThread();

/* Enfileira código Lua para execução na próxima iteração da thread Lua */
void ExecuteString(const char* code);

// ================================================================
// Funções Lua expostas ao script (implementadas em OTP_Lua.cpp)
// ================================================================

int __cdecl Lua_PressKey      (lua_State* L); // ckey(vk)
int __cdecl Lua_MoveCursor    (lua_State* L); // cmove(x, y)
int __cdecl Lua_SendClick     (lua_State* L); // cclick(btn)
int __cdecl Lua_MoveMouse     (lua_State* L); // mmove(x, y)
int __cdecl Lua_ClickMouse    (lua_State* L); // mclick(btn)
int __cdecl Lua_MoveMouse2    (lua_State* L); // mmove2(x, y)
int __cdecl Lua_ClickMouse2   (lua_State* L); // mclick2(btn)
int __cdecl Lua_MoveMouse3    (lua_State* L); // mmove3(x, y)
int __cdecl Lua_ClickMouse3   (lua_State* L); // mclick3(btn)
int __cdecl Lua_Print         (lua_State* L); // clog(str)
int __cdecl Lua_ReadInt       (lua_State* L); // rmem(addr)
int __cdecl Lua_ReadFloat     (lua_State* L); // rmemf(addr)
int __cdecl Lua_ReadString    (lua_State* L); // rmems(addr, len)
int __cdecl Lua_WriteInt      (lua_State* L); // wmem(addr, val)
int __cdecl Lua_WriteFloat    (lua_State* L); // wmemf(addr, val)
int __cdecl Lua_WriteString   (lua_State* L); // wmems(addr, str)
int __cdecl Lua_GetWindowSize (lua_State* L); // wsize()
int __cdecl Lua_GetWindowPos  (lua_State* L); // wpos()
int __cdecl Lua_GetCursorPos  (lua_State* L); // cpos()

// ================================================================
// Gerenciamento de hooks (implementado em OTP_Hooks.cpp)
// ================================================================

bool InstallHooks();

} // namespace otp

/* Ponto de entrada chamado pelo DllMain em DLL_PROCESS_ATTACH */
void OTP_Init(HMODULE module);
