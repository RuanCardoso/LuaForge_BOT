/*
 * PxG_Hooks.cpp - Gerenciamento de hooks e contexto do PokeXgAMES
 *
 * Responsabilidades deste arquivo:
 *   - Definição das variáveis globais (g_ctx, g_mouseX, g_mouseY)
 *   - Enfileiramento e execução de scripts Lua externos
 *   - Registro das funções Lua no ambiente global do jogo
 *   - Callbacks dos hooks instalados via MinHook:
 *       Hook_Tick:       intercepta a função tick do jogo (chamada por frame)
 *       Hook_LoadBuffer: intercepta luaL_loadbuffer para capturar o lua_State
 *   - InstallHooks(): configura o console e instala ambos os hooks
 */

#include "PxG.h"
#include "../shared/AobScanner.h"

namespace pxg {

// ================================================================
// Variáveis globais (definições)
// ================================================================

/* Contexto global — populado pelos hooks após a injeção */
Context g_ctx;

/* Posição de mouse compartilhada entre mmove e mclick */
int g_mouseX = 0;
int g_mouseY = 0;

// ================================================================
// Estado interno dos hooks
// ================================================================

static bool s_functionsRegistered = false;

// ================================================================
// Execução de scripts externos
// ================================================================

void ExecuteString(const char* code)
{
    if (!g_ctx.luaState) {
        std::printf("[-] PxG: lua_State ainda nao foi capturado\n");
        return;
    }
    g_ctx.pendingCode = code;
}

static void ProcessPendingCode()
{
    if (g_ctx.pendingCode.empty() || !g_ctx.luaState) return;

    std::string code = std::move(g_ctx.pendingCode);
    g_ctx.pendingCode.clear();

    std::printf("[*] PxG: executando codigo do overlay (%zu bytes)\n", code.size());

    const int loadResult = g_ctx.fnLoadBuffer(g_ctx.luaState,
                                              reinterpret_cast<__int64>(code.c_str()),
                                              static_cast<__int64>(code.size()),
                                              "=(menu)", 0);
    if (loadResult != 0) {
        const char* err = reinterpret_cast<const char*>(
            g_ctx.fnToLString(g_ctx.luaState, (unsigned int)-1, nullptr));
        std::printf("[-] PxG: erro de compilacao: %s\n", err ? err : "desconhecido");
        return;
    }

    const int execResult = g_ctx.fnPCall(g_ctx.luaState, 0, LUA_MULTRET, 0);
    if (execResult == 0)
        std::printf("[+] PxG: execucao concluida\n");
    else {
        const char* err = reinterpret_cast<const char*>(
            g_ctx.fnToLString(g_ctx.luaState, (unsigned int)-1, nullptr));
        std::printf("[-] PxG: erro de execucao: %s\n", err ? err : "desconhecido");
    }
}

/**
 * @brief Registra todas as funções C no ambiente global Lua.
 *
 * Chamado uma única vez na primeira iteração da thread Lua.
 * Após este ponto, os scripts Lua podem usar ckey, mmove, rmem, etc.
 */
static void RegisterLuaFunctions()
{
    // Teclado
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_PressKey,   0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "ckey");

    // Log de debug
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_Print,      0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "clog");

    // Mouse real (SendInput / SetCursorPos)
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_MoveCursor, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "cmove");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_SendClick,  0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "cclick");

    // Mouse virtual v1 — PostMessage (segundo plano)
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_MoveMouse,  0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mmove");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ClickMouse, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mclick");

    // Mouse virtual v2 — SendMessage (síncrono)
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_MoveMouse2, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mmove2");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ClickMouse2,0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mclick2");

    // Mouse virtual v3 — SendInput absoluto
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_MoveMouse3, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mmove3");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ClickMouse3,0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mclick3");

    // Leitura de memória
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ReadInt,    0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "rmem");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ReadFloat,  0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "rmemf");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ReadString, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "rmems");

    // Escrita de memória
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_WriteInt,   0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "wmem");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_WriteFloat, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "wmemf");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_WriteString,0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "wmems");

    // Informações da janela
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_GetWindowSize, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "wsize");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_GetWindowPos,  0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "wpos");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_GetCursorPos,  0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "cpos");

    std::printf("[+] PxG: funcoes Lua registradas\n");
}

// ================================================================
// Callbacks de hook
// ================================================================

static void CallUpdate()
{
    if (!g_ctx.luaState || !s_functionsRegistered) return;

    static const char s_chunk[] = "if Update then Update() end";

    const int lr = g_ctx.fnLoadBuffer(g_ctx.luaState,
                                      reinterpret_cast<__int64>(s_chunk),
                                      sizeof(s_chunk) - 1,
                                      "=(update)", 0);
    if (lr != 0) {
        g_ctx.fnToLString(g_ctx.luaState, (unsigned int)-1, nullptr);
        return;
    }
    g_ctx.fnPCall(g_ctx.luaState, 0, 0, 0);
}

LONGLONG __fastcall Hook_Tick()
{
    if (IsLuaThread()) {
        if (!s_functionsRegistered && g_ctx.luaState) {
            s_functionsRegistered = true;
            RegisterLuaFunctions();
        }
        ProcessPendingCode();
        CallUpdate();
    }

    return g_ctx.fnTick();
}

/**
 * @brief Hook de luaL_loadbuffer — captura o lua_State na primeira chamada.
 *
 * Executado cada vez que o jogo carrega um chunk Lua.
 * Na primeira chamada registra o lua_State e o ID da thread Lua.
 */
__int64 __fastcall Hook_LoadBuffer(lua_State* L, __int64 buff, __int64 size,
                                    const char* name, __int64 a5)
{
    if (!g_ctx.luaState) {
        g_ctx.luaState    = L;
        g_ctx.luaThreadId = GetCurrentThreadId();
        std::printf("[+] PxG: lua_State captured at %p (thread %lu)\n",
            static_cast<void*>(L), g_ctx.luaThreadId);
    }

    return g_ctx.fnLoadBuffer(L, buff, size, name, a5);
}

// ================================================================
// Instalação dos hooks
// ================================================================

/**
 * @brief Inicializa o console de debug e instala ambos os hooks via MinHook.
 *
 * Deve ser chamado de uma thread separada (não do DllMain diretamente).
 * @return true em caso de sucesso total, false em qualquer falha.
 */
bool InstallHooks()
{
    // Aloca console para saída de debug durante a sessão injetada
    AllocConsole();
    FILE* file = nullptr;
    freopen_s(&file, "CONOUT$", "w", stdout);
    freopen_s(&file, "CONOUT$", "w", stderr);
    freopen_s(&file, "CONIN$",  "r", stdin);
    SetConsoleTitleA("LuaForge — PxG");

    g_ctx.baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));

    g_ctx.fnToLString   = reinterpret_cast<FnToLString>  (shared::AobScan("55 57 56 53 48 83 EC ? 89 D6 48 89 CF 4C 89 C3 E8 ? ? ? ? ? ? ? 48 89 C2 48 C1 FA ? 83 FA ? 75 ? 48 BA ? ? ? ? ? ? ? ? 48 21 D0 48 85 DB 74 ? 8B 78 ? ? ? ? 48 83 C0 ? 48 83 C4 ? 5B 5E 5F 5D C3"));
    g_ctx.fnPushClosure = reinterpret_cast<FnPushCClosure>(shared::AobScan("55 57 56 53 48 83 EC ? 48 8B 41 ? 48 89 CB 48 89 D7 48 8B 48"));
    g_ctx.fnSetField    = reinterpret_cast<FnSetField>   (shared::AobScan("57 56 53 48 83 EC ? 4C 89 C6 48 89 CB E8 ? ? ? ? 48 89 F1 48 89 C7 E8 ? ? ? ? 48 89 F2 48 89 D9 49 89 C0 E8 ? ? ? ? 4C 8D 44 24 ? 48 89 D9 48 BA ? ? ? ? ? ? ? ? 48 09 D0 48 89 FA 48 89 44 24 ? E8 ? ? ? ? 48 8B 53 ? 48 85 C0 74 ? 48 8D 4A"));
    g_ctx.fnToInteger   = reinterpret_cast<FnToInteger>  (shared::AobScan("56 53 48 83 EC ? 48 89 CB 89 D6 4D 89 C2"));
    g_ctx.fnPCall       = reinterpret_cast<FnPCall>      (shared::AobScan("41 56 41 55 41 54 55 57 56 53 48 83 EC ? 45 31 F6 4C 8B 69"));
    void* addrLoadBuffer = reinterpret_cast<void*>(shared::AobScan("57 56 53 48 81 EC ? ? ? ? 48 8D 05 ? ? ? ? 66 0F EF C0 4D 85 C9"));
    void* addrTick       = reinterpret_cast<void*>(shared::AobScan("53 48 83 EC ? 8B 05 ? ? ? ? 48 8B 1D"));

    if (!g_ctx.fnToLString  || !g_ctx.fnPushClosure || !g_ctx.fnSetField ||
        !g_ctx.fnToInteger  || !g_ctx.fnPCall        ||
        !addrLoadBuffer     || !addrTick) {
        std::printf("[-] PxG: um ou mais AOB scans falharam\n");
        return false;
    }

    std::printf("[*] PxG: base=0x%llX | LoadBuffer=%p | Tick=%p\n",
        (unsigned long long)g_ctx.baseAddress, addrLoadBuffer, addrTick);

    if (MH_Initialize() != MH_OK) {
        std::printf("[-] PxG: MH_Initialize falhou\n");
        return false;
    }

    // Hook de luaL_loadbuffer
    if (MH_CreateHook(addrLoadBuffer, &Hook_LoadBuffer,
                      reinterpret_cast<void**>(&g_ctx.fnLoadBuffer)) != MH_OK ||
        MH_EnableHook(addrLoadBuffer) != MH_OK) {
        std::printf("[-] PxG: hook de LoadBuffer falhou\n");
        return false;
    }

    // Hook da função tick
    if (MH_CreateHook(addrTick, &Hook_Tick,
                      reinterpret_cast<void**>(&g_ctx.fnTick)) != MH_OK ||
        MH_EnableHook(addrTick) != MH_OK) {
        std::printf("[-] PxG: hook de Tick falhou\n");
        return false;
    }

    std::printf("[+] PxG: hooks instalados com sucesso\n");
    return true;
}

} // namespace pxg
