/*
 * OTP_Hooks.cpp - Hook management and context for OT Pokemon
 *
 * Differences from PxG_Hooks.cpp:
 *   - Scripts executed only via ExecuteString (menu textbox) — no file loading
 *   - FnTick returns LARGE_INTEGER (not LONGLONG)
 *   - Hook_LoadBuffer has 4 parameters (no fifth __int64 a5 like PxG)
 */

#include "OTP.h"
#include "../shared/AobScanner.h"
#include <cstring>

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

// ================================================================
// Stack-walk spoof (anti-anticheat)
// ================================================================
//
// The game protects luaL_loadbuffer (and 4 sibling APIs) with a runtime
// dispatcher that calls RtlCaptureStackBackTrace and rejects any caller
// whose return address falls outside the main executable's .text range.
// Since our Hook_LoadBuffer lives in this injected DLL, every call we
// make through fnLoadBuffer would be detected.
//
// The dispatcher invokes ntdll!RtlCaptureStackBackTrace via the game's
// IAT (`call ds:...`). Redirecting that single IAT entry to a stub that
// returns a fake stack of in-.text return addresses defeats the check
// without touching the AC's own code (its monitor thread checks vtable
// integrity, not the IAT).

using FnRtlCSBT = USHORT (WINAPI*)(DWORD, DWORD, PVOID*, PDWORD);

static FnRtlCSBT s_origRtlCSBT = nullptr;
static PVOID     s_textAnchor  = nullptr;

static USHORT WINAPI Hook_RtlCaptureStackBackTrace(DWORD skip, DWORD count, PVOID* trace, PDWORD hash)
{
    const DWORD n = (count > 8) ? 8 : count;
    for (DWORD i = 0; i < n; ++i) trace[i] = s_textAnchor;
    if (hash) *hash = 0;
    return static_cast<USHORT>(n);
}

/* Returns an address inside .text that is preceded by an E8 byte —
   satisfies both the range check and the "CALL opcode before retaddr"
   sub-check (sub_13C15D0). */
static PVOID FindCallSiteAnchor()
{
    HMODULE base = GetModuleHandleA(nullptr);
    if (!base) return nullptr;

    auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    auto nt  = reinterpret_cast<PIMAGE_NT_HEADERS>(
                  reinterpret_cast<BYTE*>(base) + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (std::memcmp(sec->Name, ".text", 5) != 0) continue;
        BYTE* start = reinterpret_cast<BYTE*>(base) + sec->VirtualAddress;
        const DWORD size = sec->Misc.VirtualSize;
        for (DWORD j = 0; j + 5 < size; ++j) {
            if (start[j] == 0xE8) return start + j + 5;
        }
    }
    return nullptr;
}

static bool InstallStackSpoof()
{
    s_textAnchor = FindCallSiteAnchor();
    if (!s_textAnchor) {
        std::printf("[-] OTP: stack spoof — no E8 anchor found in .text\n");
        return false;
    }

    HMODULE base = GetModuleHandleA(nullptr);
    auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    auto nt  = reinterpret_cast<PIMAGE_NT_HEADERS>(
                  reinterpret_cast<BYTE*>(base) + dos->e_lfanew);
    const DWORD impRVA =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!impRVA) {
        std::printf("[-] OTP: stack spoof — no import directory\n");
        return false;
    }

    auto desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
                    reinterpret_cast<BYTE*>(base) + impRVA);

    for (; desc->Name; ++desc) {
        auto thunkOrig = reinterpret_cast<PIMAGE_THUNK_DATA>(
                             reinterpret_cast<BYTE*>(base) + desc->OriginalFirstThunk);
        auto thunkReal = reinterpret_cast<PIMAGE_THUNK_DATA>(
                             reinterpret_cast<BYTE*>(base) + desc->FirstThunk);

        for (; thunkOrig->u1.AddressOfData; ++thunkOrig, ++thunkReal) {
            if (thunkOrig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;

            auto imp = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
                           reinterpret_cast<BYTE*>(base) + thunkOrig->u1.AddressOfData);
            if (lstrcmpA(reinterpret_cast<const char*>(imp->Name),
                         "RtlCaptureStackBackTrace") != 0) continue;

            DWORD oldProt = 0;
            if (!VirtualProtect(&thunkReal->u1.Function, sizeof(uintptr_t),
                                PAGE_READWRITE, &oldProt)) {
                std::printf("[-] OTP: stack spoof — VirtualProtect failed\n");
                return false;
            }

            s_origRtlCSBT = reinterpret_cast<FnRtlCSBT>(thunkReal->u1.Function);
            thunkReal->u1.Function =
                reinterpret_cast<uintptr_t>(&Hook_RtlCaptureStackBackTrace);

            VirtualProtect(&thunkReal->u1.Function, sizeof(uintptr_t),
                           oldProt, &oldProt);

            std::printf("[+] OTP: stack spoof installed (anchor=%p)\n", s_textAnchor);
            return true;
        }
    }

    std::printf("[-] OTP: RtlCaptureStackBackTrace not found in IAT\n");
    return false;
}

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
                                              "=(menu)");
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

    static LONG s_callCount = 0;
    const LONG  callN       = InterlockedIncrement(&s_callCount);
    std::printf("[*] OTP: Hook_LoadBuffer #%ld | name=\"%s\" | size=%zu\n",
        callN, name ? name : "(null)", size);

    const int result = g_ctx.fnLoadBuffer(L, buff, size, name);
    if (result != 0)
        std::printf("[-] OTP: Hook_LoadBuffer #%ld returned error %d\n", callN, result);

    return result;
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

    if (!InstallStackSpoof()) {
        std::printf("[-] OTP: aborting — stack spoof required to bypass AC\n");
        return false;
    }

    g_ctx.baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));

    g_ctx.fnToLString   = reinterpret_cast<FnToLString>  (shared::AobScan("56 8B 74 24 ? 57 8B 7C 24 ? 56 57 E8 ? ? ? ? 83 C4 ? 8B 48 ? 83 F9 ? 75 ? ? ? EB ? 83 F9 ? 77 ? 8B 4F ? 8B 41 ? 3B 41 ? 72 ? 8B CF E8 ? ? ? ? 56 57 E8 ? ? ? ? 83 C4 ? 8B F0 8B D6 8B CF E8 ? ? ? ? ? ? C7 46 ? ? ? ? ? 8B 54 24 ? 85 D2 74 ? 8B 48 ? ? ? 5F 83 C0 ? 5E C3 8B 44 24"));
    g_ctx.fnPushClosure = reinterpret_cast<FnPushCClosure>(shared::AobScan("56 8B 74 24 ? 8B 4E ? 8B 41 ? 3B 41 ? 72 ? 8B CE E8 ? ? ? ? 8B 46"));
    g_ctx.fnSetField    = reinterpret_cast<FnSetField>   (shared::AobScan("55 8B EC 83 E4 ? 83 EC ? 56 8B 75 ? 57 FF 75 ? 56 E8 ? ? ? ? 8B 55 ? 8B F8 8B CA 83 C4 ? 8D 41 ? 89 44 24 ? ? ? 41 84 C0 75 ? 2B 4C 24 ? 51 52 56 E8 ? ? ? ? 89 44 24 ? 8D 44 24 ? 50 57 56 C7 44 24 ? ? ? ? ? E8 ? ? ? ? 8B 56"));
    g_ctx.fnToInteger   = reinterpret_cast<FnToInteger>  (shared::AobScan("55 8B EC 83 E4 ? 83 EC ? FF 75 ? FF 75 ? E8 ? ? ? ? 83 C4 ? 8B 48 ? 83 F9 ? 73 ? ? ? ? ? F2 0F 2C C0 8B E5 5D C3 83 F9 ? 75 ? ? ? ? ? ? E8 ? ? ? ? 85 C0 74 ? ? ? ? ? ? F2 0F 2C C0 8B E5 5D C3 33 C0"));
    g_ctx.fnPCall       = reinterpret_cast<FnPCall>      (shared::AobScan("8B 54 24 ? 8B 44 24 ? 53 56"));
    // Scan finds sub_13E8320 (trampoline: "jmp sub_13C1810").
    // Follow the E9 rel32 to resolve the real dispatcher address (sub_13C1810).
    const uintptr_t addrTrampoline = shared::AobScan(
        "E9 ? ? ? ? F8 81 EC ? ? ? ? 8B 45 ? B9 ? ? ? ? 89 44 24 ? "
        "8B 45 ? 89 44 24 ? 8D 44 24");
    void* addrLoadBuffer = nullptr;
    if (addrTrampoline) {
        const int32_t rel = *reinterpret_cast<const int32_t*>(addrTrampoline + 1);
        addrLoadBuffer = reinterpret_cast<void*>(addrTrampoline + 5 + rel);
    }
    void* addrTick       = reinterpret_cast<void*>(shared::AobScan("55 8B EC 51 51 8D 45 ? 50 FF 15"));

    if (!g_ctx.fnToLString  || !g_ctx.fnPushClosure || !g_ctx.fnSetField ||
        !g_ctx.fnToInteger  || !g_ctx.fnPCall        ||
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
