/*
 * OTP_Hooks.cpp - Hook management and context for OT Pokemon
 *
 * Differences from PxG_Hooks.cpp:
 *   - Scripts loaded with luaL_loadfile (no prior file read in C++)
 *   - Supports 4 hotkeys: F5→test1.txt, F6→test2.txt, F7→test3.txt, F8→test4.txt
 *   - FnTick returns LARGE_INTEGER (not LONGLONG)
 *   - Hook_LoadBuffer has 4 parameters (no fifth __int64 a5 like PxG)
 */

#include "OTP.h"
#include "../shared/AobScanner.h"

namespace otp {

// ================================================================
// Global variables (definitions)
// ================================================================

Context g_ctx;
int g_mouseX = 0;
int g_mouseY = 0;

// ================================================================
// Internal hook state
// ================================================================

static bool s_functionsRegistered = false;

/* Rising-edge control for each hotkey */
static bool s_f5Pressed = false;
static bool s_f6Pressed = false;
static bool s_f7Pressed = false;
static bool s_f8Pressed = false;

// ================================================================
// External script execution
// ================================================================

void ExecuteString(const char* code)
{
    if (!g_ctx.luaState) {
        std::printf("[-] OTP: lua_State not captured yet\n");
        return;
    }
    g_ctx.pendingCode = code;
}

static void ProcessPendingCode()
{
    if (g_ctx.pendingCode.empty() || !g_ctx.luaState) return;

    std::string code = std::move(g_ctx.pendingCode);
    g_ctx.pendingCode.clear();

    std::printf("[*] OTP: executing overlay code (%zu bytes)\n", code.size());

    const int loadResult = g_ctx.fnLoadBuffer(g_ctx.luaState,
                                              code.c_str(),
                                              code.size(),
                                              code.c_str());
    if (loadResult != 0) {
        const char* err = g_ctx.fnToLString(g_ctx.luaState, -1, nullptr);
        std::printf("[-] OTP: compile error: %s\n", err ? err : "unknown");
        return;
    }

    const int execResult = g_ctx.fnPCall(g_ctx.luaState, 0, LUA_MULTRET, 0);
    if (execResult == 0)
        std::printf("[+] OTP: execution finished\n");
    else {
        const char* err = g_ctx.fnToLString(g_ctx.luaState, -1, nullptr);
        std::printf("[-] OTP: runtime error: %s\n", err ? err : "unknown");
    }
}

void ExecuteFile(const char* path)
{
    if (!g_ctx.luaState) {
        std::printf("[-] OTP: lua_State not captured yet\n");
        return;
    }
    g_ctx.pendingFile = path;
}

static void ProcessPendingFile()
{
    if (!g_ctx.pendingFile || !g_ctx.luaState) return;

    const char* path  = g_ctx.pendingFile;
    g_ctx.pendingFile = nullptr;

    std::printf("[*] OTP: loading '%s'\n", path);

    const int loadResult = g_ctx.fnLoadFile(g_ctx.luaState, path);
    if (loadResult != 0) {
        const char* err = g_ctx.fnToLString(g_ctx.luaState, -1, nullptr);
        std::printf("[-] OTP: compile error: %s\n", err ? err : "unknown");
        return;
    }

    std::printf("[*] OTP: executing...\n");
    const int execResult = g_ctx.fnPCall(g_ctx.luaState, 0, LUA_MULTRET, 0);
    if (execResult == 0)
        std::printf("[+] OTP: execution finished\n");
    else {
        const char* err = g_ctx.fnToLString(g_ctx.luaState, -1, nullptr);
        std::printf("[-] OTP: runtime error: %s\n", err ? err : "unknown");
    }
}

static void RegisterLuaFunctions()
{
    // Keyboard
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_PressKey,   0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "ckey");

    // Log
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_Print,      0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "clog");

    // Real mouse
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_MoveCursor, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "cmove");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_SendClick,  0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "cclick");

    // Virtual mouse v1 — PostMessage
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_MoveMouse,  0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mmove");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ClickMouse, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mclick");

    // Virtual mouse v2 — SendMessage
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_MoveMouse2, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mmove2");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ClickMouse2,0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mclick2");

    // Virtual mouse v3 — absolute SendInput
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_MoveMouse3, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mmove3");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ClickMouse3,0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "mclick3");

    // Memory read
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ReadInt,    0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "rmem");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ReadFloat,  0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "rmemf");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_ReadString, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "rmems");

    // Memory write
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_WriteInt,   0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "wmem");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_WriteFloat, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "wmemf");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_WriteString,0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "wmems");

    // Window
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_GetWindowSize, 0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "wsize");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_GetWindowPos,  0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "wpos");
    g_ctx.fnPushClosure(g_ctx.luaState, Lua_GetCursorPos,  0); g_ctx.fnSetField(g_ctx.luaState, LUA_GLOBALSINDEX, "cpos");

    std::printf("[+] OTP: Lua functions registered\n");
}

// ================================================================
// Helper macro for rising-edge hotkey detection
// ================================================================

#define DETECT_KEY(vk, flagVar, path)                              \
    {                                                              \
        bool _now = (GetAsyncKeyState(vk) & 0x8000) != 0;         \
        if (_now && !(flagVar)) {                                  \
            (flagVar) = true;                                      \
            std::printf("[*] OTP: %s — loading %s\n", #vk, path); \
            ExecuteFile(path);                                     \
        } else if (!_now) { (flagVar) = false; }                   \
    }

// ================================================================
// Hook callbacks
// ================================================================

/**
 * @brief Calls the global Lua Update() function if defined by the user script.
 * Safe to call every tick — skips silently when Update is nil.
 */
static void CallUpdate()
{
    if (!g_ctx.luaState || !s_functionsRegistered) return;

    static const char s_chunk[] = "if Update then Update() end";

    const int lr = g_ctx.fnLoadBuffer(g_ctx.luaState, s_chunk, sizeof(s_chunk) - 1, s_chunk);
    if (lr == 0)
        g_ctx.fnPCall(g_ctx.luaState, 0, 0, 0);
}

/**
 * @brief Hook for the game tick function.
 *
 * Execution order:
 *   1. Detect F5-F8 hotkeys → queue corresponding script files
 *   2. On the Lua thread: register functions once, process pending scripts
 *   3. Call Update() if defined in the user script
 *   4. Call the original tick function
 */
LARGE_INTEGER __cdecl Hook_Tick()
{
    DETECT_KEY(VK_F5, s_f5Pressed, "C:\\test1.txt")
    DETECT_KEY(VK_F6, s_f6Pressed, "C:\\test2.txt")
    DETECT_KEY(VK_F7, s_f7Pressed, "C:\\test3.txt")
    DETECT_KEY(VK_F8, s_f8Pressed, "C:\\test4.txt")

    if (IsLuaThread()) {
        if (!s_functionsRegistered && g_ctx.luaState) {
            s_functionsRegistered = true;
            RegisterLuaFunctions();
        }
        ProcessPendingFile();
        ProcessPendingCode();
        CallUpdate();
    }

    return g_ctx.fnTick();
}

/**
 * @brief Hook for luaL_loadbuffer — captures lua_State on first call.
 * 4-parameter signature (no fifth a5 like PxG).
 */
int __cdecl Hook_LoadBuffer(lua_State* L, const char* buff, size_t size, const char* name)
{
    if (!g_ctx.luaState) {
        g_ctx.luaState    = L;
        g_ctx.luaThreadId = GetCurrentThreadId();
        std::printf("[+] OTP: lua_State captured at %p (thread %lu)\n",
            static_cast<void*>(L), g_ctx.luaThreadId);
    }

    return g_ctx.fnLoadBuffer(L, buff, size, name);
}

// ================================================================
// Hook installation
// ================================================================

bool InstallHooks()
{
    AllocConsole();
    FILE* file = nullptr;
    freopen_s(&file, "CONOUT$", "w", stdout);
    freopen_s(&file, "CONOUT$", "w", stderr);
    freopen_s(&file, "CONIN$",  "r", stdin);
    SetConsoleTitleA("LuaForge — OTP");

    g_ctx.baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));

    g_ctx.fnToLString   = reinterpret_cast<FnToLString>  (shared::AobScan("56 8B 74 24 ? 57 8B 7C 24 ? 56 57 E8 ? ? ? ? 83 C4 ? 8B 48 ? 83 F9 ? 75 ? ? ? EB ? 83 F9 ? 77 ? 8B 4F ? 8B 41 ? 3B 41 ? 72 ? 8B CF E8 ? ? ? ? 56 57 E8 ? ? ? ? 83 C4 ? 8B F0 8B D6 8B CF E8 ? ? ? ? ? ? C7 46 ? ? ? ? ? 8B 54 24 ? 85 D2 74 ? 8B 48 ? ? ? 5F 83 C0 ? 5E C3 8B 44 24"));
    g_ctx.fnPushClosure = reinterpret_cast<FnPushCClosure>(shared::AobScan("56 8B 74 24 ? 8B 4E ? 8B 41 ? 3B 41 ? 72 ? 8B CE E8 ? ? ? ? 8B 46"));
    g_ctx.fnSetField    = reinterpret_cast<FnSetField>   (shared::AobScan("55 8B EC 83 E4 ? 83 EC ? 56 8B 75 ? 57 FF 75 ? 56 E8 ? ? ? ? 8B 55 ? 8B F8 8B CA 83 C4 ? 8D 41 ? 89 44 24 ? ? ? 41 84 C0 75 ? 2B 4C 24 ? 51 52 56 E8 ? ? ? ? 89 44 24 ? 8D 44 24 ? 50 57 56 C7 44 24 ? ? ? ? ? E8 ? ? ? ? 8B 56"));
    g_ctx.fnToInteger   = reinterpret_cast<FnToInteger>  (shared::AobScan("55 8B EC 83 E4 ? 83 EC ? FF 75 ? FF 75 ? E8 ? ? ? ? 83 C4 ? 8B 48 ? 83 F9 ? 73 ? ? ? ? ? F2 0F 2C C0 8B E5 5D C3 83 F9 ? 75 ? ? ? ? ? ? E8 ? ? ? ? 85 C0 74 ? ? ? ? ? ? F2 0F 2C C0 8B E5 5D C3 33 C0"));
    g_ctx.fnPCall       = reinterpret_cast<FnPCall>      (shared::AobScan("8B 54 24 ? 8B 44 24 ? 53 56"));
    g_ctx.fnLoadFile    = reinterpret_cast<FnLoadFile>   (shared::AobScan("55 8B EC 83 E4 ? 81 EC ? ? ? ? A1 ? ? ? ? 33 C4 89 84 24 ? ? ? ? 56 8B 75 ? 57 8B 7D"));
    void* addrLoadBuffer = reinterpret_cast<void*>(shared::AobScan("55 8B EC 8B 45 ? 8B 0D ? ? ? ? 85 C9"));
    void* addrTick       = reinterpret_cast<void*>(shared::AobScan("55 8B EC 51 51 8D 45 ? 50 FF 15"));

    if (!g_ctx.fnToLString  || !g_ctx.fnPushClosure || !g_ctx.fnSetField  ||
        !g_ctx.fnToInteger  || !g_ctx.fnPCall        || !g_ctx.fnLoadFile ||
        !addrLoadBuffer     || !addrTick) {
        std::printf("[-] OTP: one or more AOB scans failed\n");
        return false;
    }

    std::printf("[*] OTP: base=0x%llX | LoadBuffer=%p | Tick=%p\n",
        (unsigned long long)g_ctx.baseAddress, addrLoadBuffer, addrTick);

    if (MH_Initialize() != MH_OK) {
        std::printf("[-] OTP: MH_Initialize failed\n");
        return false;
    }

    if (MH_CreateHook(addrLoadBuffer, &Hook_LoadBuffer,
                      reinterpret_cast<void**>(&g_ctx.fnLoadBuffer)) != MH_OK ||
        MH_EnableHook(addrLoadBuffer) != MH_OK) {
        std::printf("[-] OTP: LoadBuffer hook failed\n");
        return false;
    }

    if (MH_CreateHook(addrTick, &Hook_Tick,
                      reinterpret_cast<void**>(&g_ctx.fnTick)) != MH_OK ||
        MH_EnableHook(addrTick) != MH_OK) {
        std::printf("[-] OTP: Tick hook failed\n");
        return false;
    }

    std::printf("[+] OTP: hooks installed successfully\n");
    return true;
}

} // namespace otp
