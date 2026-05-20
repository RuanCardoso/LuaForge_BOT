#pragma once
/*
 * PxG.h - Tipos, padrões AOB e contexto global do alvo PokeXgAMES
 *
 * Este header centraliza tudo o que é compartilhado entre os demais
 * arquivos do módulo PxG:
 *   - Tipos de ponteiros de função Lua (__fastcall)
 *   - Padrões AOB para localização de funções em runtime
 *   - Estrutura de contexto global (Context)
 *   - Declarações de todas as funções do módulo
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

namespace pxg {

// Valor especial Lua: indica retorno múltiplo em lua_pcall
constexpr int LUA_MULTRET = -1;

// ================================================================
// Tipos de ponteiros de função Lua
// ================================================================

/* PokeXgAMES usa a convenção __fastcall no seu interpretador Lua */
using LuaCFunction   = int(__fastcall*)(lua_State* L);
using FnPushCClosure = uint64_t(__fastcall*)(lua_State* L, LuaCFunction fn, unsigned int n);
using FnSetField     = unsigned __int64*(__fastcall*)(lua_State* L, __int64 idx, const char* name);
using FnToInteger    = __int64(__fastcall*)(lua_State* L, __int64 idx);
using FnToLString    = __int64(__fastcall*)(lua_State* L, unsigned int idx, uint64_t* len);
using FnLoadBuffer   = __int64(__fastcall*)(lua_State* L, __int64 buff, __int64 size, const char* name, __int64 a5);
using FnPCall        = __int64(__fastcall*)(lua_State* L, int nargs, int nresults, int errfunc);
using FnTick         = LONGLONG(__fastcall*)();

// ================================================================
// Padrões AOB — usados pelo AobScan em InstallHooks()
// Os offsets abaixo são apenas referência histórica; não são usados
// em runtime pois os RVAs variam entre execuções.
// ================================================================

// OFF_TOSTRING    ~ 0x9E5B00 | 55 57 56 53 48 83 EC ? 89 D6 48 89 CF 4C 89 C3 E8 ? ? ? ? ? ? ? 48 89 C2 48 C1 FA ? 83 FA ? 75 ? 48 BA ? ? ? ? ? ? ? ? 48 21 D0 48 85 DB 74 ? 8B 78 ? ? ? ? 48 83 C0 ? 48 83 C4 ? 5B 5E 5F 5D C3
// OFF_CCLOSURE    ~ 0x9E88D0 | 55 57 56 53 48 83 EC ? 48 8B 41 ? 48 89 CB 48 89 D7 48 8B 48
// OFF_SETFIELD    ~ 0x9EAAD0 | 57 56 53 48 83 EC ? 4C 89 C6 48 89 CB E8 ? ? ? ? 48 89 F1 48 89 C7 E8 ? ? ? ? 48 89 F2 48 89 D9 49 89 C0 E8 ? ? ? ? 4C 8D 44 24 ? 48 89 D9 48 BA ? ? ? ? ? ? ? ? 48 09 D0 48 89 FA 48 89 44 24 ? E8 ? ? ? ? 48 8B 53 ? 48 85 C0 74 ? 48 8D 4A
// OFF_TOINTEGER   ~ 0x9E5A50 | 56 53 48 83 EC ? 48 89 CB 89 D6 4D 89 C2
// OFF_LOADBUFFER  ~ 0x9EE230 | 57 56 53 48 81 EC ? ? ? ? 48 8D 05 ? ? ? ? 66 0F EF C0 4D 85 C9
// OFF_PCALL       ~ 0x9ED380 | 41 56 41 55 41 54 55 57 56 53 48 83 EC ? 45 31 F6 4C 8B 69
// OFF_TICK        ~ 0x628190 | 53 48 83 EC ? 8B 05 ? ? ? ? 48 8B 1D

/* Índice da tabela global no ambiente Lua */
constexpr int LUA_GLOBALSINDEX = -10002;

// ================================================================
// Contexto global do módulo
// ================================================================

/**
 * @brief Armazena o estado vivo do módulo PxG durante a injeção.
 *
 * Todos os ponteiros de função são resolvidos via AobScan em InstallHooks()
 * e permanecem válidos durante toda a sessão injetada.
 */
struct Context {
    uintptr_t      baseAddress   = 0;        // endereço base do módulo do jogo
    lua_State*     luaState      = nullptr;  // ponteiro para o estado Lua capturado
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

/* Instância única do contexto — definida em PxG_Hooks.cpp */
extern Context g_ctx;

/* Posição de mouse usada pelas funções de clique virtual */
extern int g_mouseX;
extern int g_mouseY;

// ================================================================
// Utilitários (implementados em PxG_Lua.cpp)
// ================================================================

/* Retorna true se a thread atual é a que executa Lua */
bool IsLuaThread();

/* Enfileira código Lua para execução na próxima iteração da thread Lua */
void ExecuteString(const char* code);

// ================================================================
// Funções Lua expostas ao script (implementadas em PxG_Lua.cpp)
// ================================================================

int __fastcall Lua_PressKey      (lua_State* L); // ckey(vk)
int __fastcall Lua_MoveCursor    (lua_State* L); // cmove(x, y)
int __fastcall Lua_SendClick     (lua_State* L); // cclick(btn)
int __fastcall Lua_MoveMouse     (lua_State* L); // mmove(x, y)
int __fastcall Lua_ClickMouse    (lua_State* L); // mclick(btn)
int __fastcall Lua_MoveMouse2    (lua_State* L); // mmove2(x, y)
int __fastcall Lua_ClickMouse2   (lua_State* L); // mclick2(btn)
int __fastcall Lua_MoveMouse3    (lua_State* L); // mmove3(x, y)
int __fastcall Lua_ClickMouse3   (lua_State* L); // mclick3(btn)
int __fastcall Lua_Print         (lua_State* L); // clog(str)
int __fastcall Lua_ReadInt       (lua_State* L); // rmem(addr)
int __fastcall Lua_ReadFloat     (lua_State* L); // rmemf(addr)
int __fastcall Lua_ReadString    (lua_State* L); // rmems(addr, len)
int __fastcall Lua_WriteInt      (lua_State* L); // wmem(addr, val)
int __fastcall Lua_WriteFloat    (lua_State* L); // wmemf(addr, val)
int __fastcall Lua_WriteString   (lua_State* L); // wmems(addr, str)
int __fastcall Lua_GetWindowSize (lua_State* L); // wsize()
int __fastcall Lua_GetWindowPos  (lua_State* L); // wpos()
int __fastcall Lua_GetCursorPos  (lua_State* L); // cpos()

// ================================================================
// Gerenciamento de hooks (implementado em PxG_Hooks.cpp)
// ================================================================

/* Escaneia AOB e instala hooks via MinHook. Retorna true em caso de sucesso. */
bool InstallHooks();

} // namespace pxg

/* Ponto de entrada chamado pelo DllMain em DLL_PROCESS_ATTACH */
void PxG_Init(HMODULE module);
